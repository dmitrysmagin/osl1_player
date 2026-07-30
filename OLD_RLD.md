# The Pre-OSL1 "Old" `.RLD` Format

A clean-room description of the **older, pre-`OSL1`** music format written by
Ocean Software's tracker and still loadable by the 1993 `MED.EXE`. Files in this
format carry the extension `.RLD` (a few carry none at all) and begin with the
3-byte magic `B6 9A 01` instead of the ASCII `"OSL1"`.

This document is the byte-level specification. It was reconstructed by
reverse-engineering `MED.EXE`'s alternate load path (`med.asm` ≈ `0x233B`–
`0x2717`), cross-checked against the 311 old-format files in the corpus and —
crucially for the instrument layout — against the 326-file OSL1 corpus, which
shares many of the same named instruments. Where a field's meaning is confirmed
it is stated plainly; where it is inferred or unverified it is marked
**(inferred)** or **(unverified)**.

The reference implementation is `src/oldrld.c`. See `OSL1.md` for the newer
container; the two share the pattern *cell* encoding but nothing else.

---

## 1. Conventions

* **All multi-byte integers are little-endian.**
* Offsets written `+0xNN` are relative to the start of the enclosing structure
  (file, record or block). "File-absolute" means relative to byte 0 of the file.
* Types: `u8`, `u16` = unsigned 8/16-bit; `char[N]` = fixed-width, NUL-padded
  ASCII (not necessarily NUL-terminated when full).
* A **paragraph** is 16 bytes, the DOS segment granularity this format uses for
  all of its pattern-stream arithmetic.
* Sizes and offsets use hexadecimal; counts use decimal.

---

## 2. How this format differs from OSL1

| | Old `.RLD` | OSL1 |
|---|---|---|
| Magic | `B6 9A 01` | `"OSL1"` |
| Song metadata | fixed offsets in a 0xB58-byte header | scattered, reached via offset fields |
| Instruments | fixed 256-byte blocks, one per *present* slot, appended after the patterns | variable-size records reached through a pointer table |
| Instrument count | fixed 64 slots, with a presence flag | `instr_count`, explicit |
| Patterns | fixed 64 rows, always compressed, 16-byte aligned | variable rows, compressed *or* raw |
| Track count | 1 byte at `+0x118` (4–8 observed) | `u16` in the pattern block |
| Tempo / speed | **not stored** — fixed by the loader | `u16`/`u8` in the pattern block |
| Devices | OPL2 only | device-agnostic, five targets |

The one thing they genuinely share is the **2-bit-per-track cell coding** (§6.2),
which is bit-for-bit identical to OSL1's compressed-position bitstream. That is
what makes it practical to decompress old patterns at load time into the shape
OSL1's uncompressed-position path already understands, and play both formats on
one unmodified replay engine — which is exactly what `src/oldrld.c` does.

---

## 3. Top-level layout

```
+0x0000  Magic + song name                             (§4)
+0x0018  Pattern order table, 128 x u8                 (§4)
+0x0098  Instrument presence/volume table, 64 x 2      (§4)
+0x0118  Track count, then reserved space              (§4)
+0x0158  Cue table, 128 x 16 bytes                     (§5)
+0x0958  Pattern paragraph table, up to 256 x u16      (§6.1)
+0x0B58  Pattern stream                                (§6)
  ...    Instrument blocks, 256 bytes each             (§7)
  ...    Optional space-filled trailer                 (§8)
```

Everything up to `+0xB58` is at a fixed offset — this format has no relocatable
sections at all. Only the boundary between the pattern stream and the instrument
blocks is data-dependent, and it falls out of decompressing the patterns.

---

## 4. File header (`+0x00` … `+0x157`)

