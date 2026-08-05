# Effects and player features across the four generations — an audit

**Question asked:** how much do the pre-`OSL1` formats and `OSL1` share, where do
they diverge, and does `medplay` implement all of it?

**Short answers.** Of the 32 effect slots every driver in this family dispatches,
**26 behave identically** across all four generations and **6 differ**. The two
pre-`OSL1` drivers that survive — `BSSJS/ADLIB.DRV` (`B4`, April 1991) and
`PIT/ADLIB/ADLIB.EXE` v3.00 (`.ALB`, September 1991) — have **byte-for-byte the
same effect vocabulary: nine live effects, nine for nine**. `TRACKER.DRV`
(`OSL1`) dispatches **sixteen**, of which **twelve are audible on an OPL2**.
`medplay` implements **15 of `OSL1`'s 16** correctly and **5 of the old formats'
9** correctly, with 3 approximated, 1 missing and 1 spurious.

Everything below is measured, not inferred. Sources:
`../BSSJS_ADLIB.DRV.annotated.asm`, `../PIT_ADLIB.EXE.annotated.asm`,
`../TRACKER.DRV.annotated.asm` + `../tracker_full.asm`,
`../ADLIB.DEV.annotated.asm`, `../med.asm`, and `src/replay.c`. Corpus figures
come from `tools/fxhist.c` (§A).

---

## 1. What "an effect" is in this family

All three drivers do the same thing with the effect byte:

```
and bx,0x1F          ; mask - no range check, so 0x20..0x7F alias down
shl bx,1
jmp [table + bx]     ; two tables: one per tick, one per row
```

Two 32-word tables, both indexed by `effect & 0x1F`: one runs on ticks
1..speed−1, the other once when a row is decoded. Unused slots point at a bare
`ret`. That structure is identical in all three drivers, which is the single
strongest piece of evidence that they are one lineage.

`TRACKER.DRV` adds one thing on top: it tests `cmd & 0x80` *before* masking and
routes those commands to the device's `SetSpecifics` vtable slot instead.

---

## 2. The effect sets side by side

Live = a real handler. `ret` = the slot exists in the table but the handler is a
bare `ret`. `—` = the slot points at the shared stub.

| # | `B4` `ADLIB.DRV` | `.ALB` `ADLIB.EXE` v3.00 | `OSL1` `TRACKER.DRV` | Audible on OPL2? |
|---|---|---|---|---|
| `0x00` | — | arpeggio, **`ret`** (`AD_ARPEGGIO` `0x0648`) | — | no, anywhere |
| `0x01` | portamento up `0x063B` | portamento up `0x06DE` | portamento up `0x129D` | **yes, all** |
| `0x02` | portamento down `0x0668` | portamento down `0x0708` | portamento down `0x12B0` | **yes, all** |
| `0x03` | tone portamento `0x0711` | tone portamento `0x07A6` | tone portamento `0x13B5` | **yes, all** |
| `0x04` | vibrato, **`ret`** `0x07B7` | vibrato, **`ret`** (`AD_VIB` `0x0838`) | vibrato `0x1402` → `D_SetVibrato` | **no** — stub in all three |
| `0x05` | — | — | pan `0x130E` → `D_SetPanPos` | **no** — OPL2 is mono |
| `0x06` | note off `0x0697` | note off `0x0734` | note off `0x12CC` | **yes, all** |
| `0x07` | — | — | reverb → `D_SetReverb` | **no** — stub |
| `0x08` | — | — | reverb → `D_SetReverb` | **no** — stub |
| `0x09` | — | — | **set speed** `0x1361` | `OSL1` only |
| `0x0A` | volume slide `0x05D7` | volume slide `0x06A8` | volume slide `0x1201` | **yes, all** |
| `0x0B` | position jump `0x06A8` | position jump `0x0743` | position jump `0x133F` | **yes, all** |
| `0x0C` | set volume `0x06B5` | set volume `0x0753` | set volume `0x1358` | **yes, all** |
| `0x0D` | **pattern break** `0x06A3` | **pattern break** `0x073B` | — **stub** | old only |
| `0x0E` | — | — | **stop channel** `0x1351` | `OSL1` only |
| `0x0F` | **set speed** `0x06CB` | **set speed** `0x0766` | **set tempo** `0x1376` | both, *different meanings* |
| `0x10`–`0x1D` | — | — | — | no, anywhere |
| `0x1E` | — | — | strum/arpeggio `0x11A0`/`0x119B` | `OSL1` only |
| `0x1F` | — | — | note retrigger `0x1611`/`0x15E8` | `OSL1` only |
| `≥ 0x80` | (masks down) | (masks down) | → `SetSpecifics` | **no** — stub |

