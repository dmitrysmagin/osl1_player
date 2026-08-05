# medplay — OSL1/Adlib player in C + SDL2 + Nuked-OPL3

`medplay.exe` is a standalone Windows console tool that plays Ocean OSL1 songs
(`.ALB` / `.ADL` / `.LAP` / `.SCC`) **and all three pre-OSL1 "old" generations** —
`B4 9A 01` (1991) and `B6 9A 01` (1991–93), both usually `.RLD`, plus the
`20 AD 01` runtime export that always carries `.ALB` (1991–92) — through a
clean-room re-implementation of the DOS `TRACKER.DRV` replay engine and
`ADLIB.DEV` sound backend, synthesised by Nuked-OPL3 and output via SDL2.

> Note that `.ALB` is ambiguous: most `.ALB` files are ordinary OSL1 Adlib
> containers, but 16 of them are the old-format variant above. `medplay`
> dispatches on the magic bytes, never the extension.

Old-format songs are converted to the OSL1 in-memory shape at load time, so a
single unmodified replay engine drives both. There is one byte-level
specification per format:

| Document | Covers |
|---|---|
| **`OSL1.md`** | the `"OSL1"` container — 1992–93, five device targets |
| **`RLD.md`** | pre-OSL1 `B4 9A 01` and `B6 9A 01`, the editor working files |
| **`ALB.md`** | pre-OSL1 `20 AD 01`, the `.ALB` runtime export |
| **`EFFECTS.md`** | all four generations' effect sets and player features compared, and what `replay.c` does and does not cover |

