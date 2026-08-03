# The Pre-OSL1 `B4`/`B6` `.RLD` Format

A clean-room description of the **older, pre-`OSL1`** music format written by
Ocean Software's tracker as an *editor working file*. Files in this format
usually carry the extension `.RLD` (a few carry none at all) and begin with a
3-byte magic `B4 9A 01` or `B6 9A 01` instead of the ASCII `"OSL1"`.

This document is the byte-level specification. It was reconstructed by
reverse-engineering two separate loaders — `MED.EXE`'s alternate load path
(`med.asm` ≈ `0x233B`–`0x2717`) and the 1991 standalone driver
`MEDIT/LAPMUSIC/OLDMUSIC/BSSJS/ADLIB.DRV` (fully annotated in
`../BSSJS_ADLIB.DRV.annotated.asm`) — cross-checked against the 502 `B4`/`B6`
files in the corpus and, crucially for the instrument layout, against the
326-file OSL1 corpus, which shares many of the same named instruments. Where a
field's meaning is confirmed it is stated plainly; where it is inferred or
unverified it is marked **(inferred)** or **(unverified)**.

The reference implementation is `src/oldrld.c`. See `OSL1.md` for the newer
container — `B4`/`B6` share the pattern *cell* encoding with it but nothing
else — and `ALB.md` for the third pre-`OSL1` generation.

## Two generations, and a third elsewhere

The first magic byte is a **generation counter, not a device id**. Both
generations hold OPL2 FM data; the `A/` versus `R/` folder convention seen
throughout `OLDMUSIC/` is the orthogonal Adlib/Roland distinction. The evidence
for that — Roland folders full of `B4` files, an Adlib tune that is `B6`, 19
project folders holding both — is set out in `../RE-REPORT.md` §11.1b.

| | `B4 9A 01` | `B6 9A 01` |
|---|---|---|
| Era | Jan–May 1991 | Aug 1991 – 1993 |
| Extension | `.RLD` | `.RLD` |
| Files in corpus | 189 (149 distinct) | 313 (297 distinct) |
| Instrument slots | 32 | 64 |
| Slot table size | `0x40` bytes | `0x80` bytes |
| Cue table | **absent** | 128 × 16 bytes at `+0x158` |
| Paragraph table | `+0x118`, 256 fixed entries | `+0x958`, 256 fixed entries |
| Pattern stream | `+0x318` | `+0xB58` |
| Adlib editor bank | always present; live in 99 | present in 138, live in 17 |
| Pattern break effect | `0x0D` | `0x0E` (see §9.1) |
| Loaded by | `BSSJS/ADLIB.DRV` (Apr 1991) | `MED.EXE` / `TRACKER.DRV` (1992–93) |

**The two differ in one design decision — 32 instrument slots versus 64 — plus
one added feature.** The magic layout, the order table, the slot table, the
paragraph addressing, the pattern encoding, the 256-byte instrument blocks, the
split-patch layout and the editor bank are all **bit-identical**. The `0x800`
cue table plus the `0x40` slot shift is the whole `0x840` difference between
`+0x318` and `+0xB58`. A `B4` reader and a `B6` reader differ only in two
constants, which is why one document specifies both.

Everything below is verified corpus-wide by `tools/gen_compare.py` over the 149
distinct `B4` and 297 distinct `B6` files: 207 085 and 455 067 pattern cells
decoded, **0 boundary misses**, presence byte only ever 0 or 1, absent-slot
volume always 0, volume ceiling `0x40`, and `table[0] == 32` in every one of the
446 files.

One caveat is not by design but is worth knowing about when writing a reader.
`B6` carries a little dirt: **3 of its 297 distinct files** hold roughly 200
cells with notes past 108, selectors past 64 or effect commands past `0x0F` —
`OLDMUSIC/TUNE.RLD` and two variants of `DEMO/MOON.RLD`. They are the format's
oldest and least tidy specimens, they still render, and a reader should **clamp
rather than reject**. `B4` produces no out-of-range cell at all.

### The third generation

A third pre-`OSL1` generation exists: **`20 AD 01`**, always `.ALB`, a *runtime
export* rather than an editor working file. It reuses the header prefix and the
paragraph-addressing scheme described here, but replaces the pattern encoding
and drops the 256-byte instrument blocks entirely. It is specified separately in
**`ALB.md`**; nothing in this document applies to it except where `ALB.md` says
so. `README.md` sets all three generations side by side.

**Each generation has its own dedicated driver, and no driver reads more than
one of them.** All three loaders begin by `rep movsb`-ing a fixed-size header
copy out of the loaded file — `0x118` bytes in `ADLIB.DRV`, `0x158` in
`.ALB`'s `ADLIB.EXE` v3.00 — and every offset they use afterwards is baked in.
There is no magic check anywhere in either standalone driver and no fallback in
`MED.EXE`, so the generations are mutually unreadable in practice even though
they share most of their field layout on paper.