| Offset | Type      | Field          | Notes                                                        |
|--------|-----------|----------------|--------------------------------------------------------------|
| +0x00  | u8[3]     | magic          | `B6 9A 01`. All 311 corpus files. Files not starting with this are not old-format. |
| +0x03  | char[20]  | name           | Song name, NUL-padded. Often empty (135 of 311).             |
| +0x17  | u8        | —              | **Not** part of the name. Non-zero in 51 files, holding a stray ASCII character. Uninitialised editor scratch **(inferred)**. |
| +0x18  | u8[128]   | order          | Pattern order table: one pattern number per position.        |
| +0x98  | u8[64][2] | instr_slots    | Per-instrument-slot `[present][volume]`. See below.           |
| +0x118 | u8        | track_count    | Voices per row. Only 4, 5, 6, 7, 8 observed; 8 in 180 files. |
| +0x119 | u8        | restart_idx?   | Zero in 295 of 311 files; small values (1–27) otherwise, always less than the order length. Plausibly a loop-restart position, but never validated against playback **(unverified)**. `oldrld.c` ignores it. |
| +0x11A | —         | —              | Zero in every corpus file, right up to `+0x157`.             |

### The order table (`+0x18`)

128 bytes, each the number of the pattern to play at that position. There is no
stored length: the song's order length is **the index of the last non-zero byte,
plus one** — position 0 is always played, so a table that is entirely zero still
yields a one-position song.

This does mean a song cannot deliberately end on pattern 0. That is a real
limitation of the format, not of the reader.

### The instrument presence/volume table (`+0x98`)

64 two-byte entries, one per instrument slot:

| Offset | Type | Field   | Notes                                                       |
|--------|------|---------|-------------------------------------------------------------|
| +0x00  | u8   | present | 1 = this slot has an instrument block; 0 = unused. Only ever 0 or 1 (4820 present across the corpus, 15084 unused). |
| +0x01  | u8   | volume  | Slot volume, `0`–`64`. `64` (maximum) in 2579 of the 4820 present slots. |

**This table determines the file's layout**, because §7's instrument blocks are
written only for slots with `present == 1`, in slot order. A slot's index is
still its identity: pattern cells select instruments by slot number, so a gap in
the presence table leaves a permanent hole in the numbering.

The volume byte is on the editor's 0–64 scale, not the OPL2's 0–63 attenuation
scale, and is *not* currently applied by `oldrld.c` **(unverified)**.

---

## 5. Cue table (`+0x158`)

128 entries of 16 bytes, ending exactly where §6.1's paragraph table begins
(`0x158 + 128 × 16 == 0x958`). Unused entries are named `"Not Used"`.

| Offset | Type     | Field     | Notes                                            |
|--------|----------|-----------|--------------------------------------------------|
| +0x00  | char[10] | name      | Cue name, NUL-padded. `"Not Used"` when free.    |
| +0x0A  | u8       | start_pos | First order position of the cue, 1-based **(inferred)**. |
| +0x0B  | u8       | end_pos   | Last order position of the cue, 1-based **(inferred)**. |
| +0x0C  | u8       | flag_a    | Only 0 or 1 observed. Purpose unknown **(unverified)**. |
| +0x0D  | u8       | flag_b    | Only 1 or 2 observed. Purpose unknown **(unverified)**. |
| +0x0E  | u16      | —         | Zero in every observed entry.                    |

This is the format's **named sound-effect / sub-song list**: a single file packs
many short cues, each naming a span of order positions. `PMONGER/COCKFX.RLD` is
the clearest example, with 52 named entries — `WIND`, `SPRING`, `SUMMER`,
`AUTUMN`, `CARPENTER`, `BLACKSMITH`, `BREATHING1`…`BREATHING6`, `MARCHING1`,
`VILLAGES1`, `INCAMP1`, `INBOATS`, `GETFOOD1`, `DROPEQUIPMENT`, `SHEEP1` — each
covering exactly one position (`start_pos == end_pos == index + 1`).

Most files never use it: 110 have no named entries at all and 158 have exactly
one. `oldrld.c` parses none of it; a player that wanted to expose individual
cues would start here.

Entry 127 is a fixed sentinel across all 311 files — name `"Not Used"` with
`start_pos`/`end_pos` reading as the u16 `0x8000`.

