# The `.ALB` Runtime Export Format (`20 AD 01`)

A clean-room, byte-level description of the **`20 AD 01`** music format written
by Ocean Software's tracker as a *runtime export*. Files in this format always
carry the extension `.ALB`. Sixteen distinct songs survive, across five projects
dated September 1991 to 1992 — `PIT`, `LETHAL3`, `CHAOS`, `CRUSADE` and
`UTOPIA`.

This document is self-contained: everything needed to read an `.ALB` file is
here. The reference implementation is `src/oldalb.c`, reached when
`oldfmt_generation()` returns `OLDFMT_GEN_ALB`; the handful of mechanics it
shares with the `B4`/`B6` loader live in `src/oldfmt.c`. Where a field's
meaning is confirmed it is stated plainly; where it is inferred or unverified
it is marked **(inferred)** or **(unverified)**.

> **Everything below is confirmed against the driver that plays it.**
> `MEDIT/LAPMUSIC/OLDMUSIC/OLDMUSIC/PIT/ADLIB/` holds `ADLIB.EXE` — "ADLIB
> DRIVER (Version 3.00)", Imagitec Design Ltd., September 1991 — together with
> its API documentation (`README.DOC`), two `.ALB` files it plays, and the MASM
> source of the harnesses that load them (`../MUSIC.ASM`, `../FX.ASM`). The
> format was originally reversed from the files alone; the driver has since
> corroborated the container arithmetic (§5–§6), the presence-mask row encoding
> (§7), the five percussion slots (§8) and the effect table (§10), in every case
> exactly. Citations are offsets into the **driver body**, which begins at file
> offset `0x200` — `ADLIB.EXE` is a flat binary behind a 32-paragraph MZ stub,
> and `MUSIC.ASM` loads it and calls `segment:0x200`. See §11.
>
> **Updated 2026-08.** The driver is now disassembled and annotated in full, in
> `../PIT_ADLIB.EXE.annotated.asm`. The build shipped with its Borland debug
> block still attached, so that listing carries the author's own 245 label names
> and 1185 `ADLIB.ASM` line numbers. Where this document previously named a
> routine by address it can now name it outright, and several claims made from
> partial reading have been corrected here — notably the effect table's per-row
> and per-tick columns (§10.1), the belief that vibrato was implemented (it is
> not), and the driver's relationship to `BSSJS/ADLIB.DRV` (§11).

## Where this format sits

`.ALB` is the third and last of the three pre-`OSL1` generations. The other two
— `B4 9A 01` and `B6 9A 01`, both usually `.RLD` — are editor working files and
are specified in **`RLD.md`**. The newer `"OSL1"` container is specified in
**`OSL1.md`**. `README.md` sets all three old generations side by side under
*"The three pre-OSL1 generations"*.

`.ALB` reuses the `B4`/`B6` header prefix and paragraph-addressing scheme, but
**not** their pattern encoding, and it is **not an editor working file**. Every
structure that exists purely to serve the editor is dropped or trimmed to fit:

* the 256-byte instrument blocks are **gone entirely** — only the compact
  64-byte Adlib editor records survive, and there are exactly as many of them as
  the song uses;
* the cue table is cut from a fixed 128 entries to however many the song has;
* the paragraph table is cut from a fixed 256 entries to `pattern_count + 1`;
* the pattern encoding is replaced with one that is simpler to decode and
  slightly larger on disc.

Taken together that reads as a **"save for the game" pass**: it discards editor
state, keeps only what a player needs, and trades a little size for a decoder
with no variable-length payloads in it.

Curiously the format takes its 32-slot instrument table from `B4` but its
`track_count` placement at `+0x118` from `B6`, leaving `+0xD8`–`+0x117` as a
permanent hole. It is a hybrid of the two, not a successor to either.

---

## 1. Conventions

* **All multi-byte integers are little-endian.**
* Offsets written `+0xNN` are relative to the start of the enclosing structure
  (file or record). "File-absolute" means relative to byte 0 of the file.
* Types: `u8`, `u16` = unsigned 8/16-bit; `char[N]` = fixed-width, NUL-padded
  ASCII (not necessarily NUL-terminated when full).
* A **paragraph** is 16 bytes, the DOS segment granularity this format uses for
  all of its pattern-stream arithmetic.
* Sizes and offsets use hexadecimal; counts use decimal.

---

## 2. Provenance and the corpus

| Project | Files | Date |
|---|---|---|
| `LAPMUSIC/OLDMUSIC/PIT/ADLIB` | `ADLIB`, `ADLIBFX` | Nov 1991 |
| `LAPMUSIC/LETHAL3` | `ING4`, `PCGO`, `PCMOD` | 1992 |
| `LAPMUSIC/OLDMUSIC/CHAOS` | `ADLIB2` | 1992 |
| `LAPMUSIC/OLDMUSIC/CRUSADE` | `CRUSFX`, `DEATH`, `ING`, `MEDAL`, `TITLE` | 1992 |
| `LAPMUSIC/OLDMUSIC/UTOPIA` | `ING1`, `ING2`, `ING3`, `TITLE`, `UTOFX` | 1992 |

**16 distinct files by content**, 22 paths. `OLDMUSIC/` contains a nested copy
of itself and `medplay/test/` is a working duplicate, so the path count is far
higher; all statistics below are over the 16.

> **Corrected 2026-07.** An earlier revision said 14 files across four 1992
> projects. It missed `PIT/ADLIB/`, which lives only in the *nested*
> `OLDMUSIC/OLDMUSIC/` copy and was never mirrored into `medplay/test/`. The
> two files there are the oldest `.ALB` in the corpus (6 Nov 1991) and they ship
> alongside the driver that plays them, which is why the omission mattered: the
> "no surviving loader" claim it led to was wrong. Both decode cleanly under the
> model of §7 — 0 boundary misses, 0 out-of-range cells — which makes them a
> genuine hold-out set, since the model was derived without them.

Three of the titles at `+0x03` name a **`.MOD` source file** — `pcing4.mod`,
`pcgo.mod`, `pcmod.mod` — which places the variant alongside the Amiga ports and
is further evidence that these are exported deliverables rather than
compositions in progress.

`MED.EXE` cannot open any of them: its magic test at `med.asm:0x2364` is a bare
`cmpw $0x9ab6`, so only `B6` reaches the editor.

---

## 3. Top-level layout

```
  +0x0000  Magic + song name                                  (§4)
  +0x0018  Pattern order table, 128 x u8                      (§4.1)
  +0x0098  Instrument presence/volume table, 32 x 2 bytes     (§4.2)
  +0x00D8  Permanent zero gap
  +0x0118  Track count, restart, n_instr, n_cue               (§4)
  +0x0158  Cue table, n_cue x 16 bytes                        (§5)
    para   Pattern paragraph table, pattern_count + 1 x u16   (§6)
    ...    Pattern stream                                     (§7)
    ...    Adlib editor records, n_instr x 64 bytes, to EOF   (§9)
```

