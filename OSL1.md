# The OSL1 Container Format

A complete, clean-room description of the **OSL1** music container written by
Ocean Software's `MED.EXE` (1993) and played back by its `TRACKER.DRV` driver
with a per-device backend (`ADLIB.DEV`, `SBLAST.DEV`, `LAPC1.DEV`, `SCC1.DEV`
and a Super Nintendo target).

This document is the byte-level specification. It was reconstructed by
reverse-engineering `MED.EXE`, `TRACKER.DRV` and `ADLIB.DEV`, cross-checked
against a corpus of 326 OSL1 files and against DRO register captures. Where a
field's meaning is confirmed it is stated plainly; where it is inferred or
unverified it is marked **(inferred)** or **(unverified)**.

The reference implementation is `src/osl1.c` (parser) and `src/replay.c`
(replay engine); the corpus scanner is `tools/osl1_scan.py`.

---

## 1. Conventions

* **All multi-byte integers are little-endian.** The format never relies on C
  struct packing; every field is read at an explicit offset.
* Offsets written `+0xNN` are relative to the start of the enclosing structure
  (file, record or block). "File-absolute" means relative to byte 0 of the file.
* Types: `u8`, `u16`, `u32` = unsigned 8/16/32-bit; `char[N]` = fixed-width,
  NUL-padded ASCII (not necessarily NUL-terminated when full).
* Sizes and offsets use hexadecimal; counts use decimal.

---

## 2. File families and the device question

OSL1 files appear with several extensions, one per Ocean project/target:

| Ext    | Typical origin                             |
|--------|--------------------------------------------|
| `.ALB` | Ocean "Album"/SHUTIT-era songs             |
| `.ADL` | Adlib-oriented exports                      |
| `.LAP` | Roland LAPC-I native sessions               |
| `.SCC` | Roland SCC-1 / Sound Canvas sessions         |
| `.RLD` | Roland-target variants                       |
| `.SNS` | Super Nintendo sample banks                  |
| `.SAV` | Editor autosaves                             |

Note that **not every `.RLD` is an OSL1 file**: 311 of them are in an older,
entirely different format that `MED.EXE` loads through a separate code path.
They are identified by the magic `B6 9A 01` in place of `"OSL1"` and are
specified in `RLD.md` and `ALB.md`.