`MED.EXE` accepts `B6` only: its magic test at `med.asm:0x2364` is a bare
`cmpw $0x9ab6` with no fallback, so the `B3`/`B4`/`B5` and `.ALB` files cannot
be opened in the editor at all. `src/oldrld.c` reads `B4`, `B6` and `.ALB` — all
524 files load cleanly — while `B3` (1 file) and `B5` (2 files) remain unhandled
for want of specimens.

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

The `20 AD 01` generation (`ALB.md`) does **not** share it. Its rows carry a
presence *mask* and fixed 4-byte cells rather than a 2-bit code word and
variable payloads, so it needs its own decoder — but the decoder still writes
into the same 8-byte cell shape, so the replay engine remains untouched for it
too.

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

The `20 AD 01` generation breaks that property: both its cue table and its
paragraph table are sized from header counts, so everything from `+0x158` onward
moves. See `ALB.md` §3.

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

The `20 AD 01` generation carries the same 16-byte entry layout but a
**variable** number of them, and its positions are **0-based** where B6's appear
to be 1-based. See `ALB.md` §5.

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
`32` in every one of the 502 corpus files, giving `0x958 + 16 × 32 == 0xB58` for
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
> Validated corpus-wide: for all 502 files, `filesize − (blocks_off +
> 256 × present_count)` is exactly 0 or exactly 2048 — never anything else. The
> 2048 is §7.4's Adlib editor bank.
>
> **The `20 AD 01` generation has no stale entries.** Its table holds exactly
> `pattern_count + 1` entries, padded to a paragraph and nothing more
> (`ALB.md` §6). The warning above applies to `B4`/`B6` only.

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

> **Confirmed in writing (2026-07).** `PIT/ADLIB/README.DOC` — Imagitec's own
> API manual for the sibling `.ALB` driver (`ALB.md` §11) — states it outright
> under the initialisation instructions: **"Note driver runs at 50hz."** The
> binary backs the sentence up with a literal: `mov ax,0x5D37` immediately
> before the PIT write, and 1 193 182 / 23863 = 50.0016 Hz. Nothing else in
> either driver writes the timer. Both the rate and its immutability are
> therefore documented by the authors rather than merely inferred from
> disassembly, and the 50 in `oldrld.c` is not a fallback but the specification.

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

The corpus settles it beyond doubt. Across the 446 distinct `B4`/`B6` songs,
every one of the 9408 `Fxx` parameters bar nine strays falls in `1`–`0x20`, and
they cluster on exactly the values a tick count would:

| Param | 4 | 5 | 6 | 8 | 9 | others |
|---|---:|---:|---:|---:|---:|---:|
| B6 (8617 total) | 828 | 741 | **3170** | 437 | 2694 | 747 |
| B4 (791) | 23 | 78 | **293** | 127 | 10 | 260 |

The mode is 6 — the format's own default speed. A tempo in Hz would cluster near
50, and a ProTracker BPM near 125; neither appears. Conversely `0x09`, OSL1's
set-speed, is effectively absent from old files: 4 occurrences in all of B6 and
none at all in B4, exactly as a stubbed handler predicts. The `20 AD 01`
generation agrees independently — see `ALB.md` §10.2.

> **Correction (2026-07).** `src/replay.c` originally ran one effect table for
> both formats, so an old-format `Fxx` reached OSL1's set-*tempo* handler. Its
> `if (t < 19) t = 19` clamp then pinned the timer at 19 Hz — **38% of the
> correct rate** — and the speed change the song actually asked for never
> happened. It affected **170 of 190 B4 files and 247 of 314 B6 files**: every
> old song carrying an `Fxx`, which is most of them. `replay.c` now switches on
> `Replay.old_format` and routes `0x0F` to the shared `set_speed()` helper for
> pre-OSL1 songs. After the fix no old-format file moves off 50 Hz, and 112 B4
> and 209 B6 files change speed where none did before (plus 17 `.ALB`). OSL1
> playback is bit-identical either way.

`OLDMUSIC/INFERNO.RLD` is the reference case: it opens with `0F 05`, which now
correctly means "five ticks per row" (tempo 50, speed 6 → 5) rather than
"19 Hz".

> **`0x0D` versus `0x0E` for pattern break — settled for `B4` and `.ALB`,
> still open for `B6`.** The same effect table shows `ADLIB.DRV` handling
> **`0x0D`** as pattern break with `0x0E` stubbed, whereas OSL1 — and therefore
> `replay.c` — uses `0x0E`.
>
> The `.ALB` driver found in 2026-07 (`ALB.md` §10.3) agrees with `ADLIB.DRV`:
> its per-tick dispatch table sends `0x0D` to a real handler at `0x073B` and
> `0x0E` to the bare `ret` at `0x0647`. So **two of the three generations
> demonstrably break on `0x0D`.** The corpus agrees: `0x0D` appears 557 times in
> B4 and in 8 of the 16 `.ALB` files, `0x0E` once in B4 and in only 2 `.ALB`
> files. `CRUSADE/CRUSFX.ALB` is the clincher — a sound-effect bank of 25
> one-position cues carrying exactly 25 `0x0D`s, one to terminate each.
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