---

## 6. Patterns

Pattern data begins at file offset **`0xB58`** and runs until the instrument
blocks. Every pattern is **exactly 64 rows** and is **always compressed**; there
is no raw-copy variant as there is in OSL1.

### 6.1 The paragraph table (`+0x958`)

Up to 256 `u16` entries, filled out with `0xFFFF`. Each is a **16-byte paragraph
offset relative to `0x958` itself**, so:

```
pattern_file_offset = 0x958 + 16 * table[i]
```

Entry 0 is `32` in every one of the 311 corpus files, and `0x958 + 16 × 32 ==
0xB58` — which is how the pattern-stream base above is derived rather than
assumed. There are `pattern_count + 1` live entries; the last marks the end of
the pattern stream, i.e. the start of the instrument blocks.

> **Do not trust this table's length.** It is an editor-side working table that
> is not truncated on save, so entries past the live pattern count are **stale
> leftovers from earlier, longer versions of the song** — they can point beyond
> the end of the file, and they are not even guaranteed to stay monotonic. In
> `LAPMUSIC/LETHAL3/PCTIT.RLD` the 11th entry drops back below the 10th; in
> `AMERICAN/COMPLETE.RLD` (7544 bytes) the tail claims patterns running to
> offset `0x30B8`, nearly 5 KB past the end of the file.
>
> The live prefix *is* reliable: walk it alongside a sequential decompression
> and stop at the first entry that disagrees. On 210 of 311 files the whole
> table agrees; on the rest it agrees up to the live pattern count and then goes
> stale. `oldrld.c` does not read this table at all — it decompresses
> sequentially from `0xB58`, which yields the same boundaries.

### 6.2 Pattern stream encoding

Each of a pattern's 64 rows is:

```
u16 code_word          ; 2 bits per track, MSB first
<per-track payloads>   ; in track order, only for non-zero codes
```

The code word is **always** a fixed 2-byte read, but only the top
`2 × track_count` bits are examined — the extraction loop is driven by the
`track_count` byte at `+0x118` (`med.asm` `0x24CA`), not by a hardcoded 8. Track
`t`'s code is therefore `(code_word >> (14 - 2*t)) & 3`.

Each code selects how many payload bytes follow and where they land in the
8-byte voice cell the replay engine consumes:

| Code | Bytes | Payload → cell                                                    |
|------|-------|-------------------------------------------------------------------|
| 0    | 0     | Empty cell — nothing playing, nothing changing.                   |
| 1    | 2     | `cell[5]`, `cell[6]` = effect command, effect parameter.          |
| 2    | 2     | `cell[0]`, `cell[4]` = note byte, instrument selector.            |
| 3    | 7     | `cell[0..6]` = 4 note/period bytes, instrument selector, effect command, effect parameter. |

The cell bytes are the replay engine's `voiceblock[1..8]`, so `cell[4]` is the
instrument selector `b[5]`, `cell[5]` the effect command `b[6]`, and `cell[6]`
the effect parameter `b[7]` (see `decode_cell()` in `src/replay.c`).

This is the same coding OSL1 uses, with one simplification: OSL1's code 3 is
modulated by the position header's flag bits `0x1000` and `0x2000`, which can
suppress the period bytes or add an eighth byte. The old format has no flags
word, so code 3 is invariably 7 bytes — equivalent to OSL1 with bit 12 clear and
bit 13 set.

**Instrument selectors are 1-based**, as in OSL1: selector `n` means slot
`n − 1`, and `0` means "no instrument change". (See `OSL1.md` §5 for how badly
this bites if you get it wrong.)

### 6.3 Per-pattern alignment

Every pattern keeps its **own** byte accumulator, reset to 0 at the start of the
pattern (`med.asm` `0x2473`: `movw $0x0,0x40aa` sits immediately before the
64-row loop — it is *not* a running total across the song). After the 64th row,
the read cursor is advanced to the next 16-byte boundary of that accumulator:

```
pad = (16 - (acc & 15)) & 15
```