`B6` has no driver of its own. `MED.EXE` loads `tracker\tracker.drv` (the only
driver name in its string table) and its `B6` pattern converter at
`med.asm:0x24b0`–`0x2556` copies the effect byte **verbatim** — code 1 is
`mov %ax,%fs:0x5(%di)`, code 3 is a straight 7-byte `movsd`/`movsw`/`movsb`.
**There is no translation table.** So mechanically `B6` gets `OSL1` semantics.
See §6.4 for why the corpus says otherwise.

---

## 3. Similarities — 26 of 32 slots, and 7 real effects

Counting slots that behave the same, to the ear, in all four generations:

| Class | Slots | Count |
|---|---|---:|
| Live and identical in meaning | `0x01 0x02 0x03 0x06 0x0A 0x0B 0x0C` | **7** |
| Present somewhere but silent on OPL2 everywhere | `0x04 0x05 0x07 0x08` | **4** |
| Dead in all four | `0x00`, `0x10`–`0x1D` | **15** |
| **Total in agreement** | | **26** |

The seven live shared effects are straight ProTracker: `1xx` `2xx` `3xx`
portamento, `6xx` note off, `Axy` volume slide, `Bxx` position jump, `Cxx` set
volume. Between `B4` and `.ALB` the agreement is total — same nine live slots,
same nine stubs, same masking, same two-table split, same `0x0D`-is-break
choice. Two drivers written five months apart by the same hand.

Beyond the effect tables, these are shared too:

* **The two-table dispatch itself**, `&0x1F`, no range check, stub = bare `ret`.
* **50 Hz base tick**, from a PIT divisor. `ADLIB.EXE` uses `0x5D37` (23863 →
  50.0013 Hz) and says so in its own manual; `ADLIB.DRV` does the same.
* **`Axy` nibble convention**: high nibble = up, high nibble zero = slide down
  by the low nibble. Identical in all three drivers (§6.2).
* **`Bxx` post-increment quirk**: the handler stores `param − 1` and the
  sequencer's position-advance then unconditionally increments, so the net
  target is `param`. Present in `ADLIB.DRV` (`0x06A8`: `dec %al` then set the
  break flag) and in `TRACKER.DRV` (`0x133F`).
* **`Fxx`/`0x09` masking**: `and 0x1F`, and zero is a no-op rather than a stop.
  Byte-identical routines.
* **The 96-word F-Number table**, 12 notes × 8 octaves, `(octave << 12) | fnum`,
  octave 0 = `0157 016C 0181 …  028A`. Physically shared: 256 bytes at
  `0x1203` in `ADLIB.EXE` and `0x1392` in `ADLIB.DRV`, and again as `FNUM[]` in
  `ADLIB.DEV`.

---

## 4. Differences — 6 slots

| # | Old (`B4`, `.ALB`) | `OSL1` | Consequence |
|---|---|---|---|
| `0x09` | stub | set speed, `param & 0x1F` | new in `OSL1` |
| `0x0D` | **pattern break** | stub | a break in an old file is a no-op under `OSL1` |
| `0x0E` | stub | **stop channel** | see below |
| `0x0F` | **set speed** | **set tempo** (PIT Hz, clamped ≥ `0x13`) | the expensive one |
| `0x1E` | stub | strum / arpeggio | new in `OSL1` |
| `0x1F` | stub | note retrigger | new in `OSL1` |

### `0x0E` is *stop channel*, not pattern break

