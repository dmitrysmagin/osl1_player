# The Pre-OSL1 "Old" `.RLD` Format

A clean-room description of the **older, pre-`OSL1`** music format written by
Ocean Software's tracker. Files in this format usually carry the extension
`.RLD` (a few carry none at all, and one variant uses `.ALB`) and begin with a
3-byte magic `Bn 9A 01` or `20 AD 01` instead of the ASCII `"OSL1"`.

This document is the byte-level specification. It was reconstructed by
reverse-engineering two separate loaders — `MED.EXE`'s alternate load path
(`med.asm` ≈ `0x233B`–`0x2717`) and the 1991 standalone driver
`MEDIT/LAPMUSIC/OLDMUSIC/BSSJS/ADLIB.DRV` (fully annotated in
`../BSSJS_ADLIB.DRV.annotated.asm`) — cross-checked against the 500 old-format
files in the corpus and, crucially for the instrument layout, against the
326-file OSL1 corpus, which shares many of the same named instruments. The
`.ALB` variant of §11 has no surviving loader and was reversed from the files
alone. Where a field's meaning is confirmed it is stated plainly; where it is
inferred or unverified it is marked **(inferred)** or **(unverified)**.

The reference implementation is `src/oldrld.c`. See `OSL1.md` for the newer
container; `B4`/`B6` share the pattern *cell* encoding with it but nothing else,
and `.ALB` shares not even that.

## Three generations

The first magic byte is a **generation counter, not a device id**. All three
generations hold OPL2 FM data; the `A/` versus `R/` folder convention seen
throughout `OLDMUSIC/` is the orthogonal Adlib/Roland distinction. The evidence
for that — Roland folders full of `B4` files, an Adlib tune that is `B6`, 19
project folders holding both — is set out in `../RE-REPORT.md` §11.1b.

| | `B4 9A 01` | `B6 9A 01` | `20 AD 01` (§11) |
|---|---|---|---|
| Era | Jan–May 1991 | Aug 1991 – 1993 | Sep 1991 – 1992 |
| Extension | `.RLD` | `.RLD` | `.ALB` |
| Files in corpus | 189 (149 distinct) | 311 (297 distinct) | 16 distinct |
| Role | editor working file | editor working file | **runtime export** |
| Instrument slots | 32 | 64 | 32 (only the first `n_instr` live) |
| Cue table | absent | 128 × 16 bytes at `+0x158` | `n_cue` × 16 bytes at `+0x158` |
| Paragraph table | `+0x118`, 256 fixed entries | `+0x958`, 256 fixed entries | `+0x158 + 16·n_cue`, `pat+1` entries |
| Pattern stream | `+0x318` | `+0xB58` | first paragraph after the table |
| Cell coding | 2 bits/track, 0/2/2/7 bytes | same | **1 bit/slot, fixed 4 bytes** |
| Row width | `track_count` | `track_count` | `track_count + 5` (§11.4) |
| 256-byte instrument blocks | yes | yes | **none** |
| Adlib editor bank | always present; live in 99 | present in 138, live in 17 | always, `n_instr` records |
| Pattern break effect | `0x0D` | `0x0E` (see §9.1) | `0x0D` |
| Loaded by | `BSSJS/ADLIB.DRV` (Apr 1991) | `MED.EXE` / `TRACKER.DRV` (1992–93) | `PIT/ADLIB/ADLIB.EXE` v3.00 (Sep 1991) |

**Each generation has its own dedicated driver, and no driver reads more than
one of them.** All three loaders begin by `rep movsb`-ing a fixed-size header
copy out of the loaded file — `0x118` bytes in `ADLIB.DRV`, `0x158` in
`ADLIB.EXE` v3.00 — and every offset they use afterwards is baked in. There is
no magic check anywhere in the two standalone drivers (§11.6) and no fallback in
`MED.EXE`, so the generations are mutually unreadable in practice even though
they share most of their field layout on paper.

`B4` and `B6` differ *only* in the slot count and the offsets that shift as a
result — the magic layout, the order table, the slot table, the paragraph
addressing, the pattern encoding and the 256-byte instrument blocks are
**identical**, and the `0x40` shift in every offset after the slot table falls
straight out of 32 slots versus 64. Sections 1–10 describe those two.

`.ALB` reuses the header and the paragraph-addressing scheme but replaces the
pattern encoding and drops the instrument blocks entirely. **Section 11 covers
it in full**; the earlier sections apply to it only where §11 says so.

`MED.EXE` accepts `B6` only: its magic test at `med.asm:0x2364` is a bare
`cmpw $0x9ab6` with no fallback, so the `B3`/`B4`/`B5` and `.ALB` files cannot
be opened in the editor at all. `src/oldrld.c` reads `B4`, `B6` and `.ALB` — all
514 files load cleanly — while `B3` (1 file) and `B5` (2 files) remain unhandled
for want of specimens.

### How alike are they, exactly?

**Shared by all three**, verified over the whole corpus (149 `B4`, 297 `B6`,
16 `.ALB` distinct files):

* the first `0x98` bytes are the *same structure*: 3-byte magic, `char[20]`
  name at `+0x03`, stray editor scratch byte at `+0x17`, `u8[128]` order table
  at `+0x18`. Order length is derived the same way in all three (last non-zero
  index + 1), and lands in the same range (2–65, 2–65, 2–55);