Skipped when already aligned, and applied after **every** pattern including the
last: the seek lives inside the same outer `loop` iteration that decrements the
pattern counter, with no special case for the final one.

This is what makes §6.1's paragraph table expressible in the first place.

---

## 7. Instrument blocks

Immediately after the pattern stream come the instrument blocks: **256 bytes
each, one per slot with `present == 1`, in slot order.** There is no pointer
table, no per-block length and no terminator — the count comes from §4's
presence table and the start comes from wherever pattern decompression finished.

### 7.1 The OPL2 patch is split around the name

This is the format's one genuine trap. The 16-byte OPL2 patch is *not*
contiguous: the 10-byte name is embedded in the middle of it.

| Offset | Type     | Field      | Notes                                          |
|--------|----------|------------|------------------------------------------------|
| +0x00  | u8[8]    | adl[0..7]  | Modulator `0x20`/`0x40`/`0x60`/`0x80`/`0xE0`, then carrier `0x20`/`0x40`/`0x60`. |
| +0x08  | char[10] | name       | Instrument name, NUL- or space-padded.         |
| +0x12  | u8[8]    | adl[8..15] | Carrier `0x80`/`0xE0`, then `0xC0` feedback/connection. `adl[11..15]` are unused tail. |
| +0x1A  | …        | —          | Editor parameters for MED's other device targets **(inferred)** — see §7.3. |

The 11 live patch bytes are, in order: modulator `0x20`, `0x40`, `0x60`, `0x80`,
`0xE0`; carrier `0x20`, `0x40`, `0x60`, `0x80`, `0xE0`; channel `0xC0`. Identical
in meaning and order to OSL1's `+0x2E` patch — it is literally the same 16-byte
structure with the name re-interleaved after byte 8.

> **How this was established.** Many instruments appear under the same name in
> both corpora, so every old-format block was matched by name against the OSL1
> instrument records. Under the split above, **789 pairs agree on all 11 live
> patch bytes**; a further 178 agree on 10 of 11 and 448 on 9 of 11, the
> residue being genuinely different edits of a same-named patch. Under *any*
> contiguous 11-byte read of the block, **zero** pairs agree.
>
> Restricting to the 963 pairs whose first 8 bytes match exactly and then
> searching the whole 256-byte block for the remaining 3 bytes puts them at
> offset `0x12` in 558 cases, with no other offset scoring above 4.
>
> Worked example: `BSSJS/BSSADLIB.RLD` slot 0 is named `Chorale` and reads
> `02 00 18 32 18 03 01 00` at `+0x00` and `07 02 07` at `+0x12`. The OSL1 file
> `INFERNO/ADLIB/SADFINAL.ADL` carries an instrument also named `Chorale` whose
> `+0x2E` patch is `02 00 18 32 18 03 01 00 07 02 07` — the same eleven bytes.

> **Correction.** `src/oldrld.c` previously read `adl[0..15]` straight from
> `+0x2E`, an offset carried over verbatim from the OSL1 record layout. In a
> 256-byte old-format block that lands in the middle of §7.3's other-device
> parameter area, so **every old-format instrument was voiced with unrelated
> bytes.** Correcting it raised `BSSADLIB.RLD`'s rendered peak amplitude from
> 13172 to 25492 and its RMS from 3161 to 3778 — the carrier levels and
> envelopes had been arbitrary before.

### 7.2 What the block does *not* contain

* **No synth-type code.** The old format predates MED's multi-device support:
  every instrument is an OPL2 patch. Byte `+0x00` is `adl[0]`, the modulator's
  `0x20` register — its frequent value of `0x02` is a coincidence that made it
  look like OSL1's `SYNTH_FM_SHORT` code.
* **No GM program number**, for the same reason.
* **No transpose or finetune field has been located.** OSL1's `+0x22` transpose
  is non-zero in only 82 of the 789 matched reference instruments, and no old
  block offset correlates with it above chance. `oldrld.c` therefore uses zero
  **(unverified)** — worth revisiting, since a missing transpose is exactly the
  fault that made OSL1 instrument 0 play two octaves high (`OSL1.md` §5).