where

```
para = 0x158 + 16 * n_cue
```

Unlike `B4`/`B6`, whose every structure sits at a fixed offset, **everything
from `+0x158` onward is data-dependent**: both the cue table and the paragraph
table are sized from header counts. There is no fixed pattern-stream base.

---

## 4. File header

Identical to `B4` up to `+0x118`, then it diverges. The instrument slot table is
**32 entries** as in `B4`, so it ends at `+0xD8`; but unlike `B4` the track count
does *not* follow immediately — it sits at `+0x118`, at the `B6` offset, leaving
`+0xD8`–`+0x117` as a permanently zero gap (0 of 16 files have a non-zero byte
in it).

| Offset | Type      | Field       | Notes                                                     |
|--------|-----------|-------------|-----------------------------------------------------------|
| +0x000 | u8[3]     | magic       | `20 AD 01`.                                               |
| +0x003 | char[20]  | name        | Song name, NUL-padded. Empty in 6 of 16.                  |
| +0x017 | u8        | —           | **Not** part of the name — uninitialised editor scratch, non-zero in 1 file **(inferred)**. |
| +0x018 | u8[128]   | order       | Pattern order table (§4.1). Order length 2–55 observed.   |
| +0x098 | u8[32][2] | instr_slots | Per-slot `[present][volume]` (§4.2). **Only the first `n_instr` entries are meaningful** — see §4.3. |
| +0x0D8 | —         | —           | Zero in every file, to `+0x117`.                          |
| +0x118 | u8        | track_count | Melodic voices per row. 4, 5 or 6 observed. **Excludes** the five percussion slots (§8). |
| +0x119 | u8        | restart_idx | Order position to loop back to, 0–4 observed, always inside the order. Interpretation as a loop target is **(inferred)** from its range alone. |
| +0x11A | u8        | n_instr     | Number of 64-byte editor records at end of file. 2–24 observed. |
| +0x11B | u8        | n_cue       | Entries in the cue table, **including its `"Not Used"` sentinel**. 1, 2, 11 or 25 observed. |
| +0x11C | —         | —           | Zero in every file, to `+0x157`.                          |
| +0x158 | ×`n_cue`  | cue_table   | 16 bytes each (§5).                                       |

`track_count` is read by the driver at `[es:0xdd7]` = `0xCBF + 0x118` (body
`0x055F`), confirming the odd `B6`-style placement despite the `B4`-style 32-slot
table.

### 4.1 The order table (`+0x18`)

128 bytes, each the number of the pattern to play at that position. There is no
stored length: the song's order length is **the index of the last non-zero byte,
plus one** — position 0 is always played, so a table that is entirely zero still
yields a one-position song. The pattern count is the highest value in that range,
plus one.

This does mean a song cannot deliberately end on pattern 0. That is a real
limitation of the format, not of the reader.

### 4.2 The instrument presence/volume table (`+0x98`)

32 two-byte entries, ending at `+0xD8`:

| Offset | Type | Field   | Notes                                                       |
|--------|------|---------|-------------------------------------------------------------|
| +0x00  | u8   | present | 1 = this slot is in use; 0 = unused. Only ever 0 or 1.      |
| +0x01  | u8   | volume  | Slot default volume, `0`–`0x40` (0–64 decimal). `0x40` is the observed maximum and the commonest value. |

Unlike `B4`/`B6`, this table does **not** determine the file's layout — there are
no per-slot instrument blocks to locate. A slot's index is still its identity:
pattern cells select instruments by slot number.

#### Default volume

The volume byte is the instrument's **default note-on volume**. The driver
**doubles it** into the replay engine's 0–0x7F range and clamps:

```
def_volume = min(slot_volume * 2, 0x7F)
```

The stored scale is **0–64**, not 0–63, which is exactly why the clamp to `0x7F`
exists. A `0Ch` (set volume) riding on the same row **overrides** the default
rather than scaling it — the driver's cell handler does this explicitly (§7).

`src/oldalb.c` parses this into `Instrument.def_volume` and `src/replay.c`
applies it at note-on, preferring an explicit `0Ch` when one is present.

### 4.3 The slot table is stale past `n_instr`

`present_count` and `n_instr` disagree in 11 of the 16 files, and in both
directions — `UTOPIA/TITLE.ALB` declares 4 records while leaving `present = 1`
in all 32 slots, and `UTOPIA/ING3.ALB` declares 24 records with only 11 slots
flagged present. The export evidently rewrites the counts without clearing the
table it inherited from the editor.

The rule that works: **take `n_instr` as the record count, and read the presence
flag and default volume only for slots below it.** Within that range the flag is
reliable — across all 16 files, no pattern cell ever selects a slot whose
presence flag is 0.

Do **not** substitute "has a blank name" for the presence flag. Three files
(`LETHAL3/ING4.ALB` slots 6 and 9, `CRUSADE/ING.ALB` slot 2) actively play
instruments whose name field is empty — the composer simply never named them.

---

## 5. Cue table (`+0x158`)

`n_cue` entries of 16 bytes, immediately followed by §6's paragraph table. The
entry layout is the same as `B6`'s fixed 128-entry table (`RLD.md` §5), but the
count is variable and the positions are **0-based** where `B6`'s appear to be
1-based:

| Offset | Type     | Field     | Notes                                            |
|--------|----------|-----------|--------------------------------------------------|
| +0x00  | char[10] | name      | Cue name, NUL-padded. `"Not Used"` when free.    |
| +0x0A  | u8       | start_pos | First order position of the cue, 0-based **(inferred)**. |
| +0x0B  | u8       | end_pos   | Last order position of the cue, 0-based **(inferred)**. |
| +0x0C  | u8       | flag_a    | Only 0 or 1 observed. Purpose unknown **(unverified)**. |
| +0x0D  | u8       | flag_b    | Only 1 or 2 observed. Purpose unknown **(unverified)**. |
| +0x0E  | u16      | —         | Zero in every observed entry.                    |

The last entry is always the `"Not Used"` sentinel, so the live cue count is
`n_cue − 1`.

This is the format's **named sound-effect / sub-song list**. Twelve of the 16
files have exactly one live cue spanning the whole song (`start_pos == 0`,
`end_pos == order_length − 1`) — the song's own name. The exceptions are the
sound-effect banks, one cue per order position:

* `CRUSADE/CRUSFX.ALB` — 24 cues over 25 patterns: `ARMOUR`, `THUD`, `PSYCHIK`,
  `FUZZ`, `ALARM`, `BUTTON`, `FOOTSTEP`, `DOOR`, `PICKUP`, `GRENADE`,
  `EXPLOSION`, `CHAOS`, `LAZER`, `MISSILE`, `SCANNER`, `BLIP`, `RICOCHET`,
  `FIRE`, `COMMANDER` …
* `UTOPIA/UTOFX.ALB` — 10 cues, `LANDEXP`, `MISSILE` …

`oldalb.c` parses none of it; a player that wanted to expose individual cues
would start here.

> **Confirmed from Ocean's own code.** `MUSIC.ASM`'s `dump_titles` routine walks
> the loaded file from **`si = 158h`, printing 10 characters per entry with
> `add si,16-10`** — the offset, the stride and the name width this section
> gives, printed by the developer's own diagnostic.

---

## 6. The paragraph table

Immediately after the cue table, at

```
para = 0x158 + 16 * n_cue
```

sit exactly **`pattern_count + 1` `u16` entries**, padded up to a 16-byte
boundary and nothing more. Each entry is a **paragraph offset relative to the
table's own file offset**:

```
pattern_file_offset = para + 16 * table[i]
```

Entry `pattern_count` marks the end of the pattern stream — i.e. the start of
the instrument records (§9). That entry is what makes the records locatable
without decoding anything.

Three properties were checked on all 16 files and hold exactly:

* `table[0] == ceil((pattern_count + 1) * 2 / 16)` — the pattern stream starts
  at the first paragraph boundary past the end of the table itself, with no
  fixed reservation and therefore **no stale tail**;
* every pattern decodes to precisely the byte range the table delimits;
* `filesize − (records_off + 64 × n_instr) == 0` — **zero slack**, so `n_instr`
  is corroborated independently by the file size.

> **No stale entries.** This is a real difference from `B4`/`B6`, whose 256-entry
> tables are editor working state that is not truncated on save and whose tails
> can point past the end of the file (`RLD.md` §6.1). Here the table is sized to
> the song, so the warning does not apply.

> **Confirmed from the driver.** `ADLIB.EXE` v3.00's "initialise tune data" call
> (`INT 60h`, `AX = 2`; body offset `0x00E9`, reached through the dispatch table
> at `0x008E`) performs precisely this arithmetic, with `DS` set to the music
> segment by the caller:
>
> ```
> 00F6  mov di,MAIN_DATA / mov cx,0x158 / rep movsb  ; header copy is 0x158 bytes
> 010B  mov cl,[SFXNUMBER]                        ; MAIN_DATA+0x11B = n_cue
> 0111  shl cx,1 x4 / add si,cx                   ; si = 0x158 + 16 * n_cue
> 011B  mov cx,[PATTERNNUMBER] / shl cx,1         ; (pattern_count) * 2
> 0121  and cx,-0x10 / add cx,0x10                ;   rounded up to a paragraph
> 0128  mov di,OFFSETS / rep movsb                ; copy the paragraph table
> 012D  mov [es:PATTERNADDR],si                   ; si now = pattern stream base
> ```
>
> The labels are the author's own, recovered from the driver's appended Borland
> symbol table: `MAIN_DATA` = `0x0CBF` (the header copy), `SFXNUMBER` = `0x0DDA`,
> `PATTERNNUMBER` = `0x15A4`, `OFFSETS` = `0x0E47` (the paragraph table),
> `PATTERNADDR` = `0x15A6`, and alongside them `VOICENUM` = `0x0DD7`
> (`track_count`), `PATTERNMAP` = `0x0CD7` (the order table) and `INSTINFO` =
> `0x0D57` (the presence/volume table). The routine's own name is `AFTER_LOAD`.
>
> Three things fall out of this. The header copy is `0x158` bytes, i.e. exactly
> up to the cue table, versus `0x118` in the 1991 `ADLIB.DRV` — the two drivers
> disagree on the header length by the same amount the two formats do. `n_cue`
> is read from `+0x11B` and multiplied by 16, fixing both the field and the
> entry size. And the table copy length is `((pattern_count × 2) & ~15) + 16`,
> which is the paragraph-padded size of `pattern_count + 1` `u16` entries — so
> the driver takes the **end of the padded table** as the pattern-stream base
> and never consults `table[0]` for it. That is a stronger statement than the
> `table[0]` identity above: the format *cannot* carry a stale tail, because the
> driver would read the tail as pattern data.

---

## 7. Pattern encoding — a presence mask, not a code word

Every pattern is **exactly 64 rows** and is padded to a 16-byte boundary of its
own byte count:

```
pad = (16 - (bytes_in_pattern & 15)) & 15
```

A reader that indexes through §6's table never needs to compute the pad — each
pattern's start is stated outright, and the padding is simply the slack before
the next stated start. The rule is documented because it explains the table's
existence and because a *writer* must reproduce it.

The row encoding is where `.ALB` parts company with every other format in this
family:

```
u16 mask                          ; one bit per channel slot, MSB first:
                                  ;   bit 15 = slot 0
<4-byte cell> x popcount(mask)    ; in ascending slot order
```

There are no variable payloads and no per-track code values. A row is always
`2 + 4 × popcount(mask)` bytes, and a slot is either silent (bit clear, nothing
stored) or carries a full cell.

| Offset | Field      | Notes                                                    |
|--------|------------|----------------------------------------------------------|
| +0x00  | note       | 0 = no note; otherwise 17–96 observed. Note 12 is C-1 — the driver indexes its 96-entry note table (`FNUMNOTES`, `0x1203`) with `note − 0x0C`. |
| +0x01  | instrument | **1-based** selector: `n` means slot `n − 1`, and 0 means "no instrument change". |
| +0x02  | effect_cmd | 0–15, the usual MOD-like set (§10) — no remapping.       |
| +0x03  | effect_par | Effect parameter.                                        |

These map onto the replay engine's 8-byte cell as `cell[0]`, `cell[4]`,
`cell[5]`, `cell[6]` — the same four slots `B4`/`B6`'s code-2 and code-3
payloads write, which is why one unmodified replay engine plays both.