* the instrument slot table at `+0x98`, `[present][volume]` pairs. The presence
  byte is **only ever 0 or 1** in all three; an absent slot's volume is
  **always 0** in all three; the volume maximum is `0x40` in all three;
* `restart_idx` immediately after `track_count`, small and always inside the
  order;
* paragraph addressing: `pattern_offset = para_table_off + 16 × table[i]`, with
  entry `pattern_count` marking the end of the pattern stream. Same expression,
  same constant, only the table's base moves;
* 64 rows per pattern, every pattern padded to a 16-byte boundary;
* the cell fields and their meanings: note, **1-based** instrument selector,
  effect command, effect parameter — and the same MOD-like effect numbering,
  with `0x0C` set-volume dominating everywhere (15% of `B4` cells, 18% of `B6`,
  26% of `.ALB`);
* note numbering. `ADLIB.DRV` and `ADLIB.EXE` v3.00 hold **byte-identical
  96-entry note tables** and both index them with `note − 0x0C`, so note 12 is
  C-1 throughout; the same twelve F-numbers appear again in OSL1's `ADLIB.DEV`;
* **no stored tempo and no stored speed.** All three are 50 Hz, 6 ticks/row by
  default, overridden only by an in-pattern `Fxx` (§9.1).

**`B4` versus `B6`** — the smallest gap of the three. They differ in *one*
design decision, 32 instrument slots versus 64, plus one added feature:

| | `B4` | `B6` |
|---|---|---|
| Slot table size | `0x40` bytes | `0x80` bytes |
| Cue table | absent | 128 × 16 bytes at `+0x158` |
| Everything after `+0x98` | shifted down `0x40` | — |

The `0x800` cue table plus the `0x40` slot shift is the whole `0x840` difference
between `+0x318` and `+0xB58`. The pattern encoding, the 256-byte instrument
blocks, the split-patch layout, the editor bank — all bit-identical. A `B4`
reader and a `B6` reader differ only in two constants.

**`.ALB` versus the other two** — a much larger gap, and asymmetric. It keeps
the header and the paragraph arithmetic and throws away everything the editor
needed:

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

Curiously `.ALB` takes its 32-slot table from `B4` but its `track_count`
placement at `+0x118` from `B6`, leaving `+0xD8`–`+0x117` as a permanent hole
(zero in all 16 files). It is a hybrid of the two, not a successor to either.

The one deliberate *behavioural* difference is the pattern-break command:
`0x0D` in `B4` and `.ALB`, `0x0E` in `B6`. See §9.1.

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

`.ALB` (§11) does **not** share it. Its rows carry a presence *mask* and fixed
4-byte cells rather than a 2-bit code word and variable payloads, so it needs its
own decoder — but the decoder still writes into the same 8-byte cell shape, so
the replay engine remains untouched for it too.

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

`.ALB` breaks that property: both its cue table and its paragraph table are
sized from header counts, so everything from `+0x158` onward moves. See §11.2.

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
| +0x01  | u8   | volume  | Slot default volume, `0`–`0x40` (0–64 decimal). Maximum in 2579 of the 4820 present B6 slots, and likewise the commonest value in B4 and `.ALB`. |

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

The stored scale is **0–64**, not 0–63: `0x40` is the observed maximum in all
three generations and it is a common value, not an outlier. The doubling
therefore reaches `0x80`, which is exactly why the clamp to `0x7F` exists.

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

`.ALB` carries the same 16-byte entry layout but a **variable** number of them,
and its positions are **0-based** where B6's appear to be 1-based. See §11.2.

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
>
> **`.ALB` has no stale entries.** Its table holds exactly `pattern_count + 1`
> entries, padded to a paragraph and nothing more — verified on all 14 files
> (§11.2). The warning above applies to `B4`/`B6` only.

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

### 9.1 The 50 Hz tick is immutable — and `Fxx` is *speed*, not tempo

**Nothing in an old-format song can change the tick rate.** `ADLIB.DRV` programs
the PIT for 50 Hz during init and never touches it again, and `MED.EXE` fixes
the same 50 at load. There is no stored tempo field and no effect that writes
one. Tempo is 50 Hz for the entire run, in all three generations.

> Ocean's own driver documentation says so outright. `ADLIB.EXE` v3.00's manual
> (`.../PIT/ADLIB/README.DOC`, September 1991 — see §11.6) states, under the
> initialisation instructions: **"Note driver runs at 50hz."** The binary agrees
> — it programs the PIT with the literal constant `0x5D37`, i.e.
> 1 193 182 / 23863 = 50.0016 Hz, and nothing else in the driver writes the
> timer. The 50 in `oldrld.c` is not a fallback; it is the specification.

What a song *can* change is the **speed** — ticks per row — and it does so
through `0x0F`. This is the one effect whose meaning differs between the old
format and OSL1, and getting it wrong is expensive:

| Effect | Old format (`ADLIB.DRV`) | OSL1 (`TRACKER.DRV`) |
|---|---|---|
| `0x09` | stub — no-op | set speed, `param & 0x1F` |
| `0x0F` | **set speed**, `param & 0x1F` | **set tempo**, PIT Hz, clamped `≥ 0x13` |

