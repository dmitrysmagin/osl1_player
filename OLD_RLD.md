# The Pre-OSL1 "Old" `.RLD` Format

A clean-room description of the **older, pre-`OSL1`** music format written by
Ocean Software's tracker. Files in this format carry the extension `.RLD` (a few
carry none at all) and begin with a 3-byte magic `Bn 9A 01` instead of the ASCII
`"OSL1"`.

This document is the byte-level specification. It was reconstructed by
reverse-engineering two separate loaders — `MED.EXE`'s alternate load path
(`med.asm` ≈ `0x233B`–`0x2717`) and the 1991 standalone driver
`MEDIT/LAPMUSIC/OLDMUSIC/BSSJS/ADLIB.DRV` (fully annotated in
`../BSSJS_ADLIB.DRV.annotated.asm`) — cross-checked against the 500 old-format
files in the corpus and, crucially for the instrument layout, against the
326-file OSL1 corpus, which shares many of the same named instruments. Where a
field's meaning is confirmed it is stated plainly; where it is inferred or
unverified it is marked **(inferred)** or **(unverified)**.

The reference implementation is `src/oldrld.c`. See `OSL1.md` for the newer
container; the two share the pattern *cell* encoding but nothing else.

## Two generations

The first magic byte is a **generation counter, not a device id**. Both
generations hold OPL2 FM data; the `A/` versus `R/` folder convention seen
throughout `OLDMUSIC/` is the orthogonal Adlib/Roland distinction. The evidence
for that — Roland folders full of `B4` files, an Adlib tune that is `B6`, 19
project folders holding both — is set out in `../RE-REPORT.md` §11.1b.

| | `B4 9A 01` | `B6 9A 01` |
|---|---|---|
| Era | Jan–May 1991 | Aug 1991 – 1993 |
| Files in corpus | 189 | 311 |
| Instrument slots | 32 | 64 |
| Cue table | absent | 128 × 16 bytes at `+0x158` |
| Paragraph table | `+0x118` | `+0x958` |
| Pattern stream | `+0x318` | `+0xB58` |
| Adlib editor bank | always present; live in 99 | present in 138, live in 17 |
| Loaded by | `BSSJS/ADLIB.DRV` (1991) | `MED.EXE` (1992–93) |

Everything else — the magic layout, the order table, the slot table, the
paragraph addressing, the pattern encoding and the 256-byte instrument blocks —
is **identical**. The `0x40` shift in every offset after the slot table falls
straight out of 32 slots versus 64.

`MED.EXE` accepts `B6` only: its magic test at `med.asm:0x2364` is a bare
`cmpw $0x9ab6` with no fallback, so the `B3`/`B4`/`B5` files cannot be opened in
the editor at all. `src/oldrld.c` reads `B4` and `B6` — all 500 files load
cleanly — while `B3` (1 file) and `B5` (2 files) remain unhandled for want of
specimens.

> **`.RLD` is a filename convention, not a format marker.** 109 files carrying
> the extension are ordinary OSL1 containers, and two are a plain text file.
> See Appendix B.

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
| Magic | `B4 9A 01` / `B6 9A 01` | `"OSL1"` |
| Song metadata | fixed offsets in a `0x318`- or `0xB58`-byte header | scattered, reached via offset fields |
| Instruments | fixed 256-byte blocks, one per *present* slot, appended after the patterns | variable-size records reached through a pointer table |
| Instrument count | fixed 32 or 64 slots, with a presence flag | `instr_count`, explicit |
| Patterns | fixed 64 rows, always compressed, 16-byte aligned | variable rows, compressed *or* raw |
| Track count | 1 byte at `+0xD8` / `+0x118` (4–8 observed) | `u16` in the pattern block |
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
              B6                        B4
           +0x0000  Magic + song name                        (§4)
           +0x0018  Pattern order table, 128 x u8            (§4)
           +0x0098  Instrument presence/volume table         (§4)
                      B6: 64 x 2 bytes    B4: 32 x 2 bytes
  +0x0118  / +0x00D8  Track count, restart position          (§4)
  +0x0158  /   —      Cue table, 128 x 16 bytes  (B6 only)   (§5)
  +0x0958  / +0x0118  Pattern paragraph table, 256 x u16     (§6.1)
  +0x0B58  / +0x0318  Pattern stream                         (§6)
       ...       ...  Instrument blocks, 256 bytes each      (§7)
       ...       ...  Adlib editor bank, 32 x 64 bytes       (§7.4)
                        B4: all 189       B6: 138 of 311 files