> **How the encoding was established.** Length validation alone cannot
> distinguish this model from a 2-bit-per-track one, because under every
> plausible pairing the row length comes out as `4 × popcount`. What settled it
> was profiling the payload bytes: under the 2-bit reading, "code 3" payloads
> showed the note and instrument fields *duplicated* in both halves — the
> signature of two independent cells being read as one. One bit per slot, four
> bytes per cell, is the reading under which every byte has exactly one meaning.
>
> **Confirmed from the driver (2026-07).** The statistical argument above is now
> redundant: `ADLIB.EXE` v3.00's row reader is explicit about it. At body offset
> `0x054C` it does a single `lodsw` to take the mask into `DX`, decodes
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
> hardcoded `mov cx,5`. That is §7 and §8 in six instructions.
>
> The cell handler `AD_PLAYVOICE` at `0x05C6` reads `[si+0]` note, `[si+1]`
> instrument, `[si+2]` effect, `[si+3]` parameter, decrements the instrument and
> indexes `INSTINFO` at `0xCBF + 0x98` to fetch the default volume — confirming
> the field order, the 1-based selector and §4.2's volume rule. It then does
> `cmp byte [si+0x2],0x0C` and, on a match, substitutes `[si+3]` for the default
> volume and zeroes the effect: `0Ch` **overrides** the slot default.
>
> **The note table.** `FNUMNOTES` at `0x1203` is 96 words = 12 notes × 8
> octaves, each word encoded `(octave << 12) | fnum` — note the shift, which is
> *not* the OPL2's own `block << 10`; the driver splits the word and re-packs it
> for `$A0`/`$B0` in `SETFREQ`. Octave 0 is
> `0157 016C 0181 0198 01B1 01CB 01E6 0203 0222 0243 0266 028A`, and each higher
> octave is the same twelve values with `0x1000` added per step. The portamento
> handlers clamp against the same constants (`0x0157` low, `0x028A` high, with
> `0x02AE` and `0x0142` as the slide limits).

### 7.1 Validation

`tools/alb_cells.py` decodes every pattern of all 16 files: **32 493 cells**,
every pattern landing exactly on its stated table boundary, every note in 17–96,
and no cell entirely empty (a set mask bit always carries something).

One cell in one file breaks the instrument-selector bound: `PIT/ADLIBFX.ALB`
selects instrument 12 where `n_instr` is 4. That is a data defect rather than a
decode failure — the file is a scratch bank whose first record is named `TEST1`,
its record count is corroborated by a zero-slack file size, and the driver would
simply index past the array. That last point is now confirmed rather than
assumed: `GET_TIMBRE` (`0x09BF`) multiplies the instrument number by 64 with six
unrolled shifts and adds it to `INSTADDR` **with no bound against `n_instr`**, so
an out-of-range selector reads 30 bytes of whatever follows the bank and plays
it as a patch. Ugly, but not a crash — which is why the file survives decoding.
Every other selector in the corpus is in range.

Effect usage is dominated by the same two commands as the older generations —
`0x0C` set volume (8563 cells) and `0x0A` volume slide (1859) — with `0x0F` set
speed (292) and a long tail of `0x01`–`0x0E`. 20 547 of the 32 493 cells carry
no effect at all, i.e. a bare note. Unlike `B6`, **no `.ALB` cell carries an
out-of-range note, selector or effect command at all.**

---

## 8. The five percussion slots

**An `.ALB` row has `track_count + 5` slots, not `track_count`.** The five extra
are the OPL2 rhythm-mode percussion channels, in the driver's own order:

| Slot | Voice | Editor rhythm code (§9, record `+0x26`) |
|---|---|---|
| `track_count + 0` | bass drum | 6 |
| `track_count + 1` | snare drum | 7 |
| `track_count + 2` | tom-tom | 8 |
| `track_count + 3` | cymbal | 9 |
| `track_count + 4` | hi-hat | 10 |

The driver puts the OPL2 into **permanent percussion mode** — `$BD` bit 5 set
during init and never cleared — so the chip offers 6 melodic voices plus 5
percussion voices rather than the usual 9 melodic. The percussion voices share
channels 7–9's operators and are keyed on by `$BD` bits 0–4 rather than by the
usual `$B0` key-on, so they cannot be driven through the melodic path at all.

Besides the driver's literal `mov cx,5` (§7), two independent corpus-wide lines
of evidence:

1. **No mask bit at or above `track_count + 5` is ever set.** Across all 16 files
   the highest slot used is exactly `track_count + 5` in the busy songs (6+5=11
   in `ING4`, `ADLIB2`, `ING`, `TITLE`, `ING3`; 4+5=9 in `CRUSFX`, `DEATH`;
   5+5=10 in `UTOFX`) and below it in the sparse ones. Never above.
2. **The instruments played in slot `track_count + k` carry rhythm code `6 + k`.**
   Checking every instrument that appears in a percussion slot gives **32
   agreements and 1 disagreement** — the exception being `CHAOS/ADLIB2.ALB`,
   which plays `BDRUM1` (code 6) in the snare slot. The names line up with the
   codes throughout: `BDRUM1` in the bass-drum slot, `SNARE1` in the snare slot,
   `WHITENOISE` in the cymbal slot, `HIHAT1`/`HIHAT2` in the hi-hat slot.

This is the cleanest confirmation in the whole corpus that the editor's rhythm
codes mean what they say — the export has baked the mapping into the row layout
itself.

Two facts from the v3.00 disassembly sharpen the picture:

* **The five percussion voices are global, not per-track.** The driver has
  eleven per-voice `MTSTRUC` records, `MT_VOICE1`–`MT_VOICE11` at `0x1047`
  onwards on a 22-byte stride. `DECODE_LINE` walks the melodic ones per track
  but reaches the last five with a fixed `mov cx,5 / mov di,MT_VOICE7`, so
  **every track shares the same five percussion voices.** Whichever track writes
  last wins. This is why `PIT/ADLIBFX.ALB` — a sound-effect bank meant to play
  over a song — carries no percussion at all: it could not, without stealing the
  music's drums.
* **The tom-tom and top cymbal never receive a pitch.** `SETFREQ` (`0x0AC3`)
  guards its `$A0`/`$B0` write with `cmp bp,8 / jc`, rejecting channel 8 — and
  channel 8 is precisely the one the tom-tom and top cymbal share in rhythm
  mode. The correct bound is 9, since channels 0–8 exist; the author appears to
  have meant to exclude the two pseudo-voices above channel 8 and got the
  comparison off by one. So both drums sound at whatever F-Number and block are
  in the shadow registers — F-Number 0, block 0, from init — and the deliberate
  `push bp / mov bp,8 / call SETPERCFREQ` at `0x0A43` is dead weight. The note
  byte in those two slots is read, stored and then thrown away.
  A consequence worth stating plainly: `oldalb.c`'s melodic emulation of those
  two slots is arguably **more** musical than the hardware original, because it
  actually pitches them. See defect 18 in `../PIT_ADLIB.EXE.annotated.asm`.

The rhythm-mode bit is also never turned off. `$BD` bit 5 is set by
`INIT_ADLIB_IO_MAP`'s table (`0xBD` → `0x20`) and no code path clears it, not
even the driver's own shutdown, so a program that stops the music and then
drives the OPL2 itself inherits a chip with only 6 melodic channels.