The old handler is `fx_set_speed` @`0x06CB` in
`../BSSJS_ADLIB.DRV.annotated.asm`: `and al,0x1F` / `jz` (so `F00` is a no-op,
not a stop) / clear `tick_in_row` / store `speed`. That is byte-for-byte the
same routine as OSL1's `0x09` handler @`0x1361`. Note the mask means there is
**no ProTracker `Fxx ≥ 0x20` BPM split** — `F20` sets speed 0, i.e. does
nothing.

The corpus settles it beyond doubt. Across the 459 distinct old-format songs,
every one of the 9696 `Fxx` parameters bar five strays falls in `1`–`0x20`, and
they cluster on exactly the values a tick count would:

| Param | 4 | 5 | 6 | 8 | 9 | others |
|---|---:|---:|---:|---:|---:|---:|
| B6 (8614 total) | 828 | 741 | **3168** | 437 | 2694 | 746 |
| B4 (791) | 23 | 78 | **293** | 127 | 10 | 260 |
| `.ALB` (291) | 17 | 1 | 41 | **97** | 27 | 108 |

The mode is 6 — the format's own default speed. A tempo in Hz would cluster near
50, and a ProTracker BPM near 125; neither appears. Conversely `0x09`, OSL1's
set-speed, is effectively absent from old files: 4 occurrences in all of B6 and
none at all in B4 or `.ALB`, exactly as a stubbed handler predicts.

> **Correction (2026-07).** `src/replay.c` originally ran one effect table for
> both formats, so an old-format `Fxx` reached OSL1's set-*tempo* handler. Its
> `if (t < 19) t = 19` clamp then pinned the timer at 19 Hz — **38% of the
> correct rate** — and the speed change the song actually asked for never
> happened. It affected **170 of 190 B4 files and 247 of 314 B6 files**: every
> old song carrying an `Fxx`, which is most of them. `replay.c` now switches on
> `Replay.old_format` and routes `0x0F` to the shared `set_speed()` helper for
> pre-OSL1 songs. After the fix no old-format file moves off 50 Hz, and 112 B4,
> 209 B6 and 17 `.ALB` files change speed where none did before. OSL1 playback
> is bit-identical either way.

`OLDMUSIC/INFERNO.RLD` is the reference case: it opens with `0F 05`, which now
correctly means "five ticks per row" (tempo 50, speed 6 → 5) rather than
"19 Hz".

> **`0x0D` versus `0x0E` for pattern break — settled for `B4` and `.ALB`,
> still open for `B6`.** The same effect table shows `ADLIB.DRV` handling
> **`0x0D`** as pattern break with `0x0E` stubbed, whereas OSL1 — and therefore
> `replay.c` — uses `0x0E`.
>
> The `.ALB` driver found in 2026-07 (§11.6) agrees with `ADLIB.DRV`: its
> per-tick dispatch table at body offset `0x1347` sends `0x0D` to a real handler
> at `0x073B` and `0x0E` to the bare `ret` at `0x0647`. So **two of the three
> generations demonstrably break on `0x0D`.** The corpus agrees: `0x0D` appears
> 557 times in B4 and in 8 of the 16 `.ALB` files, `0x0E` once in B4 and in only
> 2 `.ALB` files. `CRUSADE/CRUSFX.ALB` is the clincher — a sound-effect bank of
> 25 one-position cues carrying exactly 25 `0x0D`s, one to terminate each.
>
> `B6` remains genuinely ambiguous. It was played through `TRACKER.DRV`, which
> is the OSL1 engine, and it is the one generation where `0x0E` is common (482
> uses against 1312 of `0x0D`). No `B6`-era driver has been found that stubs
> `0x0E`. `replay.c` still routes `0x0E` for all formats; fixing it properly
> means splitting the behaviour by generation rather than by `old_format`, which
> is a change worth making deliberately rather than as a side effect.

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

## 11. The `.ALB` variant (`20 AD 01`)

A third old-format generation, distinguished by the magic `20 AD 01` and always
carrying the extension `.ALB`. Sixteen distinct songs survive, across five
projects dated September 1991 to 1992 — `PIT`, `LETHAL3`, `CHAOS`, `CRUSADE` and
`UTOPIA`.

> **Everything below is now confirmed against the driver that plays it.**
> `MEDIT/LAPMUSIC/OLDMUSIC/OLDMUSIC/PIT/ADLIB/` holds `ADLIB.EXE` — "ADLIB
> DRIVER (Version 3.00)", Imagitec Design Ltd., September 1991 — together with
> its API documentation (`README.DOC`), two `.ALB` files it plays, and the
> MASM source of the harnesses that load them (`../MUSIC.ASM`, `../FX.ASM`).
> This section was originally reversed from the files alone; the driver has
> since corroborated the container arithmetic (§11.2), the presence-mask row
> encoding (§11.3), the five percussion slots (§11.4) and the effect table
> (§11.6), in every case exactly. Citations below are offsets into the driver
> body, which begins at file offset `0x200` (`ADLIB.EXE` is a flat binary behind
> a 32-paragraph MZ stub — `MUSIC.ASM` loads it and calls `segment:0x200`).

It is **not an editor working file**. Every structure that exists purely to
serve the editor is either dropped or trimmed to fit:

* the 256-byte instrument blocks are **gone entirely** — only the compact
  64-byte Adlib editor records survive, and there are exactly as many of them as
  the song uses;