This is a new finding and it corrects three of our own documents. The handler is

```
1351:  c6 06 57 13 ff    movb  $0xff,ds:0x1357
1356:  c3                ret
```

— it sets a flag, nothing more. The consumer is in the sequencer:

```
 ffe:  f6 06 57 13 ff    testb $0xff,ds:0x1357
100f:  e9 ed fb          jmp   stop_channel (0xbff)
```

and `stop_channel` at `0xBFF` note-offs the voice, releases the device handle
and clears the channel's active bit. **`OSL1` has no pattern break at all** —
`0x0D` is a stub in *both* of its tables. Positions end when the row counter
reaches the position's own row count, or on `Bxx`.

`ALB.md` §10.3, `RLD.md` §9.1 and `README.md` all describe `OSL1` as breaking on
`0x0E`. That was wrong, and it makes `replay.c`'s `case 0x0E: break_pending = 1`
wrong for **all four** generations, not just the old ones.

---

## 5. Same command, different arithmetic

Four effects share a number and a name across the generations but not their
implementation. These are the differences that make a straight port sound
approximately right rather than right.

### 5.1 Portamento: F-Number units versus a semitone-linear bender

**Old (`B4` `0x063B`/`0x0668`/`0x0711`, `.ALB` `0x06DE`/`0x0708`/`0x07A6`).**
Add or subtract `param × 2` directly to the 10-bit F-Number, with a hand-rolled
octave carry against literal window edges — `0x2AE`/`0x157` going up,
`0x142`/`0x28A` going down. Because F-Number spacing within an octave is not
uniform (`0x157`→`0x16C` is 21 units for a semitone at the bottom,
`0x266`→`0x28A` is 36 at the top), a *constant* F-Number step is a **faster
pitch rise low in the octave than high**. `B4` has no upper block clamp at all.

**`OSL1` (`0x129D`/`0x12B0`/`0x13B5`).** Add or subtract `param × 20` to a
14-bit bender centred on `0x2000`, at `0x155` = 341 units per semitone
(`imul $0x155` at `0x1389`). Semitone-linear by construction; `ADLIB.DEV`
converts to F-Number/block by piecewise interpolation over its own table.

Average rates: old ≈ `0.070 × param` semitones/tick, `OSL1` ≈ `0.059 × param`.
About **1.2× apart**, plus the curve-shape difference within each octave.

**Tone portamento arrival differs too.** `B4` re-keys the note when the slide
lands (`0x0770`: `push si / call note_on / pop si`), restarting the OPL
envelope. `OSL1` just parks the bender.

### 5.2 `Cxx` volume: a 0–`0x40` space, doubled at the device boundary

The old drivers keep an internal volume accumulator in **0..0x40** and double it
only when handing off to the level writer. `ADLIB.DRV` is explicit:

```
6b5:  cmpb $0x40,0x6(%si)      ; fx_set_volume: clamp the parameter
6be:  movb $0x40,0x6(%si)
6c5:  mov  %al,0x7(%si)        ; store into the 0..0x40 accumulator
6c8:  jmp  0x5fe               ; -> the shared push-volume tail:
 611:   shl  $1,%al            ;      double to 0..0x80
 613:   cmp  $0x80,%al
 61a:   mov  $0x7f,%al         ;      clamp to 0x7F
 621:   call 0x929             ;      -> set_volume
```

`ADLIB.EXE` reaches the same place differently: `AD_SETVOL` (`0x0753`) clamps to
`0x40`, and `CALC_FRAC` (`0x09B4`) then computes `0x3F − (LEVEL × volume / 0x3F)`.

**`OSL1` uses the full 0..0x7F space directly** — `0x1358` is `mov %al,0x12(%di)`
with no clamp at all, and `D_SetVolume` maps it as `0x3F − vol/2`.

The corpus confirms the two spaces are real and distinct:

| | peak param | modal params | fraction > `0x40` |
|---|---:|---|---:|
| `B4` | `0x60` | `0x20` `0x30` `0x40` | 0.86% |
| `B6` | `0x60` | `0x30` `0x20` `0x10` | 0.37% |
| `.ALB` | **`0x40`** | `0x30` `0x20` | **0.00%** |
| `OSL1` | `0x82` | `0x40` `0x60` `0x50` `0x7F` | **46.99%** |

The old generations pile up on `0x20`/`0x30`/`0x40` and stop dead at `0x40`;
`OSL1` peaks at `0x40` but runs on to `0x7F`. Two different faders.

### 5.3 `Axy` volume slide: the step scale cancels out

Old: `add`/`sub` the **raw** nibble on the 0..0x40 accumulator. `OSL1`: the same
nibble but `shl al,1` first (`0x1215`) on the 0..0x7F accumulator. Different
numbers, identical result — the ×2 that separates the two volume spaces also
separates the two step sizes. This one is genuinely equivalent.

### 5.4 `OSL1`'s volume chain has three stages the old drivers do not have

`send_volume` at `0x1224` in `TRACKER.DRV`, in order:

1. clamp to the instrument record's own maximum, `es:[di+0x19]` (`0x1236`);
2. `mul es:[bx+di+0x16]` then `>>7` — a **per-track mixer level**, a byte table
   in the song block indexed by track (`0x1249`, after `les di,[si+0x12]`);
3. `mul [si+9]` then `>>7` — the **channel master volume** (`0x125B`);
4. then `D_SetVolume`.

The old drivers have none of this. Their volume is instrument default → `Cxx` /
`Axy` → level writer.

---

## 6. Player features side by side

| Feature | `B4` | `.ALB` | `B6` | `OSL1` |
|---|---|---|---|---|
| Voice model | track *N* → channel *N*, fixed | same | (via `TRACKER.DRV`) | 16 logical channels over a 32-block pool |
| Voice allocation | none — positional | none | — | first-fit over 9; **the 10th note is dropped**, no stealing |
| OPL2 rhythm mode | **on permanently**, 6 melodic + 5 percussion | **on permanently**, `$BD` bit 5 set at init and never cleared | — | **never used** — 9 melodic |
| Percussion selection | per-instrument `rhythm` code 6–10 | **by row slot position** — the rhythm byte is never read | — | n/a |
| Percussion voices | per track | **5 global `MTSTRUC`s shared by every track** | — | n/a |
| Notes per cell | 1 | 1 | 1 | **up to 4** (chords) |
| Tick rate | fixed 50 Hz, immutable | fixed 50 Hz, immutable | fixed 50 Hz | **`Fxx` reprograms the PIT globally**, clamp ≥ 19 Hz |
| Per-instrument transpose | none | none | none | **yes**, record `+0x22` |
| Global transpose | none | none | none | **yes**, `ds:0x10e2`, folded into pitch *and* into the tone-porta target |
| Per-instrument max volume | none | none | none | **yes**, record `+0x19` |
| Per-track mixer level | none | none | none | **yes**, song block, byte per track |
| Channel master volume | none | none | none | **yes** |
| Fade out | **yes**, `api_fade` `0x021E`, 25 Hz | **yes** (with defect 10 — `FADEACTIVE` read, never written) | — | **none at all** |
| Device abstraction | none — hard-wired OPL2 | none — hard-wired OPL2 | — | `.DEV` backends: Adlib, LAPC1, SBLAST, SCC1 |
| Sound-effect cues | no | **yes** — cue table, 0-based positions | no | no |
| Debug symbols shipped | no | **yes** — Borland block, 245 labels | — | no |

### 6.1 The `.ALB` percussion arrangement is unlike anything else here

Three compounding quirks, all in `../PIT_ADLIB.EXE.annotated.asm`:

* the five percussion `MTSTRUC`s are **global** (`mov cx,5 / mov di,MT_VOICE7`),
  so every track shares them;
* `GET_TIMBRE` copies 30 bytes from `INSTADDR + inst*64 + 10` into `TEMPTIMBRE`,
  which puts the record's `+0x26` rhythm byte at `0x12DF` — a location nothing
  reads. Percussion is therefore decided **purely by which of the five slots the
  cell sits in**, which is why `CHAOS/ADLIB2.ALB`, carrying `BDRUM1` (code 6) in
  the snare slot, gets a snare;