### Not emulated, but voiced

`src/opl_dev.c` is melodic-only, so `oldalb.c` reports the row width as
`track_count + 5` and lets all eleven slots play as ordinary melodic voices.
That is exact for the bass drum, which in OPL2 rhythm mode really is a normal
two-operator voice, and approximate for the other four, which in hardware share
operators and are keyed through `$BD` rather than `$B0`.

Note the difference from `B4`, where a percussion instrument is parsed and then
**not** voiced at all: here it *is* voiced, just melodically. `osl1_dump`'s
`rhythm instr` line spells out which case a given file is in. Emulating it
properly means teaching `opl_dev` a rhythm-mode allocation policy, which is a
device-layer change rather than a format one, and is the largest known gap in
`.ALB` playback fidelity.

---

## 9. Instruments

`.ALB` has **no 256-byte instrument blocks**. Its only instrument data is
`n_instr` **64-byte Adlib editor records**, one per slot in slot order including
blank ones, running from `para + 16 × table[pattern_count]` to EOF with no
slack. These are the same records `B4` carries as an end-of-file bank
(`RLD.md` §7.4), and they hold the *editor's* parameter fields rather than OPL2
registers — the driver assembles registers from them at instrument-init time.

### Record layout (64 bytes)

| Offset | Type     | Field       | Notes                                       |
|--------|----------|-------------|---------------------------------------------|
| +0x00  | char[10] | name        | Instrument name, NUL- or space-padded. May legitimately be empty (§4.3). |
| +0x0A  | u8[13]   | mod_fields  | Modulator editor fields — see below.        |
| +0x17  | u8[13]   | car_fields  | Carrier editor fields, same order.          |
| +0x24  | u8       | wave_mod    | Modulator waveform select, 0–3 (`$E0`).     |
| +0x25  | u8       | wave_car    | Carrier waveform select, 0–3 (`$E0`).       |
| +0x26  | u8       | rhythm      | 0 = melodic; 6 = bass drum, 7 = snare, 8 = tom, 9 = cymbal, 10 = hi-hat (§8). **Range-check it** — see below, and note that *v3.00 itself never reads it*. |
| +0x27  | —        | —           | Unused tail.                                |

> **v3.00 ignores the rhythm byte.** `GET_TIMBRE` (`0x09BF`) copies 30 bytes
> from `INSTADDR + instrument × 64 + 10` into the scratch buffer `TEMPTIMBRE`
> (`0x12C3`), so the byte lands at `TEMPTIMBRE+28` = `0x12DF` — and nothing in
> the driver ever reads `0x12DF`. Percussion is decided **purely by slot
> position**: `MT_SET_VOICE_TIMBRE` branches on the voice number, sending voices
> 0–5 down the two-operator melodic path, voice 6 down the two-operator bass-drum
> path, and voices 7–10 down `SET_PERC`. The field is therefore documentation of
> the composer's *intent* (which is exactly what makes it useful evidence in §8),
> not something the driver acts on. `CHAOS/ADLIB2.ALB` — the one file in the
> corpus whose rhythm codes disagree with its slot placement, playing `BDRUM1`
> (code 6) in the snare slot — therefore gets a snare, not a second bass drum.
> The slot wins.

### The 13 editor fields

The order is fixed by the driver's own field table. It appears in both drivers:
in `ADLIB.DRV` at `../BSSJS_ADLIB.DRV.annotated.asm:1493`–`1722`, and in v3.00
as `SET_PARAMS` (`0x08CF`), reached from `MT_SET_VOICE_TIMBRE` (`0x0878`).

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
| 8 | LEVEL | `$40` bits 0–5, **inverted** (`63 − v`); volume-scaled on the carrier and on percussion, *not* on the modulator |
| 9 | AM | `$20` bit 7 |
| 10 | VIBRATO | `$20` bit 6 |
| 11 | KSR | `$20` bit 4 |
| 12 | CONNECTION | computed into `$C0` bit 0, then **discarded** by an `and al,0xFE` — the driver forces FM (serial) connection on every channel. |

Sustain and level are stored the way a *musician* reads them (bigger = louder,
bigger = more sustain) and inverted into the OPL2's attenuation convention on
the way out. This is the single most common way to get an FM patch audibly
wrong, and it is why a naive byte-copy of these records produces silence.

**Only the carrier's level is scaled by volume.** v3.00 has three separate level
writers, and they do not behave alike:
`SET_MODULATOR_LEVEL` (`0x0963`) writes `0x3F − LEVEL` straight out, while
`SET_CARRIER_LEVEL` (`0x0947`) and `SET_PERC_LEVEL` (`0x097C`) route through
`CALC_FRAC` (`0x09B4`), which computes `0x3F − (LEVEL × volume / 0x3F)`. That is correct FM practice — scaling
the modulator would change the timbre rather than the loudness — but it means a
volume of 0 does *not* silence a channel whose modulator is loud enough to be
audible on its own, and it is the reason a channel's apparent volume range
depends on its patch.

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

`src/oldfmt.c` implements this as `oldfmt_editor_ops_to_adl()`, shared with the
`B4` editor bank, which uses the same record.

### Range-check the rhythm field

**Do not treat "non-zero" as "percussion".** Even live records carry
out-of-range scratch values sitting on the editor's 0–100 percentage scale. The
driver's own dispatch only recognises 6–10; accept that range and treat
everything else as melodic, which is what `oldfmt_rhythm_code()` does.

`B4`'s whole-bank blankness test does **not** apply here — these records are not
a reserved bank but the file's only instrument data, and blankness must be
judged per slot via §4.3's presence flag.

### `--fm-source` has no effect

There is no second instrument source to choose between, so `oldalb.c` always
reports `editor`. `--fm-source` is an `oldrld.c` option and does not reach here.

---

## 10. Timing and effects

**Tempo and speed are not stored.** The song runs at **50 Hz**, 6 ticks per row
by default, overridden only by an in-pattern `0x0F`.

> **Documented by the authors.** `PIT/ADLIB/README.DOC` — Imagitec's own API
> manual for this driver — states it outright under the initialisation
> instructions: **"Note driver runs at 50hz."**

The rate is hard-coded rather than derived. The driver's "internal interrupts
on" call (`AX = 0x0A`) does `mov ax,0x5D37` and programs the PIT with it (body
`0x0335`); `0x5D37` = 23863, and 1 193 182 / 23863 = **50.0013 Hz**. Turning
interrupts off writes divisor 0, restoring the BIOS 18.2 Hz. Nothing between
those two points touches the timer, and **no file field reaches it.**