* the cue table is cut from a fixed 128 entries to however many the song has;
* the paragraph table is cut from a fixed 256 entries to `pattern_count + 1`;
* the pattern encoding is replaced with one that is simpler to decode and
  slightly larger on disc.

Taken together that reads as a **runtime export**: a "save for the game" pass
that discards editor state, keeps only what a player needs, and trades a little
size for a decoder with no variable-length payloads in it. There is no surviving
loader for it in the corpus — see §11.6.

`src/oldrld.c` reads it; `oldrld_generation()` returns `OLDRLD_GEN_ALB` (`0x20`,
the literal first byte, for the same reason `0xB4`/`0xB6` are used).

### 11.1 Provenance and the corpus

| Project | Files | Date |
|---|---|---|
| `LAPMUSIC/OLDMUSIC/PIT/ADLIB` | `ADLIB`, `ADLIBFX` | Nov 1991 |
| `LAPMUSIC/LETHAL3` | `ING4`, `PCGO`, `PCMOD` | 1992 |
| `LAPMUSIC/OLDMUSIC/CHAOS` | `ADLIB2` | 1992 |
| `LAPMUSIC/OLDMUSIC/CRUSADE` | `CRUSFX`, `DEATH`, `ING`, `MEDAL`, `TITLE` | 1992 |
| `LAPMUSIC/OLDMUSIC/UTOPIA` | `ING1`, `ING2`, `ING3`, `TITLE`, `UTOFX` | 1992 |

**16 distinct files by content.** `OLDMUSIC/` contains a nested copy of itself
and `medplay/test/` is a working duplicate, so the path count is far higher; all
counts below are over the 16.

> **Corrected 2026-07.** An earlier revision said 14 files across four 1992
> projects. It missed `PIT/ADLIB/`, which lives only in the *nested*
> `OLDMUSIC/OLDMUSIC/` copy and was never mirrored into `medplay/test/`. The
> two files there are the oldest `.ALB` in the corpus (6 Nov 1991) and they ship
> alongside the driver that plays them, which is why the omission mattered: the
> "no surviving loader" claim in §11.6 was wrong. Both decode cleanly under the
> model of §11.3 — 0 boundary misses, 0 out-of-range cells — which makes them a
> genuine hold-out set, since the model was derived without them.

Three of the titles at `+0x03` name a **`.MOD` source file** — `pcing4.mod`,
`pcgo.mod`, `pcmod.mod` — which places the variant alongside the Amiga ports and
is further evidence that these are exported deliverables rather than
compositions in progress.

### 11.2 Header and container

Identical to `B4` up to `+0x118`, then it diverges. The instrument slot table is
**32 entries** as in `B4`, so it ends at `+0xD8`; but unlike `B4` the track count
does *not* follow immediately — it sits at `+0x118`, at the `B6` offset, leaving
`+0xD8`–`+0x117` as a permanently zero gap (0 of 14 files have a non-zero byte
in it).

| Offset | Type     | Field       | Notes                                                     |
|--------|----------|-------------|-----------------------------------------------------------|
| +0x000 | u8[3]    | magic       | `20 AD 01`.                                               |
| +0x003 | char[20] | name        | Song name, NUL-padded. Empty in 6 of 14.                  |
| +0x018 | u8[128]  | order       | As §4. Order length 2–55 observed.                        |
| +0x098 | u8[32][2]| instr_slots | As §4, 32 slots. **Only the first `n_instr` entries are meaningful** — see below. |
| +0x0D8 | —        | —           | Zero in every file, to `+0x117`.                          |
| +0x118 | u8       | track_count | 4, 5 or 6 observed. Excludes the percussion slots (§11.4). |
| +0x119 | u8       | restart_idx | As §4.                                                    |
| +0x11A | u8       | n_instr     | Number of 64-byte editor records at end of file. 2–24 observed. |
| +0x11B | u8       | n_cue       | Entries in the cue table, **including its `"Not Used"` sentinel**. 1, 2, 11 or 25 observed. |
| +0x11C | —        | —           | Zero in every file, to `+0x157`.                          |
| +0x158 | ×`n_cue` | cue_table   | 16 bytes each, layout exactly as §5.                      |

Then, in order:

```
para = 0x158 + 16 * n_cue          paragraph table, pattern_count + 1 u16s
                                     padded up to a 16-byte boundary
pattern i at para + 16 * table[i]  patterns (§11.3)
para + 16 * table[pattern_count]   n_instr x 64-byte editor records, to EOF
```

Three properties were checked on all 14 files and hold exactly:

* `table[0] == ceil((pattern_count + 1) * 2 / 16)` — i.e. the pattern stream
  starts at the first paragraph boundary past the end of the table itself, with
  no fixed 256-entry reservation and therefore **no stale tail** (§6.1);
* every pattern decodes to precisely the byte range the table delimits;
* `filesize − (records_off + 64 × n_instr) == 0` — **zero slack**, so `n_instr`
  is corroborated independently by the file size.