**The extension does not determine the playback hardware, and neither does any
header byte.** OSL1 is a *device-agnostic* container. `MED.EXE` selects the
target per *instrument* (its editor's "Device:" field) and via the loaded
driver; the same file can mix instruments voiced for different devices. The
five devices `MED.EXE` drives are:

| Device      | Driver       | Hardware                              |
|-------------|--------------|---------------------------------------|
| Adlib       | `ADLIB.DEV`  | Yamaha OPL2 FM (single chip, 9 ch)    |
| Sound Blaster | `SBLAST.DEV` | Creative Sound Blaster              |
| LAPC-I      | `LAPC1.DEV`  | Roland LAPC-I / MT-32 (MIDI)          |
| SCC-1       | `SCC1.DEV`   | Roland SCC-1 / Sound Canvas (GS)      |
| SNES        | (SNES target)| Super Nintendo S-DSP (FIR + echo)     |

Because the container cannot reliably name the target, renderability is best
derived from *content* — see §6 (synth codes) and §7 (heuristic class).

---

## 3. Top-level layout

```
+0x0000  File header (see §4)
+0x004E  Instrument pointer table (see §5)
  ...    Instrument records (pointed to; see §6)          [order varies]
  ...    Pattern block (at header.block_off; see §8)
  ...    Position records (pointed to by the block; see §9)
```

Only the header and the instrument pointer table live at fixed offsets. The
instrument records, the pattern block, and the position records are located via
offset fields and may appear in any order in the file body.

---

## 4. File header (`+0x00` … `+0x4F`)

| Offset | Type      | Field         | Notes                                                        |
|--------|-----------|---------------|--------------------------------------------------------------|
| +0x00  | char[4]   | signature     | `"OSL1"`. Files not starting with this are rejected.         |
| +0x04  | u8        | version       | Format version. Commonly 0.                                  |
| +0x05  | u16       | constant      | Observed `0x0001` corpus-wide. Purpose unconfirmed **(unverified)**. |
| +0x07  | u8        | generation    | Format-revision counter. Only `0x00`, `0x02`, `0x04` observed. **Not** a device id (see below). |
| +0x08  | —         | —             | Reserved/unknown region.                                     |
| +0x28  | char[30]  | title         | Song title, NUL-padded.                                      |
| +0x48  | u32       | block_off     | File-absolute offset of the pattern block (§8).             |
| +0x4C  | u16       | instr_count   | Number of entries in the instrument pointer table.          |
| +0x4E  | u32[]     | instr_table   | Start of the instrument pointer table itself (§5).          |

### The generation byte `+0x07`

Historically mistaken for a device selector. It is **not**: across the corpus it
only ever holds `0x00`, `0x02` or `0x04`, and it does not track the extension
(e.g. `.ADL`/`.SCC`/`.RLD` from one project all share `0x02`). It is best read
as a format-revision/generation marker:

* `0x00` — oldest observed revision
* `0x02` — mid revision
* `0x04` — newest observed revision

(Note: a few non-song files such as `.SNS` banks carry `0x81` here and use a
different record layout — treat those as out-of-spec for the song format.)

---

## 5. Instrument pointer table (`+0x4E`)

`instr_count` entries, **4 bytes each** (a file-absolute u32 record offset),
starting at file offset `0x4E`. The instrument records follow **immediately**
after the table, so:

```
instr[0].record_offset == 0x4E + 4 * instr_count
```

This invariant holds exactly on 304 of the 322 OSL1 files in the corpus. In the
other 18, entry 0 is `0x00000000` — an unused instrument slot 0 — and the
records begin at the same computed address anyway.

Instrument selectors in pattern cells are **1-based**: cell selector `n`
resolves to table index `n - 1`, and `0` means "no instrument change".

> **Correction (was wrong until the `od1.dro` register diff).** This table was
> previously documented and parsed as starting at `0x50` with the offset in the
> *high* half of each entry (i.e. effectively reading offsets from `0x52`), with
> `0x4E` mistaken for an `instr_size` field. That is one entry too late: it
> silently dropped **instrument 0 of every song** (+300 instruments across the
> 326-file corpus once fixed) and made the final table entry read garbage out of
> the first record's length field. Because the replay engine compensated with a
> `selector - 2` mapping, the two off-by-ones cancelled for selectors `>= 2` and
> only selector 1 misbehaved — which is why it survived earlier parity checks.
> It was caught by diffing medplay's OPL register trace against a DOSBox capture
> of MED.EXE playing `OD1.ALB`: track 0 selects instrument 0 ("Dean Bdrum",
> transpose −24) and medplay was dropping both the patch and the transpose,
> playing that bass-drum line two octaves high with the wrong timbre.
> See `tools/regcmp.py`.

### Pointer validation (parser policy)

An entry is treated as a valid instrument when:

1. `record_offset >= 0x4E + 4 * instr_count` (clears the table itself, and so
   also rejects a NULL/unused entry), **and**
2. `record_offset + 0x3E <= file_size` (the bytes we read fit), **and**
3. `record_offset` is strictly greater than the previous valid entry's offset
   (monotonically increasing).

Entries failing these tests are placeholders/padding and are skipped.

---

## 6. Instrument records

> **Correction (this rewrite).** An instrument record is **not** a flat struct
> with a "synth-type code" that picks one of FM-short / FM-ext / MIDI. It is a
> *container of one or more per-device variants*. Each variant carries its own
> **OSL device code** — the same `D_DEVICENUMBER` enum every `.DEV` exports at
> offset 0 (`LAPC1.DEV.annotated.asm:29`) — and its own device-specific payload.
> The value earlier drafts called "synth `0x24`" is the device code of the
> *first* variant, and the length↔code correlation is simply that each device's
> payload is a fixed size. In particular **`0x04` is Roland MT-32 / LAPC-1, not
> "FM-ext"**: those 286-byte records hold a 244-byte MT-32 timbre dump, and
> feeding their first 11 bytes to the OPL2 renders garbage (this was the
> `JINGLE.RLD` bug). Confirmed against MED.EXE's own loader (`med.asm`
> 0x25DC–0x26F2), `TRACKER.DRV` 0x15E1/0x10CD/0x1236, and `ADLIB.DEV:48`.