> Note that the *song* runs at 50 Hz but the BIOS does not: `CLKINT`
> (`0x0347`) chains `INT 08h` only on every third tick, so the DOS
> time-of-day clock is fed at 16.67 Hz rather than 18.2 Hz and loses about 8.5%
> for as long as music is playing. See defect 19 in
> `../PIT_ADLIB.EXE.annotated.asm`.

### 10.1 The v3.00 effect table

Two 32-entry dispatch tables of words, both indexed by `effect & 0x1F`:
`ROUTINES` at `0x1307`, dispatched by `AD_CHECKCOM` (`0x061E`) **every tick**,
and `ROUTINES2` at `0x1347`, dispatched by `AD_CHECKCOM2` (`0x0639`) **once per
row**. `NULLROUT` (`0x0647`) is the bare-`ret` stub that fills every unused
slot in both.

| Cmd | Per-tick (`ROUTINES`) | Per-row (`ROUTINES2`) | Meaning |
|---|---|---|---|
| `0x00` | `0x0648` | stub | arpeggio — **also a bare `ret`** (see below) |
| `0x01` | `0x06DE` | stub | portamento up |
| `0x02` | `0x0708` | stub | portamento down |
| `0x03` | `0x07A6` | stub | tone portamento |
| `0x04` | stub | `0x0838` | vibrato — **`AD_VIB` is a bare `ret`** |
| `0x06` | stub | `0x0734` | note off |
| `0x0A` | `0x06A8` | stub | volume slide |
| `0x0B` | stub | `0x0743` | position jump |
| `0x0C` | stub | `0x0753` | set volume |
| `0x0D` | stub | `0x073B` | **pattern break** |
| `0x0F` | stub | `0x0766` | **set speed** |

Everything else — `0x05`, `0x07`, `0x08`, **`0x09`**, **`0x0E`** and
`0x10`–`0x1F` — is a stub in both tables.

Two entries in that table are traps for the unwary:

* **`0x00` arpeggio is not implemented.** `ROUTINES[0]` points at
  `AD_ARPEGGIO` (`0x0648`), which is a distinct label from `NULLROUT` but holds
  the same single `C3` byte. Behind it, at `0x0649`–`0x06A7`, sit **95 orphaned
  bytes** — a complete, correct-looking ProTracker-derived arpeggio routine
  (`MT_ARP1`–`MT_ARP4` and `MT_ARPLOOP`) with no entry point anywhere in the
  image. Someone inserted a `ret` to disable it and never removed the body.
* **`0x04` vibrato is not implemented either.** `AD_VIB` at `0x0838` is a bare
  `C3`; the recovered `ADLIB.ASM` line numbers jump straight from L1498 to
  L1507 across it. It is *also* mis-tabled: vibrato is a per-tick effect, but
  v3.00 wires it into `ROUTINES2`, so even a working handler would have fired
  once per row. Earlier revisions of this document claimed vibrato was
  implemented in v3.00 unlike `ADLIB.DRV`; that was wrong.

### 10.2 `0x0F` is *set speed*, not set tempo

This is the one effect whose meaning differs from OSL1, and getting it wrong is
expensive:

| Effect | `.ALB` (`ADLIB.EXE` v3.00) | OSL1 (`TRACKER.DRV`) |
|---|---|---|
| `0x09` | stub — no-op | set speed, `param & 0x1F` |
| `0x0F` | **set speed**, `param & 0x1F` | **set tempo**, PIT Hz, clamped `≥ 0x13` |

The handler — the author's own label is `AD_SETSPEED` — at `0x0766` is four
instructions:

```
0766  xor ah,ah / mov al,[si+0x3] / and al,0x1f / jz ret   ; si = MTSTRUC, +3 = MT_FXDATA
076F  mov di,[TRACKADDR] / mov byte [di+0x6],0 / mov [di+0x2],al
```

— mask to 5 bits, treat zero as a no-op (so `F00` does nothing, it is not a
stop), reset the tick counter (`TD_MT_COUNTER`, `TRACK_DATA+6`), store the speed
(`TD_MT_SPEED`, `TRACK_DATA+2`). Byte-for-byte the same routine
as `ADLIB.DRV`'s `fx_set_speed` @`0x06CB` and OSL1's `0x09` handler @`0x1361`.
Note the mask means there is **no ProTracker `Fxx ≥ 0x20` BPM split** — `F20`
sets speed 0, i.e. does nothing.

The corpus agrees. All 292 `.ALB` `0x0F` parameters fall inside `1`–`0x20`, and
they cluster on exactly the values a tick count would — 8 (97), 6 (41), 12 (33),
10 (30), 11 (28). A tempo in Hz would cluster near 50, and a ProTracker BPM near
125; neither appears. Conversely `0x09`, OSL1's set-speed, appears **zero
times** in the `.ALB` corpus, exactly as a stubbed handler predicts.

> **Correction (2026-07).** `src/replay.c` originally ran one effect table for
> both formats, so an old-format `Fxx` reached OSL1's set-*tempo* handler. Its
> `if (t < 19) t = 19` clamp then pinned the timer at 19 Hz — **38% of the
> correct rate** — and the speed change the song actually asked for never
> happened. `replay.c` now switches on `Replay.old_format` and routes `0x0F` to
> the shared `set_speed()` helper for pre-OSL1 songs; 17 `.ALB` files change
> speed where none did before.

### 10.3 Pattern break is `0x0D`

The per-row table above sends `0x0D` to a real handler at `0x073B` and `0x0E` to
the bare `ret` at `0x0647`. `ADLIB.DRV`, the `B4` driver, does the same. So two
of the three pre-`OSL1` generations demonstrably break on `0x0D`, whereas
`replay.c` — following what we then believed about OSL1 — uses `0x0E`.

The corpus agrees: `0x0D` appears in 8 of the 16 `.ALB` files (43 cells), `0x0E`
in only 2. `CRUSADE/CRUSFX.ALB` is the clincher — a sound-effect bank of 25
one-position cues carrying exactly 25 `0x0D`s, one to terminate each.

> **Correction (2026-08).** The premise of the last clause was wrong. `0x0E` is
> not OSL1's pattern break either. `TRACKER.DRV`'s per-row `0x0E` handler is
> `0x1351: movb $0xff,ds:0x1357 / ret`, and the only consumer of that flag is
> `0x0FFE: testb $0xff,ds:0x1357` → `0x100F: jmp stop_channel (0x0BFF)`. OSL1's
> `0x0E` is **stop channel**; OSL1 has **no pattern break at all**, because
> `0x0D` is a bare `ret` in both of its tables. So `replay.c`'s
> `case 0x0E: break_pending = 1` is wrong for all four generations at once: it
> loses the break the old formats do have and invents one OSL1 never had, while
> silently dropping stop-channel. See `EFFECTS.md` §4 for the full four-way
> comparison.