> **Confirmed from the driver.** `ADLIB.EXE` v3.00's "initialise tune data" call
> (`INT 60h`, `AX = 2`; body offset `0x00E9`, reached through the dispatch table
> at `0x008E`) performs precisely this arithmetic, with `DS` set to the music
> segment by the caller:
>
> ```
> 00F6  mov di,0xcbf / mov cx,0x158 / rep movsb   ; header copy is 0x158 bytes
> 010B  mov cl,[0xdda]                            ; 0xCBF+0x11B = n_cue
> 0111  shl cx,1 x4 / add si,cx                   ; si = 0x158 + 16 * n_cue
> 011B  mov cx,[0x15a4] / shl cx,1                ; (pattern_count) * 2
> 0121  and cx,-0x10 / add cx,0x10                ;   rounded up to a paragraph
> 0128  mov di,0xe47 / rep movsb                  ; copy the paragraph table
> 012D  mov [es:0x15a6],si                        ; si now = pattern stream base
> ```
>
> Three things fall out of this. The header copy is `0x158` bytes, i.e. exactly
> up to the cue table, versus `0x118` in the 1991 `ADLIB.DRV` — the two drivers
> disagree on the header length by the same amount the two formats do. `n_cue`
> is read from `+0x11B` and multiplied by 16, fixing both the field and the
> entry size. And the table copy length is `((pattern_count × 2) & ~15) + 16`,
> which is the paragraph-padded size of `pattern_count + 1` `u16` entries —
> so the driver takes the **end of the padded table** as the pattern-stream
> base and never consults `table[0]` for it. That is a stronger statement than
> the `table[0] == ceil((pat+1)·2/16)` identity above: the format cannot carry
> a stale tail, because the driver would read the tail as pattern data.
>
> `track_count` is read at `[es:0xdd7]` = `0xCBF + 0x118` (body `0x055F`),
> confirming the odd `B6`-style placement despite the `B4`-style 32-slot table.

#### The cue table is 0-based here

Same 16-byte entry layout as §5, but `start_pos`/`end_pos` are **0-based** and
the last entry is always the `"Not Used"` sentinel, so the live cue count is
`n_cue − 1`. Twelve of the 14 files have exactly one live cue spanning the whole
song (`start_pos == 0`, `end_pos == order_length − 1`) — the song's own name.
The two exceptions are the sound-effect banks, one cue per order position:

* `CRUSADE/CRUSFX.ALB` — 24 cues over 25 patterns: `ARMOUR`, `THUD`, `PSYCHIK`,
  `FUZZ`, `ALARM`, `BUTTON`, `FOOTSTEP`, `DOOR`, `PICKUP`, `GRENADE`,
  `EXPLOSION`, `CHAOS`, `LAZER`, `MISSILE`, `SCANNER`, `BLIP`, `RICOCHET`,
  `FIRE`, `COMMANDER` …
* `UTOPIA/UTOFX.ALB` — 10 cues, `LANDEXP`, `MISSILE` …

That is the same role the B6 cue table plays in `PMONGER/COCKFX.RLD` (§5), which
is a useful cross-check that the field means what §5 says it means.

#### The slot table is stale past `n_instr`

`present_count` and `n_instr` disagree in 11 of the 14 files, and in both
directions — `UTOPIA/TITLE.ALB` declares 4 records while leaving `present = 1`
in all 32 slots, and `UTOPIA/ING3.ALB` declares 24 records with only 11 slots
flagged present. The export evidently rewrites the counts without clearing the
table it inherited.

The rule that works: **take `n_instr` as the record count, and read the presence
flag and default volume only for slots below it.** Within that range the flag is
reliable — across all 14 files, no pattern cell ever selects a slot whose
presence flag is 0.

Do **not** substitute "has a blank name" for the presence flag, tempting as it
is by analogy with §7.4's blank-bank test. Three files (`LETHAL3/ING4.ALB` slots
6 and 9, `CRUSADE/ING.ALB` slot 2) actively play instruments whose name field is
empty — the composer simply never named them.

### 11.3 Pattern encoding — a presence mask, not a code word

Every pattern is 64 rows and paragraph-padded exactly as in §6.3. The row
encoding is where `.ALB` parts company with everything else in this document:

```
u16 mask                ; one bit per channel slot, MSB first: bit 15 = slot 0
<4-byte cell> x popcount(mask)   ; in ascending slot order
```

There are no variable payloads and no per-track code values. A row is always
`2 + 4 × popcount(mask)` bytes, and a slot is either silent (bit clear, nothing
stored) or carries a full cell.

| Offset | Field      | Notes                                                    |
|--------|------------|----------------------------------------------------------|
| +0x00  | note       | 0 = no note; otherwise 17–96 observed. Same numbering as `B4`/`B6`. |
| +0x01  | instrument | **1-based** selector, as everywhere else in the family; 0 = no change. |
| +0x02  | effect_cmd | 0–15, the same MOD-like set as `B4`/`B6` — no remapping. |
| +0x03  | effect_par | Effect parameter.                                        |

These map onto the replay engine's 8-byte cell as `cell[0]`, `cell[4]`,
`cell[5]`, `cell[6]` — the same four slots §6.2's code 2 and code 3 write, which
is why the same replay engine plays both without modification.