Moved. The third pre-`OSL1` generation — magic `20 AD 01`, always `.ALB`, a
runtime export rather than an editor working file — now has its own
self-contained specification in **`ALB.md`**.

It shares this format's header prefix (`+0x00`–`+0x97`), its order-table length
rule, its slot-table semantics and its paragraph addressing, but not its pattern
encoding, and it carries no 256-byte instrument blocks at all. The quick map:

| Here | In `ALB.md` |
|---|---|
| §4 file header | §4 |
| §5 cue table | §5 — variable length, **0-based** positions |
| §6.1 paragraph table | §6 — `pattern_count + 1` entries, no stale tail |
| §6.2 cell encoding | §7 — **presence mask, fixed 4-byte cells** |
| §7 instrument blocks | — none at all |
| §7.4 editor records | §9 — the file's only instrument data |
| §9.1 timing and `Fxx` | §10 |
| §10 rhythm mode | §8 — five explicit percussion slots per row |

---

## Appendix A — Reading an old `.RLD` in ten steps

Where two offsets are given, the first is B6 and the second B4. For `20 AD 01`
files follow `ALB.md` Appendix A instead — steps 1, 5, 6, 8 and 9 all differ.

1. Check bytes `+0x00`–`+0x02` for `B6 9A 01` or `B4 9A 01`. Set
   `slot_count` = 64 or 32 and `para_off` = `0x958` or `0x118` accordingly.
   (`20 AD 01` is the `.ALB` generation — see `ALB.md`.)
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

**502 `B4`/`B6` files** under `MEDIT/` (all inside `LAPMUSIC/` — mostly
`OLDMUSIC/`, plus `LETHAL3/`) — 313 `B6` and 189 `B4`, every one of which parses
cleanly under `src/oldrld.c`. A further 22 files carry the `20 AD 01` magic
(`ALB.md`) and 3 the unhandled `B3`/`B5` magics, for **524 old-format files** in
total. Cross-referenced against the 326-file OSL1 corpus documented in
`OSL1.md`.

Deduplicated by content the totals are much lower — **297 `B6` and 149 `B4`**
(and 16 `.ALB`) — because `OLDMUSIC/` contains a nested copy of itself.
(`medplay/test/` is a further working duplicate of the whole tree and is
excluded from every figure in this appendix.) Where a claim in this document is
statistical it counts distinct files; where it is about the archive as found,
paths.

Files are classified **by magic, not by extension**. Four old-format files carry
no extension at all — `OLDMUSIC/DEMO/LD` (`B4`) and `OLDMUSIC/DG/TOSS` (`B6`),
each appearing twice because `OLDMUSIC/` contains a nested copy of itself.

Counting the authoritative copy under `MEDIT/` only:

| Magic | `.RLD` | `.ALB` | none | paths | distinct | Notes |
|-------|-------:|-------:|-----:|------:|---------:|-------|
| `B3 9A 01` | 1 | — | — | 1 | 1 | single specimen; unhandled |
| `B4 9A 01` | 187 | — | 2 | 189 | 149 | 1991; 32 instrument slots — **this document** |
| `B5 9A 01` | 2 | — | — | 2 | 2 | transitional; unhandled |
| `B6 9A 01` | 311 | — | 2 | 313 | 297 | 1991–93; 64 instrument slots — **this document** |
| `20 AD 01` | — | 22 | — | 22 | 16 | 1991–92; runtime export — **`ALB.md`** |
| `"OSL1"` | 109 | many | — | — | — | not this format at all — ordinary OSL1 containers |
| — | 2 | — | — | 2 | — | a misnamed text file |

Note the last two rows: `.RLD` is a filename convention covering song files of
every vintage and every device, not a format marker. The 109 OSL1-magic ones
split by device byte as 51 × `0x00`, 46 × `0x04` (Roland), 7 × `0x02` (Adlib) and
5 × `0x08` (SCC1).

`B3` and `B5` remain unhandled for want of specimens — 3 files in total. Their
header offsets are presumably a further variation on the slot-count shift, but
one and two files respectively is not enough to establish a layout with any
confidence.

`MED.EXE` cannot load anything but `B6`. Its magic test at `med.asm:0x2364` is a
single `cmpw $0x9ab6` with no fallback, so 208 files are outside the editor
entirely. Each has its own standalone driver: the `B4` ones belong to
`MEDIT/LAPMUSIC/OLDMUSIC/BSSJS/ADLIB.DRV` (April 1991), annotated in full at
`../BSSJS_ADLIB.DRV.annotated.asm`; the `.ALB` ones to
`MEDIT/LAPMUSIC/OLDMUSIC/OLDMUSIC/PIT/ADLIB/ADLIB.EXE` (v3.00, September 1991),
which arrives with its API manual, its MASM test harnesses and their source
(`ALB.md` §11).

See `../RE-REPORT.md` §11.1b for the evidence that the generation byte tracks
*date* rather than target device, and §11.3 for the editor-record derivation.