* `SETFREQ` (`0x0AC3`) guards with `cmp bp,8 / jc`, rejecting OPL2 **channel 8** —
  the channel the tom-tom and top cymbal share — so both sound at F-Number 0,
  block 0, and the `push bp / mov bp,8 / call SETPERCFREQ` at `0x0A43` is dead
  weight.

### 6.2 `Axy` slides **down**, and `RE-REPORT.md` §11.6 has it backwards

`BSSJS_ADLIB.DRV.annotated.asm` elides bytes `0x0611`–`0x063A` and asserts
"Effect 0Ah in this format is volume slide UP only… low nibble ignored". It is
not. The `jz 0x626` at `0x05E6` lands on the down path:

```
626:  xor  %ah,%ah
628:  mov  0x6(%si),%al
62b:  and  $0xf,%al          ; LOW nibble
62d:  sub  %al,0x7(%si)      ; subtract
630:  jae  0x639
635:  movb $0x0,0x7(%si)     ; clamp to 0
639:  jmp  0x5fb             ; join the shared push-volume tail
```

Both directions exist, in `ADLIB.DRV` and in `ADLIB.EXE` (`AD_VOLDOWN`
`0x06CC`) alike. This matters because the corpus uses **only** the down
direction in the two generations that have a driver:

| | up (`hi ≠ 0`) | down (`hi = 0`, `lo ≠ 0`) |
|---|---:|---:|
| `B4` | **0** | 163 |
| `.ALB` | **0** | 5 830 |
| `B6` | 234 | 4 907 |
| `OSL1` | 1 769 | 34 458 |

Under the "up only" reading, every volume slide in `B4` and `.ALB` — close to
6 000 cells — would be silent. `RE-REPORT.md` §11.6 should be corrected.

### 6.3 `0x00` and `0x04` are ambitions, not features

`ADLIB.EXE` tabulates arpeggio at `AD_ARPEGGIO` `0x0648` and vibrato at
`AD_VIB` `0x0838`; both are a single `C3`. Ninety-five orphaned bytes at
`0x0649`–`0x06A7` still carry `MT_ARP1`–`MT_ARP4` and `MT_ARPLOOP`, so the
arpeggiator was written and then unhooked. `ADLIB.DRV`'s vibrato slot is a `ret`
too. `OSL1` dispatches `0x04` for real but lands on `D_SetVibrato`, `clc/retf`
on `ADLIB.DEV`. **Nobody in this family ever had vibrato on an OPL2.**

### 6.4 `B6` — mechanically `OSL1`, authorially old

`MED.EXE` copies effect bytes verbatim and plays through `TRACKER.DRV`, so a
`B6` file loaded into the editor gets `OSL1` semantics: `Fxx` becomes set-tempo,
`0x0D` becomes a no-op, `0x0E` stops the channel.

