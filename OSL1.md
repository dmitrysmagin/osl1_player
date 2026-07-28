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
+0x0050  Instrument pointer table (see §5)
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
| +0x4E  | u16       | instr_size    | Nominal instrument-data size used for pointer validation.   |

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

## 5. Instrument pointer table (`+0x50`)

`instr_count` entries, **4 bytes each**, starting at file offset `0x50`.

| Entry offset | Type | Field           | Notes                                            |
|--------------|------|-----------------|--------------------------------------------------|
| +0x00        | u16  | (segment slot)  | Zero on disk; the driver fills a segment here at load. |
| +0x02        | u16  | record_offset   | File-absolute offset of the instrument record.   |

The driver resolves an instrument as a **far pointer** loaded from
`entry+0x02` (`les di, es:[bx*4 + table + 2]`), i.e. only the offset half is
meaningful on disk.

### Pointer validation (parser policy)

An entry is treated as a valid instrument when:

1. `record_offset > 0x4F` (clears the header), **and**
2. `record_offset + instr_size <= file_size` (record fits), **and**
3. `record_offset` is strictly greater than the previous valid entry's offset
   (monotonically increasing).

Entries failing these tests are placeholders/padding and are skipped. This
mirrors the reference dumper and cleanly rejects the stray non-song files.

---

## 6. Instrument records

Every record begins with a common header. The **record length** (`+0x00`) and
the **synth-type code** (`+0x24`) are 1:1 correlated across the whole corpus and
determine the record's total size and meaning.

### 6.1 Common header

| Offset | Type      | Field    | Notes                                                       |
|--------|-----------|----------|-------------------------------------------------------------|
| +0x00  | u16       | length   | Total record length. Fixed per synth type (see §6.3).       |
| +0x04  | u16       | p1       | Constant `0x0001` corpus-wide; never read by the driver. Inert metadata **(inferred)**. |
| +0x06  | u16       | p2       | Constant `0x0006` corpus-wide; never read by the driver. Inert metadata **(inferred)**. |
| +0x0A  | char[20]  | name     | Instrument name, NUL-padded.                                |
| +0x1E  | u8        | (runtime) | Consumed by the driver's *runtime* record after load; raw on-disk meaning not fully reversed **(unverified)**. Earlier drafts guessed "transpose" here — the real note transpose is `+0x22` (see below). |
| +0x1F  | u8        | vol_cap   | Volume ceiling in the driver's runtime record **(unverified)**. |
| +0x22  | s8        | transpose | **Signed per-instrument note transpose in semitones.** Added to the pattern note before the OPL note→F-number/block conversion. Confirmed against `COLUMBIA.ADL` vs its DRO capture (`LOGDRUM1` = −24, `"saw synth"`/`OBOE1` = +12): without it the drum kit renders two octaves too high. Corpus-wide 99.9% of values lie in ±36 semitones and 90.7% are exact octave multiples (0/±12/±24) **(confirmed)**. |
| +0x23  | u8        | (tuning?) | Almost always `0x7F`; likely a fine-tune/level centre **(unverified)**. |
| +0x24  | u8        | synth     | Synth-type code (see §6.2).                                  |

### 6.2 Synth-type codes (`+0x24`)

This is an **exclusive enum**, not a bitmask. A record's byte layout commits it
to exactly one type; FM and MIDI never combine within a single instrument (a
*file* may mix single-type instruments — that is what "Mixed" in §7 means). The
combined bit values (`0x06`, `0x0A`, `0x0E`) never occur in real records.

| Code   | Name       | Record len | Meaning                                                    |
|--------|------------|------------|------------------------------------------------------------|
| `0x02` | FM-short   | 58 (0x3A)  | Bare, "flattened" OPL2 instrument: header + name + one 11-byte OPL2 patch. |
| `0x04` | FM-ext     | 286 (0x11E)| Native MED FM instrument: identical header + the **same** 11-byte OPL2 patch, **plus** 228 bytes of MED editor data (see §6.4). |
| `0x08` | MIDI       | 170 (0xAA) | MIDI/program instrument: no FM patch; a GM program number at `+0x30`. |
| `0x81` | SNES sample| variable   | SNES sampled instrument (only in `.SNS` banks). Different, variable-length layout — out of scope for the FM/MIDI song path. **(inferred)** |

Any other value at `+0x24` (e.g. `0x00`, `0x09`, `0x43`, `0x47`) with a
non-matching length is a mis-parse of a placeholder/non-song file, not a real
type.

### 6.3 FM-short vs FM-ext: same sound, different storage

FM-short and FM-ext are **played identically** by `TRACKER.DRV` + `ADLIB.DEV`.
The driver never branches on the `+0x24` code; both types contribute only:

* `length` (`+0x00`), used to reject empty records,
* the runtime transpose/vol-cap fields, and
* the **11-byte OPL2 patch at `+0x2E`** (§6.5), uploaded verbatim.