### 6.1 Record container header

| Offset | Type      | Field       | Notes                                                       |
|--------|-----------|-------------|-------------------------------------------------------------|
| +0x00  | u16       | length      | Total record span **minus 4** (excludes this field). 58 for a lone Adlib variant, 286 for Roland, 170 for SCC-1. |
| +0x04  | u16       | n_variants  | Number of device variants in this record. `1` in all but 5 corpus records; MED.EXE's loader writes `1` (`med.asm` 0x26BC). |
| +0x06  | u16       | desc0       | Offset of variant 0 measured from `+0x04`; always `6` (`med.asm` 0x26C1). |
| +0x08  | u16       | 0           | Written `0` by the loader (`med.asm` 0x26C7). |
| …      | u16[]     | descriptors | `n_variants − 1` further 4-byte descriptors (`02 00 12 00` in the corpus) when the record carries more than one variant. |

The variants themselves follow contiguously, starting at `+0x04 + desc0`
(= `+0x0A` for a single-variant record). Each variant is walked by adding its
own payload length; the chain closes exactly on `length` for 6750 of 6757
corpus records (the 7 exceptions are `.SNS` sample banks).

### 6.2 Variant layout

All offsets are **variant-relative**. For a single-variant record the variant
base is record `+0x0A`, so the file offsets in the right column are what earlier
drafts hard-coded (and remain correct for that common case).

| Var off | Type      | Field       | Single-var file off | Notes |
|---------|-----------|-------------|---------------------|-------|
| +0x00   | char[20]  | name        | +0x0A               | Instrument name, NUL-padded. |
| +0x14   | u16       | 0xFFFF      | +0x1E               | Sentinel. |
| +0x16   | s8        | finetune    | +0x20               | Editor "FineTune" (−99..+99). **No effect on replay pitch** — every OSL driver quantises pitch to whole semitones from the note number (`ADLIB.DEV` fnum table @0x3B5; `SBLAST.DEV` `DoNoteOn`). Display only **(confirmed)**. |
| +0x18   | s8        | transpose   | +0x22               | Editor "Trans": signed semitone transpose, added to the pattern note before the note→F-number/block conversion (`TRACKER.DRV:10CD`). `COLUMBIA.ADL` `LOGDRUM1` = −24. **(confirmed)** |
| +0x19   | u8        | vol_cap     | +0x23               | Volume ceiling (`TRACKER.DRV:1236`). |
| +0x1A   | u8        | device      | +0x24               | OSL device code — see §6.3. **This is the byte earlier drafts called "synth".** |
| +0x20   | u32       | paylen      | +0x2A               | Payload length in bytes (`med.asm` 0x2685 writes `0xF4` = 244 for Roland). |
| +0x24   | payload   | payload     | +0x2E               | Device-specific; exactly the pointer `D_InstInit` receives (`ADLIB.DEV:48`). §6.5 covers the Adlib payload. |

> The drivers' own base pointer is the **variant**, not the record:
> `TRACKER.DRV:15E1` does `add di,6` after fetching the record, so
> record-relative = variant-relative + 6. That is why the single-variant file
> offsets above are all the variant offsets plus 6.

### 6.3 Device codes (`variant +0x1A`)

The OSL `D_DEVICENUMBER` enum. Payload size is 1:1 with the device, which is
what produces the length↔code correlation earlier drafts noted:

| Code   | Device            | Payload | Record len | Meaning                                                        |
|--------|-------------------|---------|------------|----------------------------------------------------------------|
| `0x02` | Adlib / OPL2      | 16 (11 live) | 58 (0x3A)  | Two-operator OPL2 voice (§6.5). The only device medplay renders. |
| `0x04` | Roland LAPC-1 / MT-32 | 244    | 286 (0x11E)| MT-32 timbre dump: Patch Memory header + Timbre Common + four 58-byte partials (`LAPC1.DEV.annotated.asm:775-904`). **Not OPL2 data.** |
| `0x08` | Roland SCC-1 / GS | 128     | 170 (0xAA) | GS/MIDI program; the GM program number is at payload `+0x02`.   |
| `0x81` | SNES S-DSP        | variable | variable  | Sampled instrument (only in `.SNS` banks). Not OPL2.            |

A variant walk that lands on any other code with a non-matching length is a
mis-parse of a placeholder/non-song file. medplay renders **only** device
`0x02`; every other device is treated as silent (see §7).

### 6.4 The MT-32 payload (`0x04`, first 8 bytes)

The 244-byte Roland payload opens with the MT-32 **Patch Memory** entry
(`LAPC1.DEV.annotated.asm:780-796`), followed by Timbre Common (`0x0A..0x0D`)
and four 58-byte partials:

| Byte | Field         | Range  |
|------|---------------|--------|
| 0    | timbre group  | 0..3   |
| 1    | timbre number | —      |
| 2    | key shift     | 0..48  |
| 3    | fine tune     | 0..100 |
| 4    | bender range  | 0..24  |
| 5    | assign mode   | 0..3   |
| 6    | reverb switch | 0..1   |
| 7    | dummy         | ignored by the MT-32 |

This header is the **signature** the old-format loader uses to tell a Roland
block from an OPL2 one (see `RLD.md` §7): a block passing `b0≤3 && b2≤48 &&
b3≤100 && b4≤24 && b5≤3 && b6≤1` is Roland. On the labelled OSL1 corpus that
passes 99.0% of device-`0x04` records and 0.0% of device-`0x02` ones.

### 6.5 The 11-byte OPL2 patch (Adlib payload, `variant +0x24`)

Two-operator OPL2 voice. `ADLIB.DEV`'s operator programmer uploads the bytes in
this exact order (verified against the disassembly at `~0xD7F` and DRO capture):

| Patch byte | OPL2 register | Operator  | Meaning                          |
|------------|---------------|-----------|----------------------------------|
| 0          | 0x20          | modulator | AM / VIB / EG-type / KSR / MULT  |
| 1          | 0x40          | modulator | KSL / total level                |
| 2          | 0x60          | modulator | attack / decay                   |
| 3          | 0x80          | modulator | sustain / release                |
| 4          | 0xE0          | modulator | waveform select                  |
| 5          | 0x20          | carrier   | AM / VIB / EG-type / KSR / MULT  |
| 6          | 0x40          | carrier   | KSL / total level                |
| 7          | 0x60          | carrier   | attack / decay                   |
| 8          | 0x80          | carrier   | sustain / release                |
| 9          | 0xE0          | carrier   | waveform select                  |
| 10         | 0xC0          | channel   | feedback / connection            |

The parser decides renderability from the **device code** (§6.3), not by
sniffing these bytes: only device `0x02` yields a usable OPL2 patch, and its
adl[] is kept; every other device is silenced and its adl[] is zeroed so the
non-OPL2 payload can never be uploaded as OPL registers. (The old non-zero-byte
heuristic survives only as a fallback for records whose device code is
unrecognised — i.e. non-song files.)

### 6.6 SCC-1 / MIDI record extras

| Offset | Type | Field   | Notes                                  |
|--------|------|---------|----------------------------------------|
| payload +0x02 | u8 | program | General MIDI program number (0–127), for device `0x08`. |