> **How the encoding was established.** Length validation alone cannot
> distinguish this model from a 2-bit-per-track one, because under every
> plausible pairing the row length comes out as `4 × popcount`. What settled it
> was profiling the payload bytes: under the 2-bit reading, "code 3" payloads
> showed the note and instrument fields *duplicated* in both halves — the
> signature of two independent cells being read as one. One bit per slot, four
> bytes per cell, is the reading under which every byte has exactly one meaning.
>
> **Confirmed from the driver (2026-07).** The statistical argument above is
> now redundant: `ADLIB.EXE` v3.00's row reader is explicit about it. At body
> offset `0x054C` it does a single `lodsw` to take the mask into `DX`, decodes
> `track_count` melodic slots, then five more:
>
> ```
> 054C  lodsw / mov dx,ax        ; DX = the row's presence mask
> 055A  call 0x5a4               ; melodic slots (count from the track table)
> 055F  mov cl,[es:0xdd7]        ; track_count
> 0566  shl dx,1 / jnc / add si,4 / loop     ; MSB first; a set bit is 4 bytes
> 056F  mov cx,5 / mov di,0x10cb / call 0x58e ; then exactly FIVE more slots
>
> 058E  shl dx,1 / jnc 0x59a     ; the slot decoder
> 0592  movsw / movsw            ;   bit set  -> copy 4 bytes
> 0594  add di,0x12              ;   (22-byte stride into the voice block)
> 059A  xor ax,ax / stosw / stosw ;  bit clear -> write an empty cell
> ```
>
> `shl dx,1` + `jnc` is MSB-first bit 15 → slot 0; the payload is
> unconditionally four bytes; and the row is `track_count` slots followed by a
> hardcoded `mov cx,5`. That is §11.3 and §11.4 in six instructions.
>
> The cell handler at `0x05C6` reads `[si+0]` note, `[si+1]` instrument,
> `[si+2]` effect, `[si+3]` parameter, decrements the instrument and indexes the
> slot table at `0xCBF + 0x98` to fetch the default volume — confirming the
> field order, the 1-based selector and §4's volume rule. It then does
> `cmp byte [si+0x2],0x0C` and, on a match, substitutes `[si+3]` for the default
> volume and zeroes the effect: `0Ch` **overrides** the slot default, exactly as
> §4 states.
>
> Validated over all 16 files by `tools/alb_cells.py`: 32 493 cells decoded,
> every pattern landing exactly on its stated table boundary, every note in
> 17–96, and no cell entirely empty (a set mask bit always carries something).
>
> One cell in one file breaks the instrument-selector bound: `PIT/ADLIBFX.ALB`
> selects instrument 12 where `n_instr` is 4. That is a data defect rather than
> a decode failure — the file is a scratch bank whose first record is named
> `TEST1`, its record count is corroborated by a zero-slack file size, and the
> driver would simply index past the array. Every other selector in the corpus
> is in range.

Effect usage across the corpus is dominated by the same two commands as the
older generations — `0x0C` set volume (8262 cells) and `0x0A` volume slide
(1859) — with `0x0F` set speed (291) and a long tail of `0x01`–`0x0E`. 17 628
cells carry effect 0, i.e. a bare note.

### 11.4 The five percussion slots

**A `.ALB` row has `track_count + 5` slots, not `track_count`.** The five extra
are the OPL2 rhythm-mode percussion channels of §10, in the driver's own order:

| Slot | Voice | Editor rhythm code (§7.4 `+0x26`) |
|---|---|---|
| `track_count + 0` | bass drum | 6 |
| `track_count + 1` | snare drum | 7 |
| `track_count + 2` | tom-tom | 8 |
| `track_count + 3` | cymbal | 9 |
| `track_count + 4` | hi-hat | 10 |

Two independent lines of evidence, both corpus-wide:

1. **No mask bit at or above `track_count + 5` is ever set.** Across all 14 files
   the highest slot used is exactly `track_count + 5` in the busy songs (6+5=11
   in `ING4`, `ADLIB2`, `ING`, `TITLE`, `ING3`; 4+5=9 in `CRUSFX`, `DEATH`;
   5+5=10 in `UTOFX`) and below it in the sparse ones. Never above.
2. **The instruments played in slot `track_count + k` carry rhythm code `6 + k`.**
   Checking every instrument that appears in a percussion slot gives **32
   agreements and 1 disagreement** — the exception being `CHAOS/ADLIB2.ALB`,
   which plays `BDRUM1` (code 6) in the snare slot. The names line up with the
   codes throughout: `BDRUM1` in the bass-drum slot, `SNARE1` in the snare slot,
   `WHITENOISE` in the cymbal slot, `HIHAT1`/`HIHAT2` in the hi-hat slot.

This is the cleanest confirmation in the whole corpus that §10's rhythm codes
mean what they say — the `.ALB` export has baked the mapping into the row layout
itself.

#### Not emulated, but voiced

`src/opl_dev.c` is melodic-only (§10), so `oldrld.c` reports the row width as
`track_count + 5` and lets all eleven slots play as ordinary melodic voices.
That is exact for the bass drum, which in OPL2 rhythm mode really is a normal
two-operator voice, and approximate for the other four, which in hardware share
operators and are keyed through `$BD` rather than `$B0`.

Note the difference from `B4`, where a percussion instrument is parsed and then
**not** voiced at all: here it *is* voiced, just melodically. `osl1_dump`'s
`rhythm instr` line spells out which case a given file is in.

### 11.5 Instruments

Only the 64-byte Adlib editor records of §7.4, `n_instr` of them, one per slot in
slot order including blank ones, running from `table[pattern_count]` to EOF with
no slack. The record layout, the 13 editor fields, and the LEVEL/SUSTAIN
inversion are all exactly as §7.4 describes.