**`replay.c` still routes `0x0E` for all formats.** Fixing it properly means
splitting the behaviour by generation rather than by the current `old_format`
boolean, which is a change worth making deliberately rather than as a side
effect. See `RLD.md` §9.1 for why `B6` is the ambiguous case.

---

## 11. The driver

`MEDIT/LAPMUSIC/OLDMUSIC/OLDMUSIC/PIT/ADLIB/` — a directory that exists only in
the nested `OLDMUSIC/` copy, which is why it was missed for so long — contains
the whole delivery kit:

| File | Date | What it is |
|---|---|---|
| `ADLIB.EXE` | 21 Sep 1991 | **"ADLIB DRIVER (Version 3.00)"**, 5600-byte flat binary behind a 32-paragraph MZ stub. The `.ALB` player. |
| `README.DOC` | 26 Sep 1991 | Its API manual (also at `PIT/ADREAD.DOC`). |
| `MUSIC.EXE`, `FX.EXE` | 8 Oct 1991 | PKLITE'd harnesses; their strings include `adlib.alb` / `adlibfx.alb` and `music driver v. 3.00 , (c) 1991 Imagitec Design Ltd.` |
| `../MUSIC.ASM`, `../FX.ASM` | 8 Oct 1991 | **MASM source** of those harnesses. |
| `ADLIB.ALB`, `ADLIBFX.ALB` | 6 Nov 1991 | The songs it plays. |

The MZ header is a decoy: entry point `0:0`, zero relocations, no stack. It is
loaded with DOS's overlay call (`INT 21h AH=4Bh AL=03h`) and executed from byte
0. Code runs `0x0000`–`0x0CBE`, data `0x0CBF`–`0x15DF`.

**The build shipped with its symbols.** After the 5600-byte load image sit
11 140 bytes of Borland debug information (magic `0x52FB`, v9.2), which yields
**245 original `ADLIB.ASM` label names** and **1185 source-line records**
(lines 21–2144). Every routine and variable cited in this document can therefore
be given the name its author used. See `../PIT_ADLIB.EXE.annotated.asm` for the
full annotated listing.

### The relationship to `BSSJS/ADLIB.DRV`

`ADLIB.EXE` v3.00 shares `BSSJS/ADLIB.DRV`'s lineage but is **not the same
driver rebuilt** — it is a rewrite by the same hand. Measured on non-trivial
shared byte runs (≥ 8 bytes containing ≥ 3 distinct byte values, so that runs of
constant fill do not inflate the figure):

| Measure | Result |
|---|---|
| Shared non-trivial runs, whole image | 39 runs, 851 of 5600 bytes = **15.2%** |
| Shared non-trivial runs, code region only | 38 runs, 595 of 3263 bytes = **18.2%** |
| `difflib` ratio, code region | 0.339 raw, 0.352 with `nop` runs collapsed |
| `nop` bytes in the code region | **v3.00: 0 · `ADLIB.DRV`: 272** |

Only **two** substantive blocks are genuinely shared:

* **`FNUMNOTES`**, 256 bytes — the 96-entry note table, byte-identical
  (`0x0157 0x016C 0x0181 …` at `0x1203` here, `0x1392` there, with the same
  `sub bl,0x0C` C-1 base).
* **`SNDOUTPUTER`**, 105 bytes at `0x0C56` here / `0x0DB2` there — the OPL2
  register write with its dummy-read settling delays.

A third, much smaller match of 16 bytes (`0x09C1` ↔ `0x07BC`) is the six
unrolled shifts that multiply an instrument number by the 64-byte record
stride. Everything else in the 595 shared code bytes is prologue/epilogue
boilerplate: the same 10-register push/pop macro (epilogues at `0x003E`,
`0x00D1`, `0x02C4`, `0x02DD`, `0x033B`, `0x03F9`) and the same
`mov ax,cs / mov ds,ax / mov es,ax` opening.

Conversely, `ROUTINES`, `ROUTINES2`, `INIT_ADLIB_IO_MAP`, `CARRIER_SLOTS` and
`MODULATOR_SLOTS` are **absent from `ADLIB.DRV` entirely** — no prefix longer
than 3 bytes appears anywhere in it. And the 0-versus-272 `nop` count settles
it: `ADLIB.DRV` was assembled with branch-target padding that v3.00 does not
have, so v3.00 is not a re-assembly of the same source file.

What *is* shared is the design: the same `INT 60h` API with a 12-entry word jump
table, the same 22-byte per-voice `MTSTRUC`, the same ProTracker-derived row
reader, the same note table, and several of the same defects (the
`and al,0xfe` that clears the freshly-computed OPL2 CONNECTION bit is present in
both). **Same family, same hand, not the same driver.**

`MUSIC.ASM` shows how it is used: it loads the driver as a flat binary at offset
0 of a segment, calls it, then issues `AX = 2` "afterload" with `DX` = the music
segment. Its `dump_titles` routine independently corroborates the cue table
(§5).

### 11.1 It reads nothing else, and nothing else reads it

This driver is **`.ALB`-only**. Its header copy is `0x158` bytes and it reads
`n_cue` at `+0x11B`, so handed a `B4` file it would compute `n_cue = 0`, put the
paragraph table at `+0x158` instead of `+0x118`, and produce garbage. It would
not *notice*, either: `AFTER_LOAD` guards only on
`cmp byte [INSTALLEDFLAG],0xff` and **never validates the `20 AD 01` magic**, so
the check that gives this document its title is one the driver itself does not
perform. The signature is for the tools, not the player.

Symmetrically, `BSSJS/ADLIB.DRV` cannot read an `.ALB` — **but not because it
refuses to.** The 1991 driver performs **no magic-signature check at all**. Its
install routine `api_after_load` (`0x00DC` in
`../BSSJS_ADLIB.DRV.annotated.asm`) guards only on
`cmp byte [installed],0xFF`, then `rep movsb`s file `+0x000`–`+0x117` into
`hdr_copy` and `+0x118`–`+0x317` into `para_table`. The magic bytes land in the
header copy and are never read again. So it would **accept** an `.ALB` file and
then produce garbage. Every offset it relies on has moved:

| `ADLIB.DRV` assumes | `.ALB` actually has |
|---|---|
| track count at `+0xD8` (no range check) | permanent zero — the row loop would run 256 iterations |
| paragraph table at `+0x118` | header fields; the table is after a variable cue table |
| pattern stream at `+0x318` | wherever the paragraph table ends |
| 256-byte instrument blocks | none at all |
| 2-bit code words with 0/2/2/7 payloads | 16-bit presence masks with 4-byte cells |