```

Everything up to the pattern stream is at a fixed offset — this format has no
relocatable sections at all. Only the boundary between the pattern stream and
the instrument blocks is data-dependent, and the paragraph table states it
explicitly (§6.1).

---

## 4. File header

The header is identical in both generations up to the instrument slot table;
after it, every B4 offset is `0x40` lower because B4 has 32 slots where B6 has
64.

| B6 offset | B4 offset | Type      | Field          | Notes                                                        |
|-----------|-----------|-----------|----------------|--------------------------------------------------------------|
| +0x00     | +0x00     | u8[3]     | magic          | `B6 9A 01` or `B4 9A 01`. Files not starting with `Bn 9A 01` are not old-format. |
| +0x03     | +0x03     | char[20]  | name           | Song name, NUL-padded. Often empty (135 of 311 B6, 121 of 189 B4). |
| +0x17     | +0x17     | u8        | —              | **Not** part of the name. Non-zero in 51 B6 files, holding a stray ASCII character. Uninitialised editor scratch **(inferred)**. |
| +0x18     | +0x18     | u8[128]   | order          | Pattern order table: one pattern number per position.        |
| +0x98     | +0x98     | u8[N][2]  | instr_slots    | Per-instrument-slot `[present][volume]`. N = 64 (B6) or 32 (B4). See below. |
| +0x118    | +0x0D8    | u8        | track_count    | Voices per row. Only 4, 5, 6, 7, 8 observed; 8 in 180 B6 files. |
| +0x119    | +0x0D9    | u8        | restart_idx    | Order position to loop back to. Zero in 295 of 311 B6 files; small values (1–27) otherwise, always less than the order length. `oldrld.c` parses it into `blk->restart_idx`; the interpretation as a loop target is still **(inferred)** from its range alone. |
| +0x11A    | +0x0DA    | —         | —              | Zero in every corpus file, up to the next structure (`+0x158` in B6, `+0x118` in B4). |

Note the B4 gap is only `0x3E` bytes and runs straight into the paragraph table
at `+0x118` — B4 has no cue table (§5).

### The order table (`+0x18`)

128 bytes, each the number of the pattern to play at that position. There is no
stored length: the song's order length is **the index of the last non-zero byte,
plus one** — position 0 is always played, so a table that is entirely zero still
yields a one-position song.

This does mean a song cannot deliberately end on pattern 0. That is a real
limitation of the format, not of the reader.

### The instrument presence/volume table (`+0x98`)

Two-byte entries, one per instrument slot — 64 entries in B6 (ending at
`+0x118`), 32 in B4 (ending at `+0xD8`):

| Offset | Type | Field   | Notes                                                       |
|--------|------|---------|-------------------------------------------------------------|
| +0x00  | u8   | present | 1 = this slot has an instrument block; 0 = unused. Only ever 0 or 1 (4820 present across the B6 corpus, 15084 unused). |
| +0x01  | u8   | volume  | Slot default volume, `0`–`0x3F`. Maximum in 2579 of the 4820 present B6 slots. |

**This table determines the file's layout**, because §7's instrument blocks are
written only for slots with `present == 1`, in slot order. A slot's index is
still its identity: pattern cells select instruments by slot number, so a gap in
the presence table leaves a permanent hole in the numbering.

#### Default volume

The volume byte is the instrument's **default note-on volume**, and both era
loaders apply it identically: they **double it** into the replay engine's 0–0x7F
range and clamp at `0x7F`.

```
def_volume = min(slot_volume * 2, 0x7F)
```

In `ADLIB.DRV` this is the `shl al,1` at `BSSJS_ADLIB.DRV.annotated.asm:~742`,
immediately before the same code path that an inline `0Ch` effect would take.
A `0Ch` (set volume) riding on the same row therefore **overrides** the default
rather than scaling it.

`src/oldrld.c` parses this into `Instrument.def_volume` and `src/replay.c`
applies it at note-on, preferring an explicit `0Ch` when one is present.

---

## 5. Cue table (`+0x158`) — B6 only

**This structure does not exist in B4**, whose paragraph table follows the header
directly at `+0x118`. It is the single largest structural difference between the
two generations and accounts for `0x800` of the `0x840` shift in the pattern
stream's base offset.

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

Entry 127 is a fixed sentinel across all 309 B6 files — name `"Not Used"` with
`start_pos`/`end_pos` reading as the u16 `0x8000`.

---

## 6. Patterns

Pattern data begins at file offset **`0xB58`** (B6) or **`0x318`** (B4) and runs
until the instrument blocks. Every pattern is **exactly 64 rows** and is
**always compressed**; there is no raw-copy variant as there is in OSL1.

### 6.1 The paragraph table (`+0x958` / `+0x118`)

Up to 256 `u16` entries, filled out with `0xFFFF`. Each is a **16-byte paragraph
offset relative to the paragraph table's own file offset**, so:

```
pattern_file_offset = para_table_off + 16 * table[i]
```

This addressing is **uniform across both generations** — the same expression
with the same constant, differing only in where the table starts. Entry 0 is
`32` in every one of the 500 corpus files, giving `0x958 + 16 × 32 == 0xB58` for
B6 and `0x118 + 16 × 32 == 0x318` for B4, which is how the pattern-stream bases
above are derived rather than assumed.

There are `pattern_count + 1` live entries; **the last marks the end of the
pattern stream, i.e. the start of the instrument blocks**. That entry is what
makes the instrument blocks locatable without decompressing anything.

> **Do not trust this table's length.** It is an editor-side working table that
> is not truncated on save, so entries past the live pattern count are **stale
> leftovers from earlier, longer versions of the song** — they can point beyond
> the end of the file, and they are not even guaranteed to stay monotonic. In
> `LAPMUSIC/LETHAL3/PCTIT.RLD` the 11th entry drops back below the 10th; in
> `AMERICAN/COMPLETE.RLD` (7544 bytes) the tail claims patterns running to
> offset `0x30B8`, nearly 5 KB past the end of the file.
>
> The live prefix *is* reliable. The live pattern count is known independently —
> it is the highest number in the order table, plus one — so index the table
> directly rather than scanning it for a terminator. `src/oldrld.c` now does
> exactly this, taking pattern *i* from `para_table_off + 16 × table[i]` and the
> instrument-block base from entry `pattern_count`. Earlier revisions
> decompressed sequentially from the stream base instead; that yields the same
> boundaries on every corpus file but does not survive a stale or reordered
> table, and it cannot find the instrument blocks without first parsing every
> pattern.
>
> Validated corpus-wide: for all 500 files, `filesize − (blocks_off +
> 256 × present_count)` is exactly 0 or exactly 2048 — never anything else. The
> 2048 is §7.4's Adlib editor bank.

### 6.2 Pattern stream encoding

Each of a pattern's 64 rows is:

```
u16 code_word          ; 2 bits per track, MSB first
<per-track payloads>   ; in track order, only for non-zero codes
```

The code word is **always** a fixed 2-byte read, but only the top
`2 × track_count` bits are examined — the extraction loop is driven by the
`track_count` byte at `+0x118` / `+0xD8` (`med.asm` `0x24CA`), not by a
hardcoded 8. Track `t`'s code is therefore `(code_word >> (14 - 2*t)) & 3`.

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

The cell coding is **bit-for-bit identical in B4, B6 and OSL1** — it is the one
part of the format that never changed across the whole 1991–93 span.

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

This is what makes §6.1's paragraph table expressible in the first place — the
pad exists precisely so that every pattern starts on a paragraph boundary and so
can be named by a 16-byte index.

A reader that indexes through the paragraph table, as `src/oldrld.c` does, never
needs to compute the pad: each pattern's start is stated outright, and the
padding is simply the slack before the next stated start. The rule is documented
here because it explains the table's existence and because a *writer* must
reproduce it.

---

## 7. Instrument blocks

Immediately after the pattern stream come the instrument blocks: **256 bytes
each, one per slot with `present == 1`, in slot order.** There is no pointer
table, no per-block length and no terminator — the count comes from §4's
presence table and the start from §6.1's paragraph table entry `pattern_count`.

**Both generations carry these blocks, in the same layout.** B4 additionally
carries the 32 × 64-byte Adlib editor bank of §7.4 at end of file, and it is the
*bank*, not the block, that `ADLIB.DRV` actually voices from. See §7.5 for which
source to prefer.

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

> **Confirmed from the loader (2026-07).** The name-matching argument above is
> now corroborated directly by `MED.EXE`'s own code. Its old-format instrument
> loader (`med.asm` `0x25DC`–`0x272E`) reads the 256-byte block to offset `0x2A`
> of a freshly allocated record, lifts 10 bytes from block `+0x08` into the
> record's name field, and then at `0x26AF` executes
> `mov di,0x32 / mov si,0x3C / mov cx,0xEC / rep movsb` — **236 bytes moved down
> by 10, closing exactly the gap the name occupied.** It then sets the synth
> code to `0x04` (FM extended) and hands `record+0x2A` to the device driver's
> `D_InstInit`. Since the runtime record is the OSL1 record minus its 4-byte
> length prefix, `record+0x2A` is precisely OSL1 `+0x2E`. The split is therefore
> not an inference at all: the loader performs it explicitly, and MED.EXE's
> handling of these instruments is correct.

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

### 7.4 The Adlib editor bank (end of file)

After the last instrument block, many files carry a further **2048 bytes: 32
records of 64 bytes**, one per Adlib editor slot. This is the bank
`BSSJS/ADLIB.DRV` loads and voices from; it holds the editor's own parameter
fields rather than OPL2 registers, and the driver assembles registers from them
at instrument-init time.

A bank is either **wholly live** (records named, fields populated) or **wholly
blank** (space-filled throughout). There is no partial case in the corpus:

| | present | live | blank | absent |
|---|---:|---:|---:|---:|
| B4 (189 files) | 189 | 99 | 90 | 0 |
| B6 (311 files) | 138 | 17 | 121 | 173 |

A blank bank is space-filled scratch reserved by the editor and never written to
(see §8). Note that **a B4 bank being present does not mean it is usable** — 90
of the 189 B4 files reserve the space without ever opening the Adlib editor, and
a reader must fall back to the 256-byte blocks for those (§7.5).

Blankness is detected by scanning the 32 name fields: if all are space- or
NUL-filled, the bank is blank. Testing the field bytes instead would be wrong,
because a space-filled field area decodes to plausible-looking register values.

#### Record layout (64 bytes)

| Offset | Type     | Field       | Notes                                       |
|--------|----------|-------------|---------------------------------------------|
| +0x00  | char[10] | name        | Instrument name, NUL- or space-padded.      |
| +0x0A  | u8[13]   | mod_fields  | Modulator editor fields — see below.        |
| +0x17  | u8[13]   | car_fields  | Carrier editor fields, same order.          |
| +0x24  | u8       | wave_mod    | Modulator waveform select, 0–3 (`$E0`).     |
| +0x25  | u8       | wave_car    | Carrier waveform select, 0–3 (`$E0`).       |
| +0x26  | u8       | rhythm      | 0 = melodic; 6 = bass drum, 7 = snare, 8 = tom, 9 = cymbal, 10 = hi-hat. **Range-check it** — see §10. |
| +0x27  | —        | —           | Unused tail.                                |

#### The 13 editor fields

The order is fixed by the driver's own field table
(`../BSSJS_ADLIB.DRV.annotated.asm:1493`–`1722`):

| # | Field | Register use |
|--:|-------|--------------|
| 0 | KSL | `$40` bits 6–7 |
| 1 | MULTIPLE | `$20` bits 0–3 |
| 2 | FEEDBACK | `$C0` bits 1–3 (modulator's copy only) |
| 3 | ATTACK | `$60` bits 4–7 |
| 4 | SUSTAIN | `$80` bits 4–7, **inverted** (`15 − v`) |
| 5 | EG-TYPE | `$20` bit 5 |
| 6 | DECAY | `$60` bits 0–3 |
| 7 | RELEASE | `$80` bits 0–3 |
| 8 | LEVEL | `$40` bits 0–5, **inverted** (`63 − v`) and volume-scaled |
| 9 | AM | `$20` bit 7 |
| 10 | VIBRATO | `$20` bit 6 |
| 11 | KSR | `$20` bit 4 |
| 12 | CONNECTION | computed into `$C0` bit 0, then **discarded** by an `and al,0xFE` — the driver forces FM (serial) connection on every channel. |

Sustain and level are stored the way a *musician* reads them (bigger = louder,
bigger = more sustain) and inverted into the OPL2's attenuation convention on
the way out. This is the single most common way to get an FM patch audibly
wrong, and it is why a naive byte-copy of these records produces silence.

Assembling the 11 live `adl[]` bytes from a record:

```
for op in (mod, car):
    adl[o+0] = AM<<7 | VIB<<6 | EGT<<5 | KSR<<4 | (MULT & 15)
    adl[o+1] = (KSL & 3)<<6 | ((63 - LEVEL) & 63)
    adl[o+2] = (ATTACK & 15)<<4 | (DECAY & 15)
    adl[o+3] = ((15 - SUSTAIN) & 15)<<4 | (RELEASE & 15)
    adl[o+4] = waveform & 3