Consequences:

* **`--fm-source` has no effect on `.ALB`.** There is no second source to choose
  between; `oldrld.c` forces `editor` regardless of the setting.
* §7.4's whole-bank blankness test does not apply — these records are not a
  reserved bank but the file's only instrument data, and blankness must be
  judged per slot via the presence flag (§11.2).
* The rhythm field at `+0x26` is live and meaningful here, and §10's
  range-check-don't-truth-test rule still applies.

Tempo and speed are unstored, exactly as in §9: 50 Hz and 6 ticks/row, with
songs overriding via an explicit `0x0F` on an early row.

### 11.6 Can `BSSJS/ADLIB.DRV` read it?

**No — but not because it refuses to.** The 1991 driver performs **no
magic-signature check at all**. Its install routine `api_after_load` (`0x00DC` in
`../BSSJS_ADLIB.DRV.annotated.asm`) guards only on `cmp byte [installed],0xFF`,
then `rep movsb`s file `+0x000`–`+0x117` into `hdr_copy` (`0x0E77`) and
`+0x118`–`+0x317` into `para_table` (`0x0F90`). The magic bytes land at
`0x0E77`–`0x0E79` and are never read again.

So it would **accept** an `.ALB` file and then produce garbage. Every offset it
relies on has moved:

| The driver assumes | `.ALB` actually has |
|---|---|
| track count at `+0xD8` (`0x031D`, `0x03C4`, `0x03E6`, `0x0460`, no range check) | permanent zero — the row loop would run 256 iterations |
| paragraph table at `+0x118` | header fields; the table is after a variable cue table |
| pattern stream at `+0x318` | wherever the paragraph table ends |
| 256-byte instrument blocks | none at all |
| 2-bit code words with 0/2/2/7 payloads | 16-bit presence masks with 4-byte cells |

The unchecked track-count read is the decisive one: it is used directly as a loop
bound with no clamp, so an `.ALB` file drives the driver straight off the end of
its row buffer. There is no sense in which the April 1991 driver "handles" this
format. Nor does `MED.EXE`, whose `cmpw $0x9ab6` rejects it outright.

#### The driver that *does* read it (found 2026-07)

An earlier revision of this section concluded that no surviving binary loads
`.ALB`. **That was wrong.** `MEDIT/LAPMUSIC/OLDMUSIC/OLDMUSIC/PIT/ADLIB/` — a
directory that exists only in the nested `OLDMUSIC/` copy, which is why it was
missed — contains the whole delivery kit:

| File | Date | What it is |
|---|---|---|
| `ADLIB.EXE` | 21 Sep 1991 | **"ADLIB DRIVER (Version 3.00)"**, 6112-byte flat binary behind a 32-paragraph MZ stub. The `.ALB` player. |
| `README.DOC` | 26 Sep 1991 | Its API manual (also at `PIT/ADREAD.DOC`). |
| `MUSIC.EXE`, `FX.EXE` | 8 Oct 1991 | PKLITE'd harnesses; their strings include `adlib.alb` / `adlibfx.alb` and `music driver v. 3.00 , (c) 1991 Imagitec Design Ltd.` |
| `../MUSIC.ASM`, `../FX.ASM` | 8 Oct 1991 | **MASM source** of those harnesses. |
| `ADLIB.ALB`, `ADLIBFX.ALB` | 6 Nov 1991 | The songs it plays. |

`ADLIB.EXE` is a direct descendant of `BSSJS/ADLIB.DRV`: 100 of 359 sampled
32-byte windows of the older driver appear verbatim in it, the `INT 60h` API is
the same, and the 96-entry note table is byte-identical (`0x0157 0x016C 0x0181 …`
at `0x1203` here, `0x1392` there, with the same `sub bl,0x0C` C-1 base). It is
the *same driver, one generation on*, retargeted at the exported container.

`MUSIC.ASM` shows how it is used, and independently corroborates the container:
it loads the driver as a flat binary at offset 0 of a segment, calls it, then
issues `AX=2` "afterload" with `DX` = the music segment; and its `dump_titles`
routine walks the loaded file from **`si = 158h` printing 10 characters per
entry with `add si,16-10`** — the cue table of §5, at the offset and stride this
document gives it, printed by Ocean's own diagnostic code.

`README.DOC` also settles §9 in one line: **"Note driver runs at 50hz."**

Two caveats on the find. First, this v3.00 driver is `.ALB`-only — its header
copy is `0x158` bytes and it reads `n_cue` at `+0x11B`, so handed a `B4` file it
would compute `n_cue = 0`, put the paragraph table at `+0x158` instead of
`+0x118`, and produce garbage in the mirror image of the failure tabulated
above. Second, the games themselves presumably still linked their own copy; what
survives here is the driver as *delivered to the developer*, alongside a test
harness, which is if anything better evidence than a shipped game would be.

#### The v3.00 effect table

Two 32-entry dispatch tables, indexed by `effect & 0x1F` (body `0x062B` and
`0x0639`) exactly as in `ADLIB.DRV`: one run once per row, one run every tick.
`0x0647`/`0x0648` are the bare-`ret` stubs.

