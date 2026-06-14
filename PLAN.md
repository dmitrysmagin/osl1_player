# medplay — OSL1/Adlib player in C + SDL2 + Nuked-OPL3 (MinGW-w64)

Plan of record. Targets a standalone Windows console tool, `medplay.exe`, that
plays Ocean OSL1 songs (`.ALB`/`.ADL`/`.LAP`/`.SCC`) through a clean-room
re-implementation of `TRACKER.DRV` + `ADLIB.DEV`, synthesised by Nuked-OPL3 and
output via SDL2.

> Status: the two former unknowns (compressed row decoder, Adlib note table)
> are now fully reverse-engineered — see **Appendix A** and **Appendix B**.
> They are implementable as written; the project is de-risked.

---

## 1. Objective & scope

**In scope (v1):** OSL1 parsing, the 50 Hz tick engine, the Adlib (OPL2) note/
effect path, an OPL3 backend (Nuked), SDL2 audio + timing, a CLI, and an offline
`--wav` render for testing.

**Out of scope (v1):** Roland MT-32 and Sound Blaster SCC1 backends; GUI/editor;
file writing. (The engine is device-independent, so those are later backends.)

## 2. Strategy

The DOS stack is two flat 16-bit blobs talking through a far-call vtable. The
port is mechanical:

| DOS component | Our C module |
|---|---|
| `TRACKER.DRV` replay engine (RE'd) | `replay.c` — tick engine, row decode, effects |
| `ADLIB.DEV` vtable routines (RE'd) | `opl_dev.c` — note on/off, pitch→fnum, patch upload, volume |
| `OUT 0x388/0x389` port I/O | `OPL3_WriteReg(&chip, reg, val)` |
| IRQ0 @ 50 Hz | sample-counter tick scheduler in the SDL audio callback |
| INT-based API dispatch | direct C calls |

Only `opl_dev.c` is Adlib-specific; the engine is shared by any future backend.

## 3. Architecture

```
file.alb ─► osl1.c ──► Song{instruments[], order[], pos_ptr[], raw[]}
                         │
                         ▼
            replay.c  replay_tick() @50Hz
              16 channels × {speed,tick,order,voices[16]}
              decode_row()  ◄── 2-bit bitstream (Appendix A)
              effects[32], trigger_note, send_volume
                         │  callbacks
                         ▼
            opl_dev.c  note_on/off, set_pitch (fnum table, Appendix B),
                       program (ADL→OPL regs), 9/18-voice allocator
                         │  OPL3_WriteReg()
                         ▼
            nukedopl.c (vendored)  → int16 stereo
                         │
                         ▼
            audio.c  SDL2 callback + fractional tick scheduler
```

## 4. Modules & data model

### osl1.c (parse per RE-REPORT §4/§8; validated by `decode_row` @0x1416)
```c
typedef struct { uint16_t len,p1,p2; char name[20]; uint8_t adl[16]; } Instrument;
typedef struct {
    char     subtitle[16];
    uint16_t track_count, row_count;
    uint8_t  order_count, order[256];     // block+0x50
    uint32_t pos_ptr[256];                // block+0x150 (file-absolute)
} PatternBlock;
typedef struct {
    uint8_t   device; char title[30];
    uint32_t  block_off; uint16_t instr_count;
    Instrument instr[128]; PatternBlock blk;
    uint8_t  *raw; size_t size;           // keep file for position data
} Song;
```

### replay.c — transcription of the annotated engine
- `Channel` mirrors the 57-byte block: `flags, voice_count, speed, tick,
  order_idx, restart_idx, pos, order_len, byte_cursor, master_vol, device_id,
  voice_ptr[16]`.
- `replay_tick()` (0xF36): per active channel `tick++`; on `tick==speed` →
  `decode_row()` + `trigger_note()` per voice; else `tick_effects()`.
- Loop/end handling (0xFCB–0xFF7): position vs `row_count` (Appendix A header),
  one-shot bit, restart index.
- `effects[32]` jump table (0x1103). **Confirmed: effect 0x0C = set volume**
  (0x1037). Others decoded incrementally; unknown → no-op.

### opl_dev.c — clean-room ADLIB.DEV (Appendix B)
- 9 melodic voices (OPL2) or 18 (OPL3) — allocator mirrors the 9-slot table at
  `0x53C`.
- `program(voice, instr)`: upload 16-byte ADL patch → OPL operator regs
  (RE-REPORT §5).
- `note_on(voice, note)`: fnum = `FNUM[note%12]`, block = `note/12`; write
  `0xA0` (fnum lo) + `0xB0` (fnum hi | block<<2 | KEYON 0x20).
- `note_off(voice)`: clear KEYON bit in `0xB0` shadow.
- `set_volume(voice, vol)`: reg `0x40` TL = `(0x3F - scaled_vol) & 0x3F | KSL`.

### audio.c / main.c — §5–6.

## 5. Nuked-OPL3 integration
- Vendor `nukedopl.c`/`.h` under `third_party/nuked/` (ISC licence).
- `OPL3_Reset(&chip, rate)`; enable OPL3 (`WriteReg(0x105,1)`) when
  `track_count > 9` (e.g. `ALL.LAP` = 11) for 18 two-op voices; else OPL2 mode.
- Register writes via `OPL3_WriteReg`. Upper 9 channels live in the `0x100` bank.
- Render with `OPL3_GenerateStream(&chip, buf, frames)` (interleaved S16).
- Accurate native rate 49716 Hz; expose `--rate` (default 49716).

## 6. SDL2 audio + the 50 Hz timing bridge (audio is the master clock)
```c
// samples_per_tick = rate / 50.0  (fractional; keep a Q16 or double accumulator)
void audio_cb(void *u, Uint8 *stream, int len) {
    int frames = len/4; int16_t *out=(int16_t*)stream;
    while (frames > 0) {
        if (tick_accum <= 0) { replay_tick(&player); tick_accum += samples_per_tick; }
        int chunk = MIN(frames, (int)tick_accum);
        OPL3_GenerateStream(&chip, out, chunk);
        out += chunk*2; frames -= chunk; tick_accum -= chunk;
    }
}
```
- Fractional `samples_per_tick` avoids drift (49716/50 = 994.32).
- Tempo change (`set_tempo`, 0x49D) recomputes `samples_per_tick = rate/hz`
  where `hz = 1193182 / (0x34DC / tempo)` to match DOS (or simply `rate/50`
  scaled by tempo ratio).
- `--wav` mode runs the same loop writing RIFF/WAVE (deterministic tests).
- Device: `SDL_AudioSpec{ freq=rate, AUDIO_S16SYS, channels=2, samples=1024 }`.

## 7. Build system (MinGW-w64 / MSYS2 UCRT64)
```make
CC      = x86_64-w64-mingw32-gcc
CFLAGS  = -O2 -std=c11 -Wall -Wextra -Ithird_party/nuked $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs)
SRC     = src/main.c src/osl1.c src/replay.c src/opl_dev.c src/audio.c \
          third_party/nuked/nukedopl.c
medplay.exe: $(SRC); $(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
```
- `pacman -S mingw-w64-x86_64-SDL2` (toolchain present at `msys64/ucrt64`).
- Ship `SDL2.dll` beside the exe; Nuked is compiled in.
- Read OSL1 with explicit `rd_u16le/rd_u32le` — never rely on struct packing.

## 8. Phased milestones
| Phase | Deliverable | Validates |
|---|---|---|
| 0 | Skeleton + Makefile + vendored Nuked + SDL tone test | toolchain/audio |
| 1 | `osl1.c` parity with `osl1_dump.py` on `OD1.ALB`,`TITLE.ADL` | parsing |
| 2 | `opl_dev.c` + scale test (one ADL patch, fnum table) | OPL backend |
| 3 | `replay.c` tick/order + `decode_row` (Appendix A) → `--wav` | engine |
| 4 | SDL2 live playback + tick scheduler + tempo | timing |
| 5 | effects table, volume/0x0C, loop points | fidelity |
| 6 | CLI polish, `--opl3` for >9 tracks, dir play, status | usability |

## 9. Risks (post-RE)
1. **Effects table** (32 entries @0x1103): only 0x0C confirmed. *Mitigation:* RE
   incrementally; unknowns become no-ops (non-fatal).
2. **Variable bit-alignment path** in `decode_row` (header bit 0x4000, table
   @0x14DA): rare; verify against a real capture if a file uses it.
3. **Voice-allocation parity** with `ADLIB.DEV`'s 9-slot scheme → note stealing.
   *Mitigation:* mirror the slot table exactly (Appendix B).
4. **Rate mismatch** Nuked vs SDL device. *Mitigation:* request 49716; else run
   Nuked at device rate (rate-agnostic).

## 10. Validation
- Parser parity vs `osl1_dump.py` on every Adlib OSL1 file.
- **Register-trace diff:** `--trace` logs every `OPL3_WriteReg`; diff against a
  DOSBox OPL register log of `MED.EXE` playing the same song (strongest signal).
- Golden WAVs for `OD1.ALB`, `SHUTIT/TITLE.ADL`, `TFX/INTRO.ADL`, `ALL.LAP`.

## 11. Layout
```
medplay/
├── PLAN.md
├── Makefile
├── src/{main,osl1,replay,opl_dev,audio}.c (+ headers)
├── third_party/nuked/{nukedopl.c,nukedopl.h}
├── tools/cmp_dump.sh
└── tests/golden/*.wav,*.trace
```

## 12. Stretch goals
SDL2 status window (order/pattern/VU); OPL3 4-op patches + per-channel pan;
seek/loop UI; batch `--wav`; SCC1 and Roland backends on the same engine.

---

# Appendix A — Compressed row decoder (RE'd: `decode_row` @0x1416)

`decode_row(channel)` is called once per **row** and fills one 8-byte cell for
every voice. State persists in the channel block: `pos` (+0x0A, row counter) and
`byte_cursor` (+0x0C, byte offset into the position stream).

### A.1 Locate the position stream
```
les di, channel.song_block            ; +0x12
pat = order[ channel.order_idx ]      ; block+0x50  (order table)
di  = *(u32*)(block + 0x150 + pat*4)  ; position-data pointer (file-absolute)
hdr = u16[di]                         ; position header word
```
- `hdr & 0x8000` **clear** → uncompressed/“repeat” position: each cell is a raw
  8-byte copy (helper @0x15A2), cursor advances by 0x80 per row. (Edge case.)
- `hdr & 0x8000` **set** → compressed (normal). Then:
```
flags     = hdr                        ; saved at 0x15C0
row_count = hdr & 0x0FFF               ; saved at 0x15C2 (= song length / loop cmp)
extra     = u16[di+2]                  ; secondary run count (BP)
di += 4                                ; skip header
si  = di + channel.byte_cursor         ; resume in stream
```

### A.2 Per-row bitstream window (one row = type codes + params)
A 32-bit window `(dx:bx)` holds up to sixteen 2-bit type codes (one per voice),
read **MSB-first** (dx is the high word):
```
if (flags & 0x4000) {                  ; variable alignment (rare)
    dx:bx = u32[si];
    n = ALIGN_TAB[extra];              ; table @0x14DA, see A.4
    si += n;  preshift (4-n)*8 bits;   ; rcl dx,1 chain
} else {
    dx:bx = u32[si]; si += 4;          ; common path
}
for (v = 0; v < voice_count; v++)
    decode_cell(&voice[v], &si, &dx, &bx, flags);
channel.byte_cursor += (si - di);      ; persist cursor (+0x0C)
```

### A.3 `decode_cell` (the 2-bit type decoder @0x1526)
Zero the 8-byte cell, then pull a 2-bit code from the top of `dx:bx`
(`shl bx,1; rcl dx,1; rcl code,1` twice):

| code | meaning | bytes read from `si` | written to cell |
|------|---------|----------------------|-----------------|
| `00` | empty | 0 | (all zero) |
| `01` | 2-byte param | 2 | cell[5..6] |
| `10` | two 1-byte params | 2 | cell[0] and cell[4] |
| `11` | full event | 1..7 (see below) | cell[0..7] |

Type `11` (note + effects), gated by header flag bits:
```
cell[0] = u8[si++]                         ; note
if (!(flags & 0x1000)) {                   ; bit12: include period
    cell[1]   = u8[si++];
    cell[2:3] = u16[si]; si += 2;
}
cell[4:5] = u16[si]; si += 2;              ; effect word
cell[6]   = u8[si++];                      ; effect cmd
if (!(flags & 0x2000))                     ; bit13: include effect param
    cell[7] = u8[si++];
```

### A.4 Header flag summary (`flags`, stored @0x15C0)
| bit | mask | meaning |
|-----|------|---------|
| 15 | 0x8000 | 1 = compressed position (else raw 8-byte cells) |
| 14 | 0x4000 | 1 = variable bit-alignment preshift (table @0x14DA) |
| 13 | 0x2000 | 1 = omit cell[7] (effect param) in type-11 |
| 12 | 0x1000 | 1 = omit cell[1..3] (period) in type-11 |
| 11..0 | 0x0FFF | row count for this position |

`ALIGN_TAB` @0x14DA (bytes): `01 01 01 01 01 02 02 02 02 03 03 03 03 04 04 04 04`.

> The resulting **8-byte runtime cell** matches RE-REPORT §10:
> `[0..1]=note/period, [2..7]=effect data`, empty = `0x7F7F`.

---

# Appendix B — Adlib note/frequency layer (RE'd from ADLIB.DEV, 4262 bytes)

ADLIB.DEV is a flat binary; its header is the far-call vtable patched at load.

### B.1 Vtable (file offset → routine; called from TRACKER as `es:[off]`)
| es:[off] | routine | purpose |
|----------|---------|---------|
| 0x08 | 0x0475 | init / reset OPL |
| 0x10 | 0x058A | note off (clear KEYON) |
| 0x14 | 0x05EF | note on / set-volume entry |
| 0x18 | 0x06F1 | set pitch (param 0..99) |
| 0x20 | 0x0617 | program change (select instrument) |
| 0x24 | 0x0692 | set pitch + trigger (→ fnum calc @0xE0C) |
| 0x30 | 0x109C | effect (mostly stub) |
| 0x38/0x3C | 0x109E | stub (clc/retf) |

Header word0 = `0x0002` (device id), word1 = `0x0388` (OPL base port).

### B.2 Per-voice state (9 melodic voices)
| table @ | stride | meaning |
|---------|--------|---------|
| 0x53C | 8 | voice slots: [0]=assigned logical id, … |
| 0x355 | 1 | shadow of OPL reg 0xA0 (fnum low) per channel |
| 0x365 | 1 | shadow of OPL reg 0xB0 (fnum hi/block/KEYON) per channel |
| 0x605 | 1 | current instrument index per voice |
| 0x682 | 1 | current volume per voice (init `0x7F`) |
| 0x729 | 16 | instrument patch storage (16-byte ADL patches) |

### B.3 Note → OPL frequency (THE note table — `FNUM` @0xF61)
Standard Adlib 12-semitone F-number table, base `0x157`:
```
FNUM[12] = { 0x157,0x16C,0x181,0x198,0x1B1,0x1CB,
             0x1E6,0x203,0x222,0x243,0x266,0x28A };
// octave doubling boundaries: 0x2AE, 0x55C, 0xAB8  (=2x,4x,8x base)
fnum  = FNUM[note % 12];
block = note / 12;                 // 0..7
reg_A0 = fnum & 0xFF;              // OPL 0xA0+ch
reg_B0 = ((fnum >> 8) & 3) | (block << 2) | 0x20 /*KEYON*/;  // OPL 0xB0+ch
```
(The 0xE0C path adds fine pitch-bend via interpolation tables @0xF61/0xF11;
not needed for plain note playback — implement bends later.)

### B.4 ADL 16-byte patch → OPL registers (program, RE-REPORT §5)
Operator regs written for modulator op (m) and carrier op (c) of the channel:
```
0x20+m = adl[0]   0x40+m = adl[1]   0x60+m = adl[2]
0x80+m = adl[3]   0xE0+m = adl[4]
0x20+c = adl[6]   0x40+c = adl[7]   0x60+c = adl[8]
0x80+c = adl[9]   0xE0+c = adl[10]
0xC0+ch = adl[12] (feedback/algorithm)
```
(operator offsets per channel: standard OPL `op_offset[ch]` table.)

### B.5 Volume (reg 0x40, @0x105C / 0x1093)
```
// vol in 0..63 (TRACKER clamps to per-voice max); 0x0C effect sets it
TL = (0x3F - vol) & 0x3F;                 // attenuation
reg_40 = TL | (adl[1] & 0xC0);            // keep KSL bits from patch
```

### B.6 OPL register write (@0x144)
`out 0x388, reg; out 0x389, val` with the mandatory post-write delays — replace
entirely with `OPL3_WriteReg(&chip, reg, val)` (Nuked handles timing).