For non-Adlib devices there is no OPL2 patch; medplay leaves the voice silent.

---

## 7. Heuristic renderability class

Because neither the extension nor the generation byte names the target, files
are classified by the instrument mix — specifically, whether instruments carry
usable OPL2 FM patches (i.e. what the Adlib backend can render):

| Class    | Condition                                              | Meaning                          |
|----------|-------------------------------------------------------|----------------------------------|
| Unknown  | no valid instruments                                   | nothing to render                |
| Adlib    | every valid instrument is a device-`0x02` OPL2 variant | fully OPL2-renderable           |
| Mixed    | some Adlib, some Roland/SCC-1/SNES                      | partly renderable (non-Adlib = silent) |
| MIDI     | no Adlib variant anywhere                               | not OPL2-renderable              |

Corpus distribution (325 OSL1-magic files): 20 Adlib, 48 Mixed, 252 MIDI,
5 Unknown. The overwhelming MIDI majority is the point of this correction — most
`.RLD`/`.LAP`/`.SCC` files are Roland/SCC-1 songs with **no** OPL2 patch, and
earlier builds mislabelled all of them "Adlib (renderable)" and rendered their
device payloads as OPL2 garbage.

---

## 8. Pattern block (at `header.block_off`)

| Offset | Type       | Field       | Notes                                                     |
|--------|------------|-------------|-----------------------------------------------------------|
| +0x00  | char[16]   | subtitle    | NUL-padded.                                               |
| +0x10  | u8         | restart_idx | Loop-restart order position (driver `@0x417`).            |
| +0x12  | u16        | track_count | Number of tracks = the engine's **voice count** (see §11).|
| +0x14  | u16        | row_count   | Rows per position/pattern.                                |
| +0x16  | u16[8]     | defaults    | Per-track defaults; `0x7F7F` = rest **(inferred)**.       |
| +0x26  | u16        | checksum    | Purpose unconfirmed **(unverified)**.                     |
| +0x28  | u16        | ver_c       | Purpose unconfirmed **(unverified)**.                     |
| +0x2A  | u16        | tempo       | Timer frequency in Hz, fed to `set_tempo` (`@0x422`). Default 50. |
| +0x2C  | u8         | speed       | Initial ticks per row (driver `@0x42C`). Default 6.       |
| +0x4E  | u8         | order_count | Number of entries in the order table.                     |
| +0x50  | u8[]       | order       | Order table: `order_count` pattern indices.               |
| +0x150 | u32[]      | pos_ptr     | Position-pointer table: file-absolute pointers, indexed by pattern number. |

**Tempo vs speed** follow the OctaMed model: `tempo` sets the tick rate (Hz);
`speed` sets how many ticks make up one row. Effect `0x0F` sets tempo (clamped to
a minimum of 0x13); effect `0x09` sets speed.

---

## 9. Position records and the cell stream

Each order entry names a pattern; `pos_ptr[pattern]` points at that pattern's
position record. The pointer targets a **2-byte length prefix** (used by the
editor to skip records); the replay header begins immediately after it, at
`pos_ptr[pattern] + 2`.

```
base = pos_ptr[pattern] + 2
hdr  = u16 @ base
```

### 9.1 Uncompressed positions (`hdr & 0x8000 == 0`)

* `row_limit = hdr & 0x7FFF`.
* Row data is a raw copy: **8 bytes per voice for 16 voice-slots** = `0x80`
  bytes per row, regardless of the actual track count. The stream cursor
  advances a fixed `0x80` each row.

### 9.2 Compressed positions (`hdr & 0x8000` set)

* `row_limit = hdr & 0x0FFF`.
* Flag bits in `hdr`:
  * `0x4000` — variable bit-alignment of the code window (see below).
  * `0x2000` — cells omit the trailing "param 2" byte.
  * `0x1000` — cells omit the 3-byte period field.
* `bp = u16 @ base+2` — the number of voice-cells this position actually fills.
  (This is the authoritative "voices used" figure per position; e.g. `bp=4`,
  `bp=7`.)
