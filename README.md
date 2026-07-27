# medplay — OSL1/Adlib player in C + SDL2 + Nuked-OPL3

`medplay.exe` is a standalone Windows console tool that plays Ocean OSL1 songs
(`.ALB` / `.ADL` / `.LAP` / `.SCC`) through a clean-room re-implementation of the
DOS `TRACKER.DRV` replay engine and `ADLIB.DEV` sound backend, synthesised by
Nuked-OPL3 and output via SDL2.

> **Status:** the Adlib path is implemented and validated. Both former unknowns
> — the compressed row decoder and the Adlib note/instrument model — are fully
> reverse-engineered from the original 16-bit binaries (see **Appendix A** and
> **Appendix B**) and confirmed against a DOSBox register capture of MED.EXE
> (see [Validation](#validation)).

---

## Quick start

Built under MSYS2 UCRT64. Use the UCRT64 `mingw32-make`, **not** MSYS2's
`/usr/bin/make` (see the note in the Makefile for why).

```sh
pacman -S mingw-w64-ucrt-x86_64-SDL2      # one-off dependency
mingw32-make                              # builds medplay.exe
./medplay.exe test/TITLE.ADL              # live playback
```

Ship `SDL2.dll` beside the executable; Nuked-OPL3 is compiled in.

## Command line

```
medplay <song|dir> [options]      live playback
medplay --wav <out> <song>        render one song to a WAV file
medplay --wav <dir> <songdir>     render every song in a directory
medplay --scale [patch.adl]       standalone scale/arpeggio demo

options:
  --wav <path>     offline render (file for a song, dir for a dir)
  --trace <file>   log every OPL register write (RRR=VV) for diffing
  --rate <hz>      output sample rate (default 48000; Nuked resamples)
  --opl2 / --opl3  force OPL2 (9 voices) or OPL3 (18); default: auto
  --speed <n>      initial ticks/row (default 6)
  --status         print a progress/status line
```

The deterministic `--wav` render plus `--trace` are the primary regression tools:
they let the OPL output be diffed byte-for-byte against a reference capture.

---

## 1. Objective & scope

**In scope:** OSL1 parsing, the 50 Hz tick engine, the Adlib (OPL2/OPL3)
note/effect path, the Nuked-OPL3 backend, SDL2 audio + timing, a CLI, and an
offline `--wav` render for testing.

**Out of scope (for now):** Roland MT-32 and Sound Blaster SCC1 backends;
GUI/editor; file writing. The replay engine is device-independent, so those are
future backends on the same core.

## 2. How it maps onto the DOS stack

The DOS stack is two flat 16-bit blobs talking through a far-call vtable. The
port is mechanical:

| DOS component | Our C module |
|---|---|
| `TRACKER.DRV` replay engine | `replay.c` — tick engine, row decode, effects |
| `ADLIB.DEV` vtable routines | `opl_dev.c` — note on/off, pitch→fnum, patch upload, volume |
| `OUT 0x388/0x389` port I/O | `OPL3_WriteRegBuffered(&chip, reg, val)` |
| IRQ0 @ 50 Hz | sample-counter tick scheduler driving `OPL3_GenerateStream` |
| INT-based API dispatch | direct C calls |

Only `opl_dev.c` is Adlib-specific; the engine is shared by any future backend.

## 3. Architecture

```
file.adl ─► osl1.c ──► Song{ instr[], order[], pos_ptr[], raw[] }
                         │
                         ▼
            replay.c  replay_tick() @ 50 Hz
              channel state, per-voice cells
              decode_cell()  ◄── 2-bit bitstream (Appendix A)
              trigger_row, effects, send_volume
                         │  callbacks
                         ▼
            opl_dev.c  note_on/off, pitch→fnum (Appendix B),
                       program (ADL→OPL regs), 9/18-voice model
                         │  OPL3_WriteRegBuffered()
                         ▼
            opl3.c (vendored Nuked-OPL3)  → int16 stereo
                         │
                         ▼
            main.c  SDL2 audio + fractional 50 Hz tick scheduler
                    (and the --wav / --trace offline path)
```

## 4. Modules & data model

### `osl1.c` — OSL1 container parser
Parses the header, the instrument pointer table (`@0x50`, one u32 per
instrument), each instrument record, and the pattern block (order table
`@block+0x50`, position-data pointers `@block+0x150`). Validated for parity
against the reference Python dumper on `OD1.ALB`, `TITLE.ADL` and `ALL.LAP`.

```c
typedef struct {
    uint16_t offset, len, p1, p2;
    char     name[21];
    uint8_t  adl[16];        // Adlib patch, read from record +0x2E
    int      valid;
} Instrument;

typedef struct {
    char     subtitle[17];
    uint16_t track_count, row_count;
    uint16_t defaults[8];
    uint8_t  order_count, order[256];   // block+0x50
    uint32_t pos_ptr[256];              // block+0x150 (file-absolute)
} PatternBlock;

typedef struct {
    uint8_t   device; char title[31];
    uint32_t  block_off; uint16_t instr_count, instr_size;
    Instrument instr[/* OSL1_MAX_INSTR */]; PatternBlock blk;
    uint8_t  *raw; size_t size;         // file kept for position data
} Song;
```

**Instrument record layout** (stride `0x3E`): `+0x00` len, `+0x04` p1, `+0x06`
p2, `+0x0A` 20-byte name, `+0x1E` a non-Adlib device sub-record, **`+0x2E` the
16-byte Adlib patch**. Reading the patch from `+0x1E` (the earlier guess) loads
the wrong bytes — the real Adlib operator data is at `+0x2E`, confirmed
byte-for-byte against the DRO capture.

### `replay.c` — clean-room `TRACKER.DRV`
- Per-voice runtime cell `RVoice.b[]` (written by `decode_cell` @0x1526):
  `b[0]`=duration, **`b[1]`=primary note**, **`b[2..4]`=up to three chord notes**,
  **`b[5]`=instrument selector**, `b[6]`=effect command, `b[7]`=effect parameter,
  `b[12]`=current volume, `b[14]`=last note. (`b[2..4]` were previously guessed to
  be a period word; `trigger_note` @0xE30 keys them as three additional notes.)
- `trigger_row` keys the primary note `b[1]` and each non-zero chord note
  `b[2..4]` through the dynamic voice allocator (below), sharing one logical
  channel id (the track index) across all of a channel's physical voices, and
  uploads the instrument selected by `b[5]` (**file instrument index = `b[5] − 2`**)
  before the key-on.
- `replay_tick()`: per active channel `tick++`; on `tick == speed` decode the
  next row and trigger (`trigger_note` @0x1013); otherwise run per-tick effects
  (`tick_effects` @0x10E3).
- **Effect engine.** Both 32-entry jump tables from `TRACKER.DRV` are ported
  verbatim — the per-tick table @0x1103 (dispatcher @0x10E3) and the row-side
  table @0x115A (dispatcher @0x1143). Index = `cmd & 0x1F`; a set bit 7 routes to
  the `es:0x30` stub (no-op on Adlib). Every handler below was checked
  byte-for-byte against the disassembly:

  | Cmd  | Effect          | Ported from        | Notes |
  |------|-----------------|--------------------|-------|
  | 0x01 | Portamento up   | @0x129D            | `period += param×20`, re-emit pitch (`es:0x24`). The ×20 is the shift routine @0x12BD (`p×4 + p×16`). |
  | 0x02 | Portamento down | @0x12B0            | `period −= param×20`. |
  | 0x03 | Tone portamento | @0x13B5 / @0x1386  | Note row retargets only (no key-on); `target = 0x2000 + (note − base_note)×0x155` clamped to 0x3FFF; slide `speed×20` toward target, snap-and-clear on arrival. |
  | 0x04 | Vibrato         | `es:0x1C` stub     | State-only no-op on Adlib. |
  | 0x05 | Porta+vol slide | `es:0x28` stub     | State-only no-op. |
  | 0x06 | Note off        | @0x12CC (row)      | Keys the channel off. |
  | 0x07 | Tremolo         | `es:0x2C` stub     | State-only no-op. |
  | 0x08 | (reserved)      | `es:0x30` stub     | State-only no-op. |
  | 0x09 | Set speed       | @0x1361 (row)      | ticks/row = `param & 0x1F`. |
  | 0x0A | Volume slide    | @0x1201            | hi-nibble≠0 → up `hi×2` (clamp 0x7F), else down `lo×2` (clamp 0). |
  | 0x0B | Position jump   | @0x133F (row)      | order = `param − 1`. |
  | 0x0C | Set volume      | @0x1358 (row)      | Also consumed in `trigger_note` when riding a note. |
  | 0x0E | Pattern break   | @0x1351 (row)      | Ends the current position. |
  | 0x0F | Set tempo       | @0x1376 (row)      | Timer Hz; clamped to a sane minimum. |
  | 0x1E | Arpeggio/strum  | @0x11A0            | Cycles notes `b[1..4]`, re-keying each via `es:0x08`/`es:0x0C`. |
  | 0x1F | Note retrigger  | @0x1611            | Row side @0x15E8 latches `retrig_note`/count; per-tick counts down and re-keys. |

  Verification: forced-OPL2 `--trace` renders of `PLANET1.ADL`/`TITLE.ADL` hit
  every chromatic base-note F-number the DOSBox `.dro` captures produce (exact
  match on all 12, plus min/max), with a sub-cent median deviation on
  portamento intermediates. Remaining differences are glide granularity and
  dynamic-voice allocation, not effect logic.

### `opl_dev.c` — clean-room `ADLIB.DEV`
- 9 melodic voices (OPL2) or 18 (OPL3).
- `program(voice, adl)` — upload a 16-byte ADL patch to OPL operator registers
  (Appendix B.4).
- `note_on(voice, note)` — table lookup by `note − 12`, write `0xA0`/`0xB0`
  with a key-off/key-on retrigger so the envelope re-attacks each note.
- `note_off(voice)` — clear the KEYON bit in the `0xB0` shadow.
- `set_volume(voice, vol)` — write carrier `0x40` TL from the volume, keeping
  the patch's carrier KSL bits.
- **Dynamic voice allocator** (ADLIB.DEV slot table @0x53C, 9 slots × 8 B),
  layered over the physical-voice calls above:
  - `keyon(chan, note, adl, vol)` — linear **first-free** scan (@0x50C) for a
    physical voice, tag it with the logical channel id `chan`, program `adl`
    (or keep the current patch if `NULL`), key on. Returns −1 if every voice is
    busy — the note is **dropped, no stealing**, exactly as the driver does.
  - `keyoff(chan)` — free every physical voice tagged with `chan` and key it
    off (@0x58F); a channel may own several voices (chords).
  - `chanvol(chan, vol)` — set volume on every voice owned by `chan`.
  - The primary note-on self-frees the channel first (`keyoff` then `keyon`,
    matching @0x475), so successive notes on a channel reuse the lowest free
    voice. Verified against `title.dro`: the per-channel key-on distribution
    matches the DOSBox capture (channels 0–8 allocated in ascending order, then
    reused), within ~1 % (occasional pool-full drops).

### `main.c` — CLI, SDL2 audio, offline render
Argument parsing, the SDL2 device + fractional 50 Hz tick scheduler, the
deterministic `--wav` writer and the `--trace` logger, directory batch mode, and
the standalone `--scale` demo.

## 5. Nuked-OPL3 integration
- Vendored as `src/opl3.c` / `src/opl3.h` (ISC licence), compiled in.
- `OPL3_Reset(&chip, rate)`; OPL3 enabled (`WriteReg(0x105, 1)`) when
  `track_count > 9`, else OPL2 mode. Upper 9 channels live in the `0x100` bank.
- Register writes go through `OPL3_WriteRegBuffered`, which models the small
  per-write ISA-bus delay so a burst of writes at a tick boundary is spread
  across samples the way a real OPL sees them.
- Rendered with `OPL3_GenerateStream` (interleaved S16). `OPL3_Reset(&chip,
  rate)` sets an internal resample ratio `(rate << RSM_FRAC) / 49716`, so we ask
  for a standard device rate (48000 Hz by default) and Nuked resamples from its
  49716 Hz native rate — the output rate is decoupled from the chip's native one.

## 6. SDL2 audio + the 50 Hz timing bridge
Audio is the master clock. A fractional `samples_per_tick = rate / 50.0`
(48000 / 50 = 960) is kept in a `double` accumulator to avoid long-run drift;
`replay_tick()` fires each time the accumulator drains, and the gap between ticks
is rendered with `OPL3_GenerateStream`. The `--wav` path runs the identical loop
writing a RIFF/WAVE file for reproducible tests.

## 7. Build system (MinGW-w64 / MSYS2 UCRT64)
`mingw32-make` targets:

| Target | Output | Purpose |
|---|---|---|
| `all` (default) | `medplay.exe` | full player |
| `dump` | `osl1_dump.exe` | parser-only parity tool |
| `scale` | `opl_scale.exe` | headless OPL backend check (no SDL) |
| `decode` | `decode_dump.exe` | headless replay-decoder validation (no SDL) |
| `run` | — | build and run |
| `clean` | — | remove build artefacts |

The Makefile derives SDL flags from `sdl2-config`/`pkg-config`, strips
`-Dmain=SDL_main` and `-lSDL2main` (we provide `main()` with
`SDL_MAIN_HANDLED`), and swaps `-mwindows` for `-mconsole` so Ctrl-C and stdout
work.

## 8. Validation
- **Parser parity** against the reference OSL1 dumper on every Adlib OSL1 file.
- **Register-trace diff** — `--trace` logs every OPL register write as `RRR=VV`;
  `tools/dro_dump.py` decodes a DOSBox DRO v2.0 capture of MED.EXE into the same
  format so the two streams can be diffed directly. `tools/dro_patches.py` and
  `tools/dro_notes.py` extract the distinct operator patches and note-ons from a
  capture for targeted comparison.
- **Confirmed result:** for `TITLE.ADL`, medplay now emits the same operator
  patches as the real driver (patches P0–P5 match byte-for-byte, modulo the
  deliberate `|0x30` OPL3 L/R enable in the `0xC0` byte) and the decoded pitches
  match the captured note-ons.

### Tools (`tools/`)
| File | Role |
|---|---|
| `osl1_dump.c` | dump parsed OSL1 structure |
| `cell_dump.c` | dump decoded per-voice cells per row |
| `decode_dump.c` | headless replay decode |
| `opl_scale.c` | headless OPL scale test |
| `dro_dump.py` | DRO v2.0 → `RRR=VV` trace |
| `dro_patches.py` | distinct operator patches at note-ons in a DRO |
| `dro_notes.py` | note-on events (frame, channel, note) from a DRO |

## 9. Layout
```
medplay/
├── README.md
├── Makefile
├── src/{main,osl1,replay,opl_dev,opl3}.{c,h}
├── tools/{osl1_dump,cell_dump,decode_dump,opl_scale}.c
│         {dro_dump,dro_patches,dro_notes}.py
└── test/{TITLE.ADL, title.dro, *.trace}
```

## 10. Known gaps & stretch goals
- **Global transpose** (`ds:[0x10E2]`) — assumed 0 (holds for `TITLE.ADL`); the
  chord notes `b[2..4]` are keyed at face value, without the driver's transpose add.
- **Pitch bends** (the fine-pitch interpolation path) — plain notes only so far.
- **Effects** beyond `0x0C` are decoded incrementally; unknowns are no-ops.
- **Stretch:** SDL2 status window (order/pattern/VU), OPL3 4-op patches +
  per-channel pan, seek/loop UI, and SCC1 / Roland backends on the same engine.

---

# Appendix A — Compressed row decoder (`decode_row` @0x1416)

`decode_row(channel)` is called once per **row** and fills one 8-byte cell for
every voice. State persists in the channel block: `pos` (row counter) and
`byte_cursor` (byte offset into the position stream).

### A.1 Locate the position stream
```
pat = order[ channel.order_idx ]         ; block+0x50 (order table)
di  = *(u32*)(block + 0x150 + pat*4)     ; position-data pointer (file-absolute)
hdr = u16[di]                            ; position header word
```
- `hdr & 0x8000` **clear** → uncompressed/"repeat" position: each cell is a raw
  8-byte copy, cursor advances by 0x80 per row (edge case).
- `hdr & 0x8000` **set** → compressed (normal):
```
flags     = hdr                          ; saved at 0x15C0
row_count = hdr & 0x0FFF                 ; song length / loop compare
extra     = u16[di+2]                    ; secondary run count
di += 4                                  ; skip header
si  = di + channel.byte_cursor           ; resume in stream
```

### A.2 Per-row bitstream window
A 32-bit window `(dx:bx)` holds up to sixteen 2-bit type codes (one per voice),
read MSB-first (dx is the high word):
```
if (flags & 0x4000) {                    ; variable alignment (rare)
    dx:bx = u32[si];
    n = ALIGN_TAB[extra];                ; table @0x14DA (see A.4)
    si += n;  preshift (4-n)*8 bits;
} else {
    dx:bx = u32[si]; si += 4;            ; common path
}
for (v = 0; v < voice_count; v++)
    decode_cell(&voice[v], &si, &dx, &bx, flags);
channel.byte_cursor += (si - di);        ; persist cursor
```

### A.3 `decode_cell` — the 2-bit type decoder (@0x1526)
Zero the 8-byte cell, then pull a 2-bit code from the top of `dx:bx`:

| code | meaning | bytes read from `si` | written to cell |
|------|---------|----------------------|-----------------|
| `00` | empty | 0 | (all zero) |
| `01` | 2-byte param | 2 | `b[5..6]` |
| `10` | two 1-byte params | 2 | `b[0]` and `b[4]` |
| `11` | full event | 1..7 (see below) | `b[0..7]` |

Type `11` (note + effects), gated by header flag bits:
```
b[0] = u8[si++]                          ; duration
if (!(flags & 0x1000)) {                 ; bit12: include note slots
    b[1]   = u8[si++];                   ; primary note
    b[2:3] = u16[si]; si += 2;           ; two more note slots (b[2],b[3])
}
b[4:5] = u16[si]; si += 2;               ; b[4] = third note slot, b[5] = instrument
b[6]   = u8[si++];                       ; effect command
if (!(flags & 0x2000))                   ; bit13: include effect param
    b[7] = u8[si++];
```

### A.4 Header flag summary (`flags` @0x15C0)
| bit | mask | meaning |
|-----|------|---------|
| 15 | 0x8000 | 1 = compressed position (else raw 8-byte cells) |
| 14 | 0x4000 | 1 = variable bit-alignment preshift (table @0x14DA) |
| 13 | 0x2000 | 1 = omit `b[7]` (effect param) in type-11 |
| 12 | 0x1000 | 1 = omit `b[1..3]` (note/period) in type-11 |
| 11..0 | 0x0FFF | row count for this position |

`ALIGN_TAB` @0x14DA: `01 01 01 01 01 02 02 02 02 03 03 03 03 04 04 04 04`.

---

# Appendix B — Adlib note/instrument layer (RE'd from ADLIB.DEV, 4262 bytes)

ADLIB.DEV is a flat binary; its header is the far-call vtable patched at load.
Several route labels in the original annotated disassembly were guesses; the
entries below are the ones exercised by playback and verified against the
`title.dro` capture.

### B.1 Vtable entries that matter for playback
| es:[off] | routine | purpose |
|----------|---------|---------|
| 0x08 | 0x0475 | primary note-on: **self-free** channel's voices (@0x58F), then find first free voice (@0x50C), pitch lookup, program operators, key-on |
| 0x0C | 0x0494 | chord/additional note-on: find first free voice + key-on, same channel id, **no** preceding free |
| 0x10 | 0x058A | note off: free every voice tagged with the channel id (clear KEYON) |
| 0x18 | 0x06F1 | 16-byte patch upload to slot `0x729 + DX*16` |
| 0x24 | 0x0692 | program change |

The 9 physical voice slots live at `cs:0x53C` (stride 8 B); slot+0 is the
free-marker (`0xFF` = free, else the owning channel id). Allocation (@0x50C) is
a **linear first-free scan**; a full scan sets carry and the note is **dropped
with no voice-stealing**.

### B.2 Note → OPL frequency (the pitch table)
Both note-on paths (`es:0x08` @0x0475 and `es:0x0C` @0x0494) take the note in
`bh` and read a precomputed `(block<<12)|fnum` from a table at `cs:0x3B5`, indexed by
`(note − 12)`. The lowest playable note is 12. Equivalent to the standard
12-semitone F-number table:
```c
static const uint16_t FNUM[12] = {
    0x157,0x16C,0x181,0x198,0x1B1,0x1CB,
    0x1E6,0x203,0x222,0x243,0x266,0x28A
};
int n     = note - 12;              // note < 12 clamps to 0
fnum      = FNUM[n % 12];
block     = n / 12;                 // 0..7
reg_A0    = fnum & 0xFF;                                     // OPL 0xA0+ch
reg_B0    = ((fnum >> 8) & 3) | (block << 2) | 0x20/*KEYON*/;// OPL 0xB0+ch
```

### B.3 ADL 16-byte patch → OPL registers (operator programmer @0xD69)
Definitive layout, verified byte-for-byte against the capture. For a channel's
modulator op (`m`) and carrier op (`c`):
```
0x20+m = adl[0]   0x40+m = adl[1]   0x60+m = adl[2]   0x80+m = adl[3]   0xE0+m = adl[4]
0x20+c = adl[5]   0x40+c = (adl[6] & 0xC0) | 0x3F      0x60+c = adl[7]
0x80+c = adl[8]   0xE0+c = adl[9]
0xC0+ch = adl[10]                       ; feedback / connection
```
The carrier `0x40` (`adl[6]`) contributes only its **KSL** bits at upload time;
the loudness (TL) is uploaded silent (`|0x3F`) and then driven from the volume
path. `adl[6]` is the carrier `0x40` byte — note the ordering: modulator =
bytes 0..4, carrier = bytes 5..9, feedback/connection = byte 10.

### B.4 Volume (carrier reg 0x40)
```c
// vol in 0..63; effect 0x0C sets it
uint8_t ksl = adl[6] & 0xC0;            // carrier KSL from the patch
uint8_t tl  = (0x3F - vol) & 0x3F;      // attenuation, 0 = loudest
reg_40      = ksl | tl;
```

### B.5 Patch upload (`es:0x14` @0x06F1)
`mov ds,bx; mov si,di; shl dx,1 (×4); di=dx+0x729; mov cx,0x10; rep movsb` —
copies 16 bytes from `BX:DI` into instrument slot `cs:[0x729 + DX*16]`.

### B.6 OPL register write
The DOS `out 0x388,reg; out 0x389,val` with mandatory post-write delays is
replaced entirely by `OPL3_WriteRegBuffered(&chip, reg, val)`; Nuked models the
timing.