Neither `TRACKER.DRV` nor `ADLIB.DEV` reads any record byte at or beyond offset
`0x3A` (confirmed by full-binary disassembly scan). Therefore:

* **FM-short (58 B)** is a stripped, patch-only export — everything the driver
  needs and nothing more.
* **FM-ext (286 B)** is a *superset*: the same playable patch plus the editor's
  source parameters. FM-ext is the dominant native form; FM-short is the
  exported/imported form.

For faithful OPL2 playback, reading the 11-byte patch at `+0x2E` is complete for
both types.

### 6.4 FM-ext editor tail (`+0x3A` … `+0x11E`)

228 bytes of MED-native instrument programming, used only by `MED.EXE`'s editor
(and, presumably, by the other device renderers). It contains command bytes and
envelope/sequence tables recognisable by run-filled regions of `0x32` (`'2'`)
and `0x64` (`'d'`). The exact sub-layout (volume sequence, arpeggio/waveform
sequence, vibrato, etc.) is only partially decoded and is **not required** for
Adlib playback **(unverified)**.

### 6.5 The 11-byte OPL2 patch (`+0x2E`, FM records)

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

A robust FM-vs-MIDI test (used by the parser) is to count non-zero bytes in this
11-byte patch: FM patches carry ~7–9 non-zero bytes; MIDI/program records carry
0–1. A threshold of ≥4 separates the two cleanly. (Caveat: SNES `.SNS` sample
records can false-positive here because sample data sits at this offset.)

### 6.6 MIDI record extras

| Offset | Type | Field   | Notes                                  |
|--------|------|---------|----------------------------------------|
| +0x30  | u8   | program | General MIDI program number (0–127).   |

For MIDI records the `+0x2E` region is empty (no OPL2 patch).

---

## 7. Heuristic renderability class

Because neither the extension nor the generation byte names the target, files
are classified by the instrument mix — specifically, whether instruments carry
usable OPL2 FM patches (i.e. what the Adlib backend can render):

| Class    | Condition                                              | Meaning                          |
|----------|-------------------------------------------------------|----------------------------------|
| Unknown  | no valid instruments                                   | nothing to render                |
| Adlib    | every valid instrument has an OPL2 FM patch            | fully OPL2-renderable            |
| Mixed    | some FM, some MIDI/program                              | partly renderable (MIDI = silent on OPL2) |
| MIDI     | only MIDI/program instruments                          | not OPL2-renderable              |

Corpus distribution (326 files): ~199 Adlib, ~61 Mixed, ~64 MIDI, ~2 Unknown.

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
  **no** dual-OPL2 / OPL3 support, and the synth codes `0x02`/`0x04` are **not**
  single- vs dual-chip markers (see §6.3).
* MIDI-family devices (LAPC-I / SCC-1) natively provide up to 16 channels, which
  is why the >9-voice songs cluster on `.RLD`/`.SCC`/`.LAP`.

---

## 12. What the driver reads vs ignores

Load-time resolution (`TRACKER.DRV @0x15C4`) hands the engine a pointer to
`record+6`. During playback the only record bytes consumed are:

* `length` at `+0x00` (must be non-zero),
* transpose at runtime `+0x1E` and volume-cap at `+0x1F`,
* the 11-byte OPL2 patch at `+0x2E` (Adlib backend).

Everything else — `p1`/`p2`, the FM-ext editor tail, `checksum`, `ver_c` — is
either editor metadata or unused by the Adlib replay path.

---

## Appendix A — Quick field reference

**Header**
```
0x00 char[4] "OSL1"     0x28 char[30] title
0x04 u8  version        0x48 u32 block_off
0x05 u16 constant       0x4C u16 instr_count
0x07 u8  generation     0x4E u16 instr_size
0x50 instrument pointer table (4 bytes/entry; offset @entry+2)
```

**Instrument record (FM)**
```
0x00 u16 length         0x22 s8  transpose (semitones, signed)
0x04 u16 p1 (inert)     0x24 u8  synth (0x02/0x04)
0x06 u16 p2 (inert)     0x2E [11] OPL2 patch
0x0A char[20] name      0x3A.. FM-ext editor tail (0x04 only)
0x1E u8  (runtime)      0x1F u8  vol_cap (runtime)
```

**Instrument record (MIDI, 0x08)**
```
0x00 u16 length (170)   0x24 u8 synth (0x08)
0x0A char[20] name      0x30 u8 GM program
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

* **Confirmed:** signature, header offsets, pointer table, synth-code↔length
  mapping, OPL2 patch layout and register order, pattern-block core fields,
  position/cell codec, effect set, single-OPL2 hardware, driver read set.
* **Inferred:** `p1`/`p2` being inert, `defaults` rest sentinel `0x7F7F`,
  `0x81` = SNES sample type, corpus-class thresholds.
* **Unverified / open:** raw on-disk meaning of record `+0x1E`/`+0x1F` before
  MED's load transform, `constant` (`+0x05`), `checksum`, `ver_c`, and the exact
  sub-structure of the FM-ext editor tail.