* Following `bp`, a **32-bit code window** is loaded MSB-first. When flag
  `0x4000` is set, the number of header bytes consumed for the window is given by
  an alignment table indexed by `bp`:

  ```
  ALIGN_TAB[0..16] = 1,1,1,1,1, 2,2,2,2, 3,3,3,3, 4,4,4,4
  ```

  and the valid bits are pre-shifted to the top of the window.

### 9.3 Per-cell decoding (2-bit codes)

For each voice in turn, the top 2 bits of the window select an encoding, then the
window is shifted left by 2. The decoded cell is 8 bytes:

| Code | Bytes consumed | Cell contents                                                   |
|------|----------------|-----------------------------------------------------------------|
| 0    | 0              | empty cell (rest / no change)                                   |
| 1    | 2              | two parameter bytes → cell[5], cell[6]                          |
| 2    | 2              | two single bytes → cell[0], cell[4]                             |
| 3    | 4–7            | full event: first byte; then (if `!0x1000`) a 3-byte period; then note, effect command, effect param; then (if `!0x2000`) a param-2 byte |

The decoded 8-byte cell maps onto the per-voice runtime block. Field semantics
after decoding (offsets within the voice block):

| Byte | Meaning                                                                 |
|------|------------------------------------------------------------------------|
| note   | primary note (0 = rest)                                              |
| chord  | up to three additional chord notes                                   |
| instr  | instrument selector (file instrument index = selector − 2)           |
| cmd    | effect command                                                       |
| param  | effect parameter                                                     |
| dur    | duration (rows the cell sustains)                                    |

---

## 10. Effect commands

Effects run either once when a row is decoded ("row-side") or every tick
("per-tick"). Confirmed effects (`TRACKER.DRV` jump table `@0x115A`):

| Cmd    | When      | Effect                                                      |
|--------|-----------|------------------------------------------------------------|
| `0x01` | per-tick  | portamento up (period −)                                    |
| `0x02` | per-tick  | portamento down (period +)                                  |
| `0x03` | per-tick  | tone portamento (slide to target period)                   |
| `0x04`/`0x05`/`0x07`/`0x08` | row | map to `ADLIB.DEV` vtable stubs — state-only no-ops on Adlib |
| `0x06` | row       | note off (key-off all voices of the channel)               |
| `0x09` | row       | set speed (ticks/row), low 5 bits                          |
| `0x0A` | per-tick  | volume slide                                                |
| `0x0B` | row       | position jump (to order = param − 1)                        |
| `0x0C` | row       | set volume (0–0x7F)                                         |
| `0x0E` | row       | pattern break                                               |
| `0x0F` | row       | set tempo (timer Hz; clamped ≥ 0x13)                        |
| `0x1E` | per-tick  | arpeggio / strum                                            |
| `0x1F` | row+tick  | note retrigger (param hi = initial count, lo = reload)     |

Pitch periods are centred at `0x2000`; a semitone step is `0x155`, giving
`target = 0x2000 + (note − base_note) × 0x155`, masked to 14 bits.

---

## 11. Voice / channel model

* The engine's **voice count equals `track_count`** (`+0x12` in the block),
  capped at 16 (`REPLAY_MAX_VOICES`). The `0x80`-byte uncompressed row stride
  (16 slots × 8 bytes) reflects this 16-voice ceiling.
* Songs in the corpus use up to **16 tracks**, and 115 of 326 files actually
  fill more than 9 voice-cells. This 16-voice ceiling is a **tracker limit**,
  not a hardware channel count.
* The Adlib backend is a **single OPL2** (9 melodic channels; `ADLIB.DEV`
  references only I/O base `0x388`, never a second chip). When a song declares
  more tracks than the chip has channels, `ADLIB.DEV`'s dynamic voice allocator
  folds logical tracks onto the 9 physical channels (voice stealing). There is
  **no** dual-OPL2 / OPL3 support. The device codes are OSL `D_DEVICENUMBER`
  values (`0x02` = Adlib, `0x04` = Roland MT-32), **not** single- vs dual-chip
  markers (see §6.3).