| Cmd | Per-row | Per-tick | Meaning |
|---|---|---|---|
| `0x01` | `0x06DE` | stub | portamento up |
| `0x02` | `0x0708` | stub | portamento down |
| `0x03` | `0x07A6` | stub | tone portamento |
| `0x04` | stub | `0x0838` | vibrato — **implemented here**, unlike `ADLIB.DRV` |
| `0x06` | stub | `0x0734` | note off |
| `0x0A` | `0x06A8` | stub | volume slide |
| `0x0B` | stub | `0x0743` | position jump |
| `0x0C` | stub | `0x0753` | set volume |
| `0x0D` | stub | `0x073B` | **pattern break** |
| `0x0F` | stub | `0x0766` | **set speed** |

Everything else — `0x00`, `0x05`, `0x07`, `0x08`, **`0x09`**, **`0x0E`** and
`0x10`–`0x1F` — is a stub in both tables. Two consequences, both of which
confirm §9.1 from a second driver: `0x09` does nothing (and duly appears zero
times in the `.ALB` corpus), and `0x0F` is *set speed*, its handler being the
familiar four instructions —

```
0766  xor ah,ah / mov al,[si+0x3] / and al,0x1f / jz ret
076F  mov di,[0x1149] / mov byte [di+0x6],0 / mov [di+0x2],al
```

— mask to 5 bits, treat zero as a no-op, reset the tick counter, store the
speed. Byte-for-byte the same routine as `ADLIB.DRV`'s `fx_set_speed` @`0x06CB`
and OSL1's `0x09` handler.

The 50 Hz claim is likewise hard-coded rather than derived: the "internal
interrupts on" call (`AX = 0x0A`) does `mov ax,0x5D37` and programs the PIT with
it (`0x0335`), and `0x5D37` = 23863, giving 1 193 182 / 23863 = **50.0016 Hz**.
Turning interrupts off writes divisor 0, restoring the BIOS 18.2 Hz. Nothing
between those two points touches the timer, and no file field reaches it.

### 11.7 Playback verification

All 25 `.ALB` paths under `medplay/test/` render through `medplay --wav` with
healthy amplitude — peak 4087–25117, RMS 816–4751, **no silent or near-silent
file**. The two quietest are the sound-effect banks (`UTOFX`, `ING1`), which are
sparse by nature. The two `PIT` files found later render cleanly too —
`ADLIB.ALB` peak 23237 / RMS 4568, `ADLIBFX.ALB` peak 7606 / RMS 467, the latter
being a four-effect scratch bank.

Adding `.ALB` support did not disturb the older generations: after the change,
`decode_dump` and `osl1_dump` output is **byte-identical to the previous
revision on all 190 `B4` and all 314 `B6` files** in the test corpus.

---

## Appendix A — Reading an old `.RLD` in ten steps

Written for the `B4` and `B6` generations. Where two offsets are given, the first
is B6 and the second B4. For `20 AD 01` files see §11 — steps 1, 5, 6, 8 and 9
all differ.

1. Check bytes `+0x00`–`+0x02` for `B6 9A 01` or `B4 9A 01`. Set
   `slot_count` = 64 or 32 and `para_off` = `0x958` or `0x118` accordingly.
   (`20 AD 01` is the `.ALB` variant of §11.)
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

## Appendix A2 — …and an `.ALB` in seven

1. Check `+0x00`–`+0x02` for `20 AD 01`. Read `track_count` at `+0x118`,
   `restart_idx` at `+0x119`, `n_instr` at `+0x11A` and `n_cue` at `+0x11B`.
2. Order length and pattern count exactly as steps 2–3 above.
3. `para_off = 0x158 + 16 × n_cue`. Read `pattern_count + 1` `u16` entries from
   it; pattern *i* is at `para_off + 16 × table[i]`, and entry `pattern_count`
   is the start of the instrument records. There is no stale tail to skip.
4. Read the presence/volume pairs from `+0x98` for slots `0 … n_instr − 1` only,
   and take each default volume as `min(volume × 2, 0x7F)`.
5. For each pattern: 64 rows, each a `u16` presence mask followed by one 4-byte
   cell (note, 1-based instrument, effect command, effect parameter) per set
   bit, scanned MSB-first, in ascending slot order.
6. A row has `track_count + 5` slots; the last five are OPL2 percussion (§11.4).
7. Read `n_instr` 64-byte editor records from the base found in step 3 — the
   same records as §7.4, decoded the same way, LEVEL and SUSTAIN inverted. They
   run to EOF with no slack; if they do not, something earlier is wrong.

## Appendix B — Corpus

**514 old-format files**, all under `MEDIT/LAPMUSIC/` (mostly `OLDMUSIC/`, plus
`LETHAL3/`) — 311 `B6`, 189 `B4` and 14 `.ALB`. All 514 parse cleanly under
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
| `20 AD 01` | — | 14 | 1992; `.ALB` runtime export — **§11**. 20 paths, 14 distinct |
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
single `cmpw $0x9ab6` with no fallback, so 206 files are outside the editor
entirely. The `B4` ones belong to the 1991 standalone driver
`MEDIT/LAPMUSIC/OLDMUSIC/BSSJS/ADLIB.DRV`, annotated in full at
`../BSSJS_ADLIB.DRV.annotated.asm`; the `.ALB` ones belong to no surviving
binary at all (§11.6).

See `../RE-REPORT.md` §11.1b for the evidence that the generation byte tracks
*date* rather than target device, and §11.3 for the editor-record derivation.