### 7.3 The `+0x1A` tail

The remaining ~230 bytes are unmapped. Their value distributions are strongly
suggestive of the editor's 0–100 percentage scales rather than hardware
registers: `+0x25`–`+0x29` hold `50` in most blocks, `+0x2D` holds `100` in 152
of 789, and runs of `0x64` (100) and `0x32` (50) recur throughout `+0x60`
onward. The best reading is that this is per-device editor state for targets
this format never actually drove **(inferred)**.

`oldrld.c` ignores all of it.

---

## 8. End of file

The instrument blocks are the last structured data. The pattern stream ends
exactly at `filesize − 256 × present_count` in files with no trailer — verified
directly on `AMERICAN/COMPLETE.RLD` (7544 bytes, 7 present instruments, patterns
ending at `0x1678`), `OLDMUSIC/INFERNO.RLD` (3864 bytes, 2 instruments, `0xD18`)
and `BSSJS/BSSADLIB.RLD` (18392 bytes, 19 instruments, `0x34D8`).

Many files, however, carry a **trailing block of unused, space-filled (`0x20`)
reserved space** after the last real instrument — 1344 to ~1700 bytes in the
files that have one, across 31 distinct sizes. `FLOOR.RLD`, `DEMO/NOREWARD.RLD`,
`DEMO/RTS.RLD` and `HUMANS/LEVEL1.RLD` are examples. `MED.EXE` closes the file
right after the last present instrument and never reads the trailer, so this is
expected and must not be treated as a truncation error.

---

## 9. What the loader supplies

Because the old format stores no timing at all, `MED.EXE`'s loader fills in
fixed values (`med.asm` `0x2374`), and `oldrld.c` mirrors them:

| Field       | Value | Source                                        |
|-------------|-------|-----------------------------------------------|
| tempo       | 50    | Fixed by the loader — 50 Hz timer tick.       |
| speed       | 6     | Fixed by the loader — 6 ticks/row.            |
| row_count   | 64    | Structural (§6).                              |
| restart_idx | 0     | `+0x119` ignored **(unverified)**, see §4.    |
| subtitle    | ""    | No such field in this format.                 |

A song can still change speed at runtime: the first row of the first pattern
typically carries an explicit `0x0F` (set speed) effect, exactly as an OSL1 song
would. `OLDMUSIC/INFERNO.RLD` opens with `0F 05`.

---

## Appendix A — Reading an old `.RLD` in ten steps

1. Check bytes `+0x00`–`+0x02` for `B6 9A 01`.
2. Read the name from `+0x03` (20 bytes), the order table from `+0x18`
   (128 bytes) and the track count from `+0x118`.
3. Order length = index of the last non-zero order byte + 1, minimum 1.
4. Read the 64 presence/volume pairs from `+0x98`; note which slots are present.
5. Seek to `0xB58`.
6. For each pattern: 64 rows, each a `u16` code word plus per-track payloads of
   0/2/2/7 bytes for codes 0/1/2/3, examining only the top `2 × track_count`
   bits.
7. After each pattern, advance to the next 16-byte boundary of that pattern's
   own byte count.
8. Stop when the pattern count matches the highest number in the order table
   (+1), or cross-check against the paragraph table at `+0x958` — remembering
   that its tail may be stale (§6.1).
9. Read one 256-byte block per present slot, in slot order, from wherever step 7
   left the cursor. Take the patch as `+0x00..+0x07` then `+0x12..+0x19`, and
   the name as `+0x08..+0x11`.
10. Ignore anything after the last block; it is space-filled padding.

## Appendix B — Corpus

311 old-format files, all under `MEDIT/LAPMUSIC/` (mostly `OLDMUSIC/`, plus
`LETHAL3/`). All 311 parse cleanly under `src/oldrld.c`. Cross-referenced
against the 326-file OSL1 corpus documented in `OSL1.md`.