adl[10] = (mod.FEEDBACK & 7) << 1        ; connection bit forced to 0
```

`src/oldrld.c` implements this as `editor_ops_to_adl()`.

### 7.5 Which instrument source to use

A B4 file therefore describes each instrument **twice** — once as a 256-byte
register block (§7.1) and once as a 64-byte editor record (§7.4). They are not
always in agreement; the block is what the *tracker* last wrote, the record what
the *Adlib editor* last wrote.

`src/oldrld.c` exposes the choice as `--fm-source`:

| Value | Behaviour |
|---|---|
| `auto` (default) | `editor` for B4, `block` for B6 — i.e. whatever that generation's own loader read. |
| `block` | Always the 256-byte block's split patch (§7.1). |
| `editor` | Always the 64-byte editor record (§7.4); falls back to the block wherever the bank is absent **or blank**. |

The `auto` mapping is not a guess. `ADLIB.DRV` (the only loader that ever read a
B4) initialises voices from the editor bank, and `MED.EXE` (the only loader that
ever read a B6) initialises them from the block — the latter verified in
`../RE-REPORT.md` §11.5.

In practice the fallback carries most of the load: of the 189 B4 files, 99
resolve to `editor` and 90 to `block`, because their banks are reserved but
blank. Resolving `auto` to `editor` unconditionally on B4 would decode 90 files
from space fill and render them near-silent.

---

## 8. End of file

The instrument blocks are the last structured data proper. The pattern stream
ends exactly at `filesize − 256 × present_count` in files with no trailer —
verified directly on `AMERICAN/COMPLETE.RLD` (7544 bytes, 7 present instruments,
patterns ending at `0x1678`), `OLDMUSIC/INFERNO.RLD` (3864 bytes, 2 instruments,
`0xD18`) and `BSSJS/BSSADLIB.RLD` (18392 bytes, 19 instruments, `0x34D8`).

> **Correction (2026-07).** Earlier revisions of this document described a
> mysterious "trailing block of unused, space-filled (`0x20`) reserved space,
> 1344 to ~1700 bytes across 31 distinct sizes". That was a measurement
> artefact: the trailer is **always exactly 2048 bytes**, and it is §7.4's Adlib
> editor bank. The apparent size variation came from measuring from the end of a
> *sequentially decompressed* pattern stream rather than from the paragraph
> table's stated block base. Reading the bank's true start from the paragraph
> table gives a remainder of exactly 0 or exactly 2048 on all 500 files.

In Roland-targeted B6 tunes the bank is present but blank — every record
space-filled, no names — because the composer never opened the Adlib editor.
That is what made it look like padding. `MED.EXE` closes the file right after
the last present instrument and never reads it, so its absence is equally
expected and must not be treated as a truncation error either.

---

## 9. What the loader supplies

Because the old format stores no timing at all, `MED.EXE`'s loader fills in
fixed values (`med.asm` `0x2374`), and `oldrld.c` mirrors them:

| Field       | Value | Source                                        |
|-------------|-------|-----------------------------------------------|
| tempo       | 50    | Fixed by the loader — 50 Hz timer tick.       |
| speed       | 6     | Fixed by the loader — 6 ticks/row.            |
| row_count   | 64    | Structural (§6).                              |
| restart_idx | file  | Read from `+0x119` / `+0xD9` (§4).            |
| subtitle    | ""    | No such field in this format.                 |

Both era loaders fix tempo and speed the same way; neither format ever stores
them.

A song can still change speed at runtime: the first row of the first pattern
typically carries an explicit `0x0F` (set speed) effect, exactly as an OSL1 song
would. `OLDMUSIC/INFERNO.RLD` opens with `0F 05`.

---

## 10. OPL2 rhythm mode (B4) — parsed but not emulated

`BSSJS/ADLIB.DRV` puts the OPL2 into **percussion mode permanently**: it sets
`$BD` bit 5 during init and never clears it. The chip therefore offers **6
melodic voices plus 5 percussion voices** (bass drum, snare, tom, cymbal,
hi-hat) rather than the usual 9 melodic, for the whole run of the driver.

An instrument's `rhythm` field (§7.4, record `+0x26`) selects which:

| Value | Voice |
|------:|-------|
| 0 | melodic |
| 6 | bass drum |
| 7 | snare drum |
| 8 | tom-tom |
| 9 | cymbal |
| 10 | hi-hat |

The percussion voices share channels 7–9's operators and are keyed on by `$BD`
bits 0–4 rather than by the usual `$B0` key-on, so they cannot be driven through
the melodic path at all.

### The field must be range-checked

**Do not treat "non-zero" as "percussion".** Two distinct hazards:

* **Blank banks.** A blank bank (§7.4) is space-filled, so `+0x26` reads `0x20`,
  not `0`. Reading rhythm from a bank without first confirming it is live yields
  **3392 false percussion instruments across the B6 corpus alone**, plus 480
  more in blank B4 banks.
* **Scratch values in live banks.** Even a live bank carries out-of-range
  values: `0x14`, `0x28`, `0x32`, `0x37`, `0x4B`, `0x55`, `0x5A` across 9 B6
  records and `0x20` across 17 B4 records. These sit on the editor's 0–100
  percentage scale and are leftover scratch, not rhythm codes.

The driver's own dispatch only recognises 6–10. Accept that range and treat
everything else as melodic — which is what `src/oldrld.c` now does. With the
check in place the corpus counts are **72 B4 files and 12 B6 files** containing
at least one percussion instrument; without it, nearly every file appeared to.

### Not emulated

`src/opl_dev.c` is melodic-only, so a B4 percussion instrument is parsed,
reported by `osl1_dump` in the `rhy` column, and then voiced as an ordinary
melodic note on a free channel. It will sound — but not as intended, and a busy
B4 tune will also allocate more melodic channels than the real driver had
available.

`OLDMUSIC/BSSJS/A/BSSADLIB.RLD` is the reference case: 25 present instruments,
of which 3 are percussion (`BDRUM1` rhythm 6, `CYMBAL1` rhythm 9, and one
further).

Emulating it properly means teaching `opl_dev` a rhythm-mode allocation policy,
which is a device-layer change rather than a format one. It is the largest known
gap in B4 playback fidelity.

---

## Appendix A — Reading an old `.RLD` in ten steps

Written for both generations. Where two offsets are given, the first is B6 and
the second B4.

1. Check bytes `+0x00`–`+0x02` for `B6 9A 01` or `B4 9A 01`. Set
   `slot_count` = 64 or 32 and `para_off` = `0x958` or `0x118` accordingly.
2. Read the name from `+0x03` (20 bytes), the order table from `+0x18`
   (128 bytes), and the track count and restart position from
   `+0x118`/`+0x119` or `+0xD8`/`+0xD9`.
3. Order length = index of the last non-zero order byte + 1, minimum 1.
   Pattern count = highest value in that range + 1.
4. Read `slot_count` presence/volume pairs from `+0x98`; note which slots are
   present, and take each default volume as `min(volume × 2, 0x7F)`.
5. Read `pattern_count + 1` `u16` entries from `para_off`. Pattern *i* starts at
   `para_off + 16 × table[i]`; entry `pattern_count` is the end of the pattern
   stream and the start of the instrument blocks. Ignore everything past that
   entry — it is stale (§6.1).
6. For each pattern: 64 rows, each a `u16` code word plus per-track payloads of
   0/2/2/7 bytes for codes 0/1/2/3, examining only the top `2 × track_count`
   bits.
7. Patterns are padded to a 16-byte boundary of their own byte count, but if you
   indexed via step 5 you never need to compute the pad — just seek to the next
   stated start.
8. Read one 256-byte block per present slot, in slot order, from the block base
   found in step 5. Take the patch as `+0x00..+0x07` then `+0x12..+0x19`, and
   the name as `+0x08..+0x11`.
9. If `filesize − (block_base + 256 × present_count)` is 2048, an Adlib editor
   bank follows: 32 × 64-byte records (§7.4). Decode its 13-field operator pairs
   into registers, remembering to invert LEVEL and SUSTAIN.
10. Prefer the editor bank for B4 and the 256-byte blocks for B6 — that is what
    each generation's own loader did (§7.5). Note any non-zero rhythm codes; you
    will need OPL2 percussion mode to voice them correctly (§10).

## Appendix B — Corpus

**500 old-format files**, all under `MEDIT/LAPMUSIC/` (mostly `OLDMUSIC/`, plus
`LETHAL3/`) — 311 `B6` and 189 `B4`. All 500 parse cleanly under
`src/oldrld.c`. Cross-referenced against the 326-file OSL1 corpus documented in
`OSL1.md`.

Files are classified **by magic, not by extension**. Four old-format files carry
no extension at all — `OLDMUSIC/DEMO/LD` (`B4`) and `OLDMUSIC/DG/TOSS` (`B6`),
each appearing twice because `OLDMUSIC/` contains a nested copy of itself. The
`.RLD`-only counts are correspondingly 187 and 309.

Counting the authoritative copy under `MEDIT/` (610 `.RLD` files;
`medplay/test/` is a near-duplicate working set, so counting both double-counts
almost everything):

| Magic | `.RLD` | +extensionless | Notes |
|-------|-------:|---------------:|-------|
| `B3 9A 01` | 1 | 1 | single specimen; unhandled |
| `B4 9A 01` | 187 | 189 | 1991; 32 instrument slots — **this document** |
| `B5 9A 01` | 2 | 2 | transitional; unhandled |
| `B6 9A 01` | 309 | 311 | 1991–93; 64 instrument slots — **this document** |
| `"OSL1"` | 109 | — | not this format at all — ordinary OSL1 containers |
| — | 2 | — | a misnamed text file |

Note the last two rows: `.RLD` is a filename convention covering song files of
every vintage and every device, not a format marker. The 109 OSL1-magic ones
split by device byte as 51 × `0x00`, 46 × `0x04` (Roland), 7 × `0x02` (Adlib) and
5 × `0x08` (SCC1).

`B3` and `B5` remain unhandled for want of specimens — 3 files in total. Their
header offsets are presumably a further variation on the slot-count shift, but
one and two files respectively is not enough to establish a layout with any
confidence.

`MED.EXE` cannot load anything but `B6`. Its magic test at `med.asm:0x2364` is a
single `cmpw $0x9ab6` with no fallback, so 192 files are outside the editor
entirely. The `B4` ones belong to the 1991 standalone driver
`MEDIT/LAPMUSIC/OLDMUSIC/BSSJS/ADLIB.DRV`, annotated in full at
`../BSSJS_ADLIB.DRV.annotated.asm`.

See `../RE-REPORT.md` §11.1b for the evidence that the generation byte tracks
*date* rather than target device, and §11.3 for the editor-record derivation.