The corpus says the files were not written that way. `B6`'s 9 558 `Fxx`
parameters cluster on **6** (the format's own default speed), then 9, 4, 5, 8 —
tick counts, not hertz. Under `TRACKER.DRV` every one of those would hit the
`≥ 0x13` clamp and pin the timer at 19 Hz. And `B6` carries 1 546 `0x0D`s
against 129 `0x0E`s, the ratio of a generation that breaks on `0x0D`.

The resolution is that `B6` files predate `MED.EXE`: they were authored under a
`B4`-era engine and later *imported*. `replay.c`'s choice — treat `B6` as old for
`Fxx` — follows authorial intent, and by the same argument `0x0D` should be
pattern break for `B6` as well. This closes the question `RLD.md` §9.1 left open,
with the new evidence being the absence of any remap table in `med.asm`.

---

## 7. Does `medplay` support all of it?

`src/replay.c` is one engine for all four generations, switched by a single
`old_format` boolean. That is a deliberate and mostly successful bet. The audit:

### 7.1 `OSL1` — 15 of 16 dispatched slots correct

Correct: `0x01` `0x02` `0x03` `0x06` `0x09` `0x0A` `0x0B` `0x0C` `0x0F` `0x1E`
`0x1F`, and the four device-stub slots `0x04` `0x05` `0x07` `0x08` are
faithfully no-ops because `ADLIB.DEV`'s `D_SetVibrato`, `D_SetPanPos` and
`D_SetReverb` really are `clc/retf`. Ignoring `cmd ≥ 0x80` is faithful too:
`SetSpecifics` at `0x109C` is `clc/retf`, so the 6 490 such cells in 110 files
are silent on real hardware. `opl_dev.c` reproduces the first-fit allocator and
the *drop the 10th note* policy, and the `0x1F` reload `or` bug and the forced-FM
`and al,0xfe` are both carried over deliberately.

Wrong: **`0x0E`** (§4).

### 7.2 The old formats — 5 of 9 correct

| # | Old spec | `replay.c` | Verdict |
|---|---|---|---|
| `0x01` | F-Number `± param × 2`, hand-rolled octave carry | bender `± param × 20` | **approximate** — ~1.2× fast, wrong curve |
| `0x02` | as above | as above | **approximate** |
| `0x03` | as above **plus re-key on arrival** (`B4`) | bender, no re-key | **approximate**, missing envelope restart |
| `0x06` | note off | `opl_dev_keyoff` on the channel | correct |
| `0x0A` | raw nibble on 0..`0x40` | `nibble × 2` on 0..`0x7F` | correct (equivalent, §5.3) |
| `0x0B` | `param − 1` then `+1` | `jump_order = param` | correct |
| `0x0C` | clamp `0x40`, **then ×2** at the device | raw param into a 0..`0x7F` field | **wrong — plays at half level** |
| `0x0D` | **pattern break** | not implemented | **missing** |
| `0x0F` | speed = `param & 0x1F` | `set_speed` when `old_format` | correct |
| `0x0E` | stub | `break_pending = 1` | **spurious break** |

### 7.3 The gap list, by severity

| # | Gap | Scale (paths under `MEDIT/`) |
|---|---|---|
| 1 | **`Cxx` not doubled for old formats.** Both sites: the standalone `case 0x0C` and the riding-`Cxx` path in `trigger_row`. Needs `clamp 0x40` then `× 2`. | 52 290 cells / 154 `B4` files, 119 769 / 228 `B6`, 20 823 / 20 `.ALB` |
| 2 | **`0x0D` pattern break missing** for `B4`/`.ALB`/`B6`. | 774 / 61 `B4`, 1 546 / 113 `B6`, 57 / 10 `.ALB` |
| 3 | **`0x0E` mis-routed** — should stop the channel under `OSL1`/`B6`, and do nothing under `B4`/`.ALB`. Currently truncates positions. | 168 / 20 `OSL1`, 129 / 24 `B6`, 7 / 1 `.ALB`, 1 / 1 `B4` |
| 4 | **No OPL2 rhythm mode.** Percussion instruments are voiced as ordinary melodic notes on free channels, and a busy old file allocates more channels than the real driver had. Compounded for `.ALB`, whose five percussion row-slots land in melodic voices 8–12. | 72 `B4` + 12 `B6` files with percussion; all `.ALB` percussion |
| 5 | **Old-format portamento uses the `OSL1` bender.** | 7 045 cells `B4` + 16 594 `B6` + 575 `.ALB` across `0x01`/`0x02`/`0x03` |
| 6 | **`OSL1` instrument max-volume ceiling (`+0x19`) not parsed.** | all `OSL1` |
| 7 | **`OSL1` per-track mixer level not applied.** `osl1.c:256` *does* read the bytes — as `blk->defaults[8]`, 8 words at block `+0x16` — but `replay.c` never consults them. The driver reads the same region as **16 bytes indexed by track**. The field's interpretation needs settling before it can be applied. | all `OSL1` |
| 8 | **`OSL1` global transpose not applied**, and `set_tone_porta_target` omits it where `0x1389` folds it in. The driver's copy is `ds:0x10e2`; **its source in the file is not yet located** — treat as open. | `OSL1` files that use it |
| 9 | **`OSL1` channel master volume not modelled.** Probably always full from the API's point of view; low risk. | — |
| 10 | **No fade.** `B4` and `.ALB` both have one; it is an API call, not an effect, so no song file asks for it. Cosmetic. | — |
| 11 | **No `& 0x1F` mask on the effect index.** The drivers alias `0x20`–`0x7F` down; `replay.c`'s `switch` lets them fall through. Measured impact is negligible: **18 cells in 14 files** alias onto a live effect. | 13 `OSL1`, 5 `B6` |

Items 1–3 are small, local and high-value. Item 1 is close to a one-line change
and touches more cells than everything else combined. Item 4 remains, as
`RLD.md` §10 already says, the largest single fidelity gap, and it is a
device-layer job rather than a format one.

---

## Appendix A — Method

`tools/fxhist.c` `#include`s `src/replay.c` rather than linking it, so it can
reimplement the tick loop with a sampling point **between** `decode_row()` and
`trigger_row()`. That matters: `dispatch_row_effect()` consumes `0x06`, `0x0C`
and `0x0E` by zeroing `b[6]`, so sampling after `replay_tick()` under-counts
exactly the three commands this audit turns on. The first run of the tool did
precisely that and reported zero `Cxx` cells in a corpus that is 40% `Cxx`.

Build and run:

```sh
gcc -O2 -o fxhist.exe tools/fxhist.c src/osl1.c src/oldfmt.c \
    src/oldalb.c src/oldrld.c src/opl_dev.c src/opl3.c -lm
while IFS= read -r f; do ./fxhist.exe "$f"; done < songs.txt
```

Each song is played through one full pass of its order list at its own speed,
with `dev = NULL`. 1 750 files carry one of the four magics across the whole
tree; the figures in §7.3 count the 833 paths under `MEDIT/` only, excluding
`medplay/test/`, which is a working duplicate. Files are classified by magic,
never by extension — `.ALB` in particular is a filename convention that covers
`OSL1` containers as often as `20 AD 01` ones.

Corpus totals, `MEDIT/` only, files carrying at least one effect cell:

| | `B4` | `B6` | `.ALB` | `OSL1` |
|---|---:|---:|---:|---:|
| files | 186 | 308 | 22 | 317 |
| effect cells | 89 221 | 227 150 | 30 872 | 360 655 |
| distinct commands used | 14 | 35 | 12 | 63 |

---

## Appendix B — Corrections this audit makes to existing documents

| Document | Claim | Correction |
|---|---|---|
| `ALB.md` §10.3, `RLD.md` §9.1, `README.md` | `OSL1` breaks the pattern on `0x0E` | `0x0E` is **stop channel** (`0x1351` → `stop_channel` `0xBFF`); `OSL1` has no pattern break |
| `RE-REPORT.md` §11.6 | `.ALB` `Axy` is "volume slide up only" | both directions exist (`AD_VOLDOWN` `0x06CC`), and the corpus uses **only** down |
| `BSSJS_ADLIB.DRV.annotated.asm` `0x05D7` header | "There is NO slide-down path… volume slide UP only" | the down path is at `0x0626`, inside the region the listing elides |
| `RLD.md` §9.1 | `B6`'s `0x0D`/`0x0E` reading "remains genuinely ambiguous" | `med.asm:0x24b0`–`0x2556` performs **no** effect remapping, so `B6` is mechanically `OSL1`; the corpus shows the files were authored under old semantics, so treat `B6` as old — which is what `replay.c` already does for `Fxx` |

All four have been applied: `ALB.md` §10.3, `RLD.md` §9.1 and `README.md` carry a
dated correction blockquote, `RE-REPORT.md` §11.6 carries the `Axy` correction
with the disassembly of the down path, and the `0x05D7` header comment in
`BSSJS_ADLIB.DRV.annotated.asm` has been rewritten with `0x0626` disassembled in
place. Nothing in the documents now claims `0x0E` breaks or that `Axy` is
one-directional.