The three old generations are set side by side below, under
[The three pre-OSL1 generations](#the-three-pre-osl1-generations).

> **`EFFECTS.md` (2026-08) supersedes this document on two points.** `OSL1`'s
> effect `0x0E` is **stop channel**, not pattern break — `TRACKER.DRV` `0x1351`
> sets a flag that `0x0FFE` feeds to `stop_channel` (`0x0BFF`) — and `OSL1` has
> no pattern break at all, `0x0D` being a stub in both of its tables. Anywhere
> below that describes `0x0E` as OSL1's pattern break is wrong. `EFFECTS.md` §4
> has the evidence and §7 the resulting gap list.

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
  --fm-source <s>  old .RLD instrument source: auto (default) | block | editor
```

The deterministic `--wav` render plus `--trace` are the primary regression tools:
they let the OPL output be diffed byte-for-byte against a reference capture.

### `--fm-source`

Old `.RLD` files can describe each instrument twice — as a 256-byte OPL2
register block, and as a 64-byte Adlib-editor record in a bank at end of file.
`auto` picks whichever that generation's own DOS loader used (editor for `B4`,
block for `B6`) and falls back to the block wherever the bank is absent or
blank, which covers 90 of the 189 `B4` files. Force one or the other to compare.
The flag has no effect on OSL1 songs, nor on old-format `.ALB` files, which
carry only the editor records and so have nothing to choose between.
`RLD.md` §7.5 has the detail.

---

## The three pre-OSL1 generations

The leading magic byte is a **generation counter, not a device id** — all three
hold OPL2 FM data. `B4` and `B6` are editor working files and share one
specification (`RLD.md`); `20 AD 01` is a runtime export with its own
(`ALB.md`).

| | `B4 9A 01` | `B6 9A 01` | `20 AD 01` |
|---|---|---|---|
| Era | Jan–May 1991 | Aug 1991 – 1993 | Sep 1991 – 1992 |
| Extension | `.RLD` | `.RLD` | `.ALB` |
| Files in corpus | 189 (149 distinct) | 313 (297 distinct) | 22 (16 distinct) |
| Role | editor working file | editor working file | **runtime export** |
| Instrument slots | 32 | 64 | 32 (only the first `n_instr` live) |
| Cue table | absent | 128 × 16 bytes at `+0x158` | `n_cue` × 16 bytes at `+0x158` |
| Paragraph table | `+0x118`, 256 fixed entries | `+0x958`, 256 fixed entries | `+0x158 + 16·n_cue`, `pat+1` entries |
| Pattern stream | `+0x318` | `+0xB58` | first paragraph after the table |
| Cell coding | 2 bits/track, 0/2/2/7 bytes | same | **1 bit/slot, fixed 4 bytes** |
| Row width | `track_count` | `track_count` | `track_count + 5` |
| 256-byte instrument blocks | yes | yes | **none** |
| Adlib editor bank | always present; live in 99 | present in 138, live in 17 | always, `n_instr` records |
| Pattern break effect | `0x0D` | `0x0E` | `0x0D` |
| Loaded by | `BSSJS/ADLIB.DRV` (Apr 1991) | `MED.EXE` / `TRACKER.DRV` (1992–93) | `PIT/ADLIB/ADLIB.EXE` v3.00 (Sep 1991) |

**Each generation has its own dedicated DOS driver, and no driver reads more
than one of them.** All three loaders `rep movsb` a fixed-size header copy out
of the file — `0x118` bytes in `ADLIB.DRV`, `0x158` in `ADLIB.EXE` v3.00 — and
bake in every offset they use afterwards. Neither standalone driver performs a
magic check, and `MED.EXE` has no fallback, so the generations are mutually
unreadable in practice even though they share most of their field layout on
paper.

### Shared by all three

Verified over the whole corpus (149 `B4`, 297 `B6`, 16 `.ALB` distinct files) by
`tools/gen_compare.py`, which reads all three through one code path wherever the
format permits — the places it needs a branch are exactly the places the formats
differ:

* the first `0x98` bytes are the *same structure*: 3-byte magic, `char[20]` name
  at `+0x03`, stray editor scratch byte at `+0x17`, `u8[128]` order table at
  `+0x18`. Order length is derived the same way in all three (last non-zero
  index + 1) and lands in the same range (2–65, 2–65, 2–55);
* the instrument slot table at `+0x98`, `[present][volume]` pairs. The presence
  byte is **only ever 0 or 1** in all three; an absent slot's volume is
  **always 0** in all three; the volume ceiling is `0x40` in all three;
* `restart_idx` immediately after `track_count`, small and always inside the
  order;
* paragraph addressing: `pattern_offset = para_table_off + 16 × table[i]`, with
  entry `pattern_count` marking the end of the pattern stream. Same expression,
  same constant — only the table's base moves;
* 64 rows per pattern, every pattern padded to a 16-byte boundary. Decoding
  every pattern of every distinct file — 207 085 `B4` cells, 455 067 `B6` and
  32 493 `.ALB` — gives **0 boundary misses** in all three;
* the cell fields and their meanings: note, **1-based** instrument selector,
  effect command, effect parameter — and the same MOD-like effect numbering,
  with `0x0C` set-volume dominating everywhere (15% of `B4` cells, 18% of `B6`,
  26% of `.ALB`);
* note numbering. `ADLIB.DRV` and `ADLIB.EXE` v3.00 hold **byte-identical
  96-entry note tables** and both index them with `note − 0x0C`, so note 12 is
  C-1 throughout; the same twelve F-numbers appear again in OSL1's `ADLIB.DEV`;
* **no stored tempo and no stored speed.** All three are 50 Hz, 6 ticks/row by
  default, overridden only by an in-pattern `Fxx`.

### `B4` versus `B6` — the smallest gap

They differ in *one* design decision, 32 instrument slots versus 64, plus one
added feature:

| | `B4` | `B6` |
|---|---|---|
| Slot table size | `0x40` bytes | `0x80` bytes |
| Cue table | absent | 128 × 16 bytes at `+0x158` |
| Everything after `+0x98` | shifted down `0x40` | — |

The `0x800` cue table plus the `0x40` slot shift is the whole `0x840` difference
between `+0x318` and `+0xB58`. The pattern encoding, the 256-byte instrument
blocks, the split-patch layout and the editor bank are all bit-identical, and
`table[0] == 32` in every one of the 446 distinct files. A `B4` reader and a
`B6` reader differ only in two constants — which is why `RLD.md` specifies both.

### `.ALB` versus the other two — a much larger, asymmetric gap

It keeps the header and the paragraph arithmetic and throws away everything the
editor needed:

| | `B4`/`B6` | `.ALB` |
|---|---|---|
| Role | editor working file | runtime export |
| Row encoding | 2-bit code word, 0/2/2/7-byte payloads | 16-bit presence mask, fixed 4-byte cells |
| Row width | `track_count` | `track_count + 5` (percussion) |
| Instrument data | 256-byte blocks **and** a 64-byte editor bank | editor records only |
| Paragraph table | fixed 256 entries, stale tail | `pattern_count + 1`, no stale tail |
| Cue table | fixed 128 entries, 1-based positions | `n_cue` entries, **0-based** |
| Slot table | authoritative | stale past `n_instr` |
| Layout | every offset fixed to `+0x318`/`+0xB58` | everything past `+0x158` is data-dependent |
| Tail slack | 0 or 2048 (the editor bank) | **always 0** — sized exactly |

Curiously `.ALB` takes its 32-slot table from `B4` but its `track_count`
placement at `+0x118` from `B6`, leaving `+0xD8`–`+0x117` as a permanent hole
(zero in all 16 files). It is a hybrid of the two, not a successor to either.

The one deliberate *behavioural* difference is the pattern-break command:
`0x0D` in `B4` and `.ALB`, `0x0E` in `B6`.

### One difference that is not by design

`B4` and `.ALB` produce **no** cell outside the legal note, selector and effect
ranges. `B6` produces a few: **3 of its 297 distinct files** carry roughly 200
cells with notes past 108, selectors past 64 or effect commands past `0x0F` —
`OLDMUSIC/TUNE.RLD` and two variants of `DEMO/MOON.RLD`. They are the oldest and
least tidy specimens in the archive, they still render, and a reader should
**clamp rather than reject**.

---

## 1. Objective & scope

**In scope:** OSL1 and old `.RLD` parsing, the 50 Hz tick engine, the Adlib
(OPL2/OPL3) note/effect path, the Nuked-OPL3 backend, SDL2 audio + timing, a
CLI, and an offline `--wav` render for testing.

**Out of scope (for now):** Roland MT-32 and Sound Blaster SCC1 backends;
GUI/editor; file writing. The replay engine is device-independent, so those are
future backends on the same core.

**Known gap:** the 1991 `ADLIB.DRV` ran the OPL2 in permanent percussion mode
(6 melodic + 5 rhythm voices). `opl_dev.c` is melodic-only, so `B4` percussion
instruments are parsed and reported but voiced as ordinary melodic notes — 72 of
the 189 `B4` files are affected. See `RLD.md` §10. The same gap applies to
the old-format `.ALB` files, whose rows reserve five explicit percussion slots
(`ALB.md` §8); those are voiced melodically too, which is exact for the
bass drum and approximate for the other four.

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
file.adl ─► osl1.c ──┐
file.rld ─► oldrld.c ├─► Song{ instr[], order[], pos_ptr[], raw[] }
file.alb ─► oldalb.c ┘   (B4/B6/.ALB are expanded into the OSL1 shape here;
                          oldfmt.c holds what the two old loaders share)
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

**Instrument record layout** (single-variant FM case, stride `0x3E`): `+0x00`
len, `+0x04` n_variants, `+0x06` desc0, `+0x0A` 20-byte name, `+0x1A` the OSL
device code, **`+0x2E` the 16-byte Adlib patch**. Reading the patch from `+0x1E`
(the earlier guess) loads the wrong bytes — the real Adlib operator data is at
`+0x2E`, confirmed byte-for-byte against the DRO capture.

A record is really a **container of per-device variants**, not a flat struct;
`+0x2E` is the payload of the *FM* (device `0x02`) variant. Device `0x04` is
Roland MT-32 — a 244-byte timbre dump, not OPL2 registers — and such records are
silenced on the OPL2 rather than voiced from their first 11 bytes (the
`JINGLE.RLD` bug). `parse_instr_record()` walks the chain and prefers the FM
variant; see `OSL1.md` §6 and `RLD.md` §7 for the full layout and the
`oldfmt_mt32_sig()` Roland test.

### `oldfmt.c` / `oldrld.c` / `oldalb.c` — the pre-OSL1 loaders

The three old generations split two-to-one. `B4` and `B6` are editor working
files differing only in slot count; `.ALB` is a runtime export with a different
cell encoding, a different row width and no instrument blocks at all. So there
are two loaders, not one, over a shared base:

| File | Handles |
|---|---|
| `oldfmt.c` | Magic detection and dispatch, the header bytes all three agree on, paragraph addressing, the 64-byte Adlib editor record, and the synthetic OSL1-shaped pattern buffer |
| `oldrld.c` | `B4 9A 01` and `B6 9A 01` (`RLD.md`) |
| `oldalb.c` | `20 AD 01` (`ALB.md`) |

`osl1.c` calls `oldfmt_generation()` and routes to `oldrld_load()` or
`oldalb_load()`. Either way the caller gets the same `Song`: every pattern
located through the paragraph table (`para_table_off + para[i] × 16`, uniform
across all three generations), expanded into the uncompressed shape `replay.c`
already understands, plus the `PatternBlock` fields the old format never stored
(tempo 50, speed 6, 64 rows).

The two cell decoders are the clearest statement of why the split exists:

* `decode_old_pattern()` in `oldrld.c` — a `u16` code word, 2 bits per track,
  with variable 0/2/2/7-byte payloads. Bit-for-bit identical to OSL1's
  compressed position stream.
* `decode_alb_pattern()` in `oldalb.c` — a `u16` presence mask, one bit per
  slot, with a fixed 4-byte cell per set bit. Rows carry `track_count + 5`
  slots, the last five being the OPL2 percussion channels (`ALB.md` §7–§8).

Instruments come from either the 256-byte register blocks or the end-of-file
Adlib editor bank, selected by `--fm-source` (§7.5 of `RLD.md`); `.ALB` has
only the editor records, so `oldalb.c` has no such choice to make. The editor
path — shared, in `oldfmt_editor_ops_to_adl()` — inverts the LEVEL and SUSTAIN
fields on the way to the OPL2's attenuation convention, exactly as `ADLIB.DRV`
did.

Old songs also run a slightly different **effect table**, which `replay.c`
selects on `Replay.old_format`: `0x0F` is ProTracker's `Fxx` *set speed* rather
than OSL1's *set tempo*, and the 50 Hz tick is immutable because no era driver
ever reprogrammed the PIT. See `RLD.md` §9.1 — reading `Fxx` as a tempo
used to pin 417 of the 504 `B4`/`B6` files at 19 Hz. Ocean's own driver manual
(`PIT/ADLIB/README.DOC`, 1991) says it in as many words: *"Note driver runs at
50hz."*

One known deviation remains: `B4` and `.ALB` break patterns on `0x0D`, `B6` and
OSL1 on `0x0E`. `replay.c` currently uses `0x0E` for all formats — see
`RLD.md` §9.1 and `ALB.md` §10.3.

Validated across all 524 old-format files in the corpus (189 `B4`, 313 `B6`,
22 `.ALB`): every one loads without error, and both `osl1_dump` and
`decode_dump` produce byte-identical output to the pre-split single-loader
build on every one of them. Every `.ALB` renders with healthy amplitude (peak
4087–25117, no silent file).

The `.ALB` decoder was written from the files alone, before the driver that
plays them was found. It agrees with `ADLIB.EXE` v3.00 exactly — header copy
length, `n_cue` placement, paragraph-table padding, MSB-first presence mask,
fixed 4-byte cell, five trailing percussion slots, and the effect table.
`ALB.md` §6, §7 and §10 quote the relevant code.

That driver is now annotated in full at `../PIT_ADLIB.EXE.annotated.asm`. It
shipped with its Borland debug block attached, so the listing carries the
author's own 245 `ADLIB.ASM` label names and 1185 source-line numbers. Two
corrections to earlier readings came out of it, both recorded in `ALB.md`: the
per-row and per-tick effect tables had been transposed (§10.1), and v3.00 is
**not** `BSSJS/ADLIB.DRV` rebuilt — filtered for non-trivial runs the two share
only 15% of their bytes, essentially just the note table and the OPL2 register
writer, and v3.00 has none of the older driver's 272 branch-padding `nop`s
(§11). Same design, same author, separately written.

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
| `gen_compare.py` | side-by-side `B4`/`B6`/`.ALB` field and cell census — the evidence behind *"The three pre-OSL1 generations"* above |
| `alb_probe.py` | `.ALB` container structure (`ALB.md` §4–§6) |
| `alb_cells.py` | `.ALB` presence-mask cell validation (`ALB.md` §7) |
| `osl1_scan.py` | magic/device census over a corpus |
| `instr_probe.py` | instrument-record correlation between corpora |
| `cmp_pitch.py`, `cmp_pitch_global.py`, `regcmp.py` | register/pitch trace comparison |
| `fxhist.c` | per-generation effect-command and parameter census — the evidence behind `EFFECTS.md`. `#include`s `src/replay.c` so it can sample between `decode_row()` and `trigger_row()`, before the row dispatch consumes `0x06`/`0x0C`/`0x0E` |

## 9. Layout
```
medplay/
├── README.md            this file
├── OSL1.md              OSL1 container specification
├── RLD.md               pre-OSL1 B4 / B6 specification
├── ALB.md               pre-OSL1 20 AD 01 (.ALB) specification
├── EFFECTS.md           effect / player-feature audit across all four generations
├── Makefile
├── src/{main,osl1,replay,opl_dev,opl3}.{c,h}
│   └── {oldfmt,oldrld,oldalb}.{c,h}                 pre-OSL1 loaders
├── tools/{osl1_dump,cell_dump,decode_dump,opl_scale}.c
│         {dro_dump,dro_patches,dro_notes}.py          DRO capture analysis
│         {gen_compare,alb_probe,alb_cells}.py         pre-OSL1 format analysis
│         {osl1_scan,instr_probe,cmp_pitch,regcmp}.py  corpus analysis
│         fxhist.c                                     effect census (EFFECTS.md)
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