* MIDI-family devices (LAPC-I / SCC-1) natively provide up to 16 channels, which
  is why the >9-voice songs cluster on `.RLD`/`.SCC`/`.LAP`.

---

## 12. What the driver reads vs ignores

Load-time resolution (`TRACKER.DRV @0x15C4`) hands the engine a pointer to
`record+6` (i.e. the variant base, §6.2). During playback, for the **Adlib**
device the only bytes consumed are:

* `length` at record `+0x00` (must be non-zero),
* transpose at variant `+0x18` and volume-cap at variant `+0x19`,
* the 11-byte OPL2 patch at the Adlib variant's payload (`variant +0x24`).

Everything else — the container descriptors, non-Adlib variant payloads,
`checksum`, `ver_c` — is either editor metadata, another device's data, or
unused by the Adlib replay path.

---

## Appendix A — Quick field reference

**Header**
```
0x00 char[4] "OSL1"     0x28 char[30] title
0x04 u8  version        0x48 u32 block_off
0x05 u16 constant       0x4C u16 instr_count
0x07 u8  generation
0x4E instrument pointer table: instr_count x u32 record offsets,
     records follow immediately at 0x4E + 4*instr_count.
     Pattern-cell selectors are 1-based: selector n -> table index n-1.
```

**Instrument record (container)**
```
0x00 u16 length (span-4)  0x06 u16 desc0 (=6)
0x04 u16 n_variants       0x08 u16 0
variants start at 0x04+desc0 (=0x0A for a single-variant record)
```

**Variant (offsets relative to variant base; single-var file off in parens)**
```
+0x00 char[20] name (0x0A)    +0x1A u8  device (0x24)  02=Adlib 04=Roland 08=SCC1
+0x16 s8  finetune (0x20)     +0x20 u32 paylen  (0x2A)
+0x18 s8  transpose (0x22)    +0x24 payload     (0x2E)
+0x19 u8  vol_cap  (0x23)
Adlib payload = 11-byte OPL2 patch. SCC-1 payload+0x02 = GM program.
```

**Pattern block**
```
0x00 char[16] subtitle  0x2A u16 tempo (Hz)
0x10 u8  restart_idx     0x2C u8  speed (ticks/row)
0x12 u16 track_count     0x4E u8  order_count
0x14 u16 row_count       0x50 u8[]  order table
0x16 u16[8] defaults     0x150 u32[] pos_ptr (by pattern)
0x26 u16 checksum
0x28 u16 ver_c
```

## Appendix B — Tools

* `src/osl1.c` / `src/osl1.h` — reference parser (loads a file into `Song`).
* `src/replay.c` / `src/replay.h` — replay engine (tick/order/effect decode).
* `tools/osl1_dump.c` → `osl1_dump.exe` — human-readable field dump for one file.
* `tools/osl1_scan.py` — corpus scanner: per-file instrument/synth/voice report,
  corpus summary, and CSV export (`--verbose`, `--csv <path>`).

## Appendix C — Confidence notes

* **Confirmed:** signature, header offsets, pointer table, the variant-container
  layout and device↔payload-length mapping (`med.asm` 0x25DC–0x26F2), device
  code `0x04` = Roland MT-32, OPL2 patch layout and register order, pattern-block
  core fields, position/cell codec, effect set, single-OPL2 hardware, driver
  read set.
* **Inferred:** container descriptor bytes (`02 00 12 00`) being inert,
  `defaults` rest sentinel `0x7F7F`, `0x81` = SNES sample device, corpus-class
  thresholds.
* **Unverified / open:** the `0xFFFF` sentinel at variant `+0x14`, `constant`
  (`+0x05`), `checksum`, `ver_c`, and the internal sub-structure of the Roland
  (Timbre Common + partials) and SCC-1 payloads beyond their headers.