The unchecked track-count read is the decisive one: it is used directly as a
loop bound with no clamp, so an `.ALB` file drives the older driver straight off
the end of its row buffer. `MED.EXE` does not read `.ALB` either — its
`cmpw $0x9ab6` rejects it outright.

**Each generation has its own dedicated driver, and no driver reads more than
one of them.**

One caveat on the find. The games themselves presumably still linked their own
copy; what survives here is the driver as *delivered to the developer*,
alongside a test harness — which is if anything better evidence than a shipped
game would be. `ADLIB.EXE` v3.00 is now annotated in full, in
`../PIT_ADLIB.EXE.annotated.asm`, with the author's own recovered labels and
`ADLIB.ASM` line numbers throughout, so the citations in this document can be
checked against the whole driver rather than against the container reader alone.

### 11.2 Driver defects that shape what the format sounds like

The full listing catalogues 22 defects. Most are API-surface bugs a player does
not care about, but eight change what a correct `.ALB` file actually *sounds*
like on the original hardware, and they are worth knowing before treating any
recording as ground truth:

| # | Defect | Audible consequence |
|--:|---|---|
| 8 | `and al,0xfe` at `0x09A9` clears the CONNECTION bit computed two instructions earlier | **Every channel is FM (serial), never AM (parallel)**, whatever the patch says. Present in `ADLIB.DRV` too, so it is the *intended* sound of both generations. |
| 15 | `AD_ARPEGGIO` is a bare `ret` with 95 bytes of working arpeggio orphaned behind it | Effect `0x00` does nothing |
| 17 | `AD_VIB` is a bare `ret`, and is wired into the per-row table rather than the per-tick one | Effect `0x04` does nothing |
| 18 | `SETFREQ` guards on `cmp bp,8` where it should be `9`, rejecting OPL2 channel 8 | Tom-tom and top cymbal **never receive a pitch** (§8) |
| 22 | `$BD` bit 5 is set at init and never cleared | Rhythm mode is permanent: 6 melodic voices, not 9, for the rest of the program's life |
| 19 | `CLKINT` chains `INT 08h` on every third tick only | The DOS clock runs at 16.67 Hz and loses 8.5% while music plays |
| 20 | `NORMCOUNTER` initialises to 0 and the first decrement wraps to 0xFF | The BIOS timer is starved for roughly 5 seconds after the driver installs |
| 21 | OPL2 settling delays are counted in bus cycles (6, 37 and 38 dummy reads), and `CLKINT` has no re-entrancy guard | Timing-marginal on fast machines; a long tick can re-enter the row reader |

Defects 8, 15, 17, 18 and 22 are the ones an emulation has to make a *choice*
about: reproducing them is faithful to the hardware, and not reproducing them is
faithful to the composer. `medplay` currently reproduces 8 (it forces FM) and 15
and 17 (both effects are no-ops) but not 18 or 22 — §8 explains why that makes
its percussion arguably more musical than the original was.

---

## 12. Playback verification

All 22 `.ALB` paths under `medplay/test/` render through `medplay --wav` with
healthy amplitude — peak 4087–25117, RMS 816–4751, **no silent or near-silent
file**. The two quietest are the sound-effect banks (`UTOFX`, `ING1`), which are
sparse by nature. The two `PIT` files found later render cleanly too —
`ADLIB.ALB` peak 23237 / RMS 4568, `ADLIBFX.ALB` peak 7606 / RMS 467, the latter
being a four-effect scratch bank.

Adding `.ALB` support did not disturb the older generations: after the change,
`decode_dump` and `osl1_dump` output is **byte-identical to the previous
revision on all 190 `B4` and all 314 `B6` files** in the test corpus.

Tools:

| Tool | Purpose |
|---|---|
| `tools/alb_probe.py` | container structure — the evidence behind §4–§6 |
| `tools/alb_cells.py` | presence-mask cell validation across the corpus (§7.1) |
| `tools/gen_compare.py` | `.ALB` against `B4`/`B6`, field by field |

---

## Appendix A — Reading an `.ALB` in seven steps

1. Check `+0x00`–`+0x02` for `20 AD 01`. Read `track_count` at `+0x118`,
   `restart_idx` at `+0x119`, `n_instr` at `+0x11A` and `n_cue` at `+0x11B`.
2. Order length = index of the last non-zero byte of `+0x18`–`+0x97`, plus one,
   minimum 1. Pattern count = the highest value in that range, plus one.
3. `para = 0x158 + 16 × n_cue`. Read `pattern_count + 1` `u16` entries from it;
   pattern *i* is at `para + 16 × table[i]`, and entry `pattern_count` is the
   start of the instrument records. There is no stale tail to skip.
4. Read the presence/volume pairs from `+0x98` for slots `0 … n_instr − 1`
   **only**, and take each default volume as `min(volume × 2, 0x7F)`.
5. For each pattern: 64 rows, each a `u16` presence mask followed by one 4-byte
   cell (note, 1-based instrument, effect command, effect parameter) per set
   bit, scanned MSB-first, in ascending slot order.
6. A row has `track_count + 5` slots; the last five are OPL2 percussion (§8).
7. Read `n_instr` 64-byte editor records from the base found in step 3, decoding
   the 13-field operator pairs into registers and **remembering to invert LEVEL
   and SUSTAIN** (§9). They run to EOF with no slack; if they do not, something
   earlier is wrong.

Timing needs no step: 50 Hz and 6 ticks per row, unless a pattern cell says
otherwise with `0x0F` (§10).

## Appendix B — Corpus

**22 `.ALB` paths, 16 distinct by content**, all under `MEDIT/LAPMUSIC/` —
`OLDMUSIC/` plus `LETHAL3/`. Every one parses cleanly under `src/oldalb.c`.
The inflation from 16 to 22 is `OLDMUSIC/`'s nested copy of itself;
`medplay/test/` is a further working duplicate of the whole tree and is excluded
from every figure here. Where a claim in this document is statistical it counts
distinct files; where it is about the archive as found, paths.

Files are classified **by magic, not by extension**, and this matters more for
`.ALB` than for any other extension in the archive: **most `.ALB` files are
ordinary OSL1 Adlib containers**, not this format. See `RLD.md` Appendix B for
the full magic census across the archive, and `OSL1.md` for the container that
accounts for the rest.

| Magic | `.ALB` paths | distinct | Notes |
|-------|-------------:|---------:|-------|
| `20 AD 01` | 22 | 16 | 1991–92; runtime export — **this document** |
| `"OSL1"` | many | — | not this format at all — ordinary OSL1 Adlib containers |

See `../RE-REPORT.md` §11.1b for the evidence that the leading magic byte tracks
*date* rather than target device, and §11.3 for the editor-record derivation.
