/* oldfmt.h - what the two pre-OSL1 loaders share
 *
 * The old formats fall into two families that are different enough to warrant
 * separate loaders:
 *
 *   oldrld.c   B4 (0xB4 0x9A 0x01) and B6 (0xB6 0x9A 0x01) - editor working
 *              files, 2-bit-per-track code word patterns, 256-byte instrument
 *              blocks. Specified in RLD.md.
 *   oldalb.c   ALB (0x20 0xAD 0x01) - a runtime export, presence-mask
 *              patterns, no instrument blocks at all. Specified in ALB.md.
 *
 * What they genuinely have in common is this file: the first 0x98 bytes of the
 * header, the paragraph addressing scheme, the 64-byte Adlib editor record,
 * and the synthetic OSL1-shaped pattern buffer both loaders hand to replay.c.
 * Everything below is deliberately mechanical - anything that encodes a real
 * decision about one format belongs in that format's loader, not here.
 *
 * Callers outside the two loaders should use oldrld.h / oldalb.h for the
 * loading itself. The two things they do need from here are
 * oldfmt_generation(), which decides which loader to call, and OldFmtFmSource,
 * which is what Song.old_fm_source holds whichever loader ran.
 */
#ifndef MEDPLAY_OLDFMT_H
#define MEDPLAY_OLDFMT_H

#include <stddef.h>
#include <stdint.h>
#include "osl1.h"

/* Value oldfmt_generation() returns for the `20 AD 01` .ALB variant. It is the
 * first magic byte, chosen for the same reason 0xB4/0xB6 are: it is what the
 * file actually starts with, so dumps and error messages stay literal. */
#define OLDFMT_GEN_ALB 0x20

/* ---- header fields at identical offsets in all three generations --------- */
#define OLDFMT_NAME_OFF     0x03
#define OLDFMT_NAME_LEN     20
#define OLDFMT_ORDER_OFF    0x18
#define OLDFMT_ORDER_MAX    128
#define OLDFMT_SLOTTAB_OFF  0x98   /* { present:u8, volume:u8 } per slot     */
#define OLDFMT_MAX_SLOTS    64     /* B6's count; B4 and .ALB use 32         */

/* Adlib editor record: 10 bytes of name then 13 field values per operator.
 * B4 stores 32 of them as an end-of-file bank; .ALB stores a variable number
 * as its only instrument data. Same layout in both. */
#define OLDFMT_EDREC_SIZE   64

#define OLDFMT_ROWS_PER_PAT 64
#define OLDFMT_PARA_ENTRIES 256

/* Slots a synthetic row record holds, matching decode_row()'s uncompressed
 * branch in replay.c (which advances a fixed 0x80 = 16 * 8 bytes per row). */
#define OLDFMT_ROW_SLOTS    16
#define OLDFMT_REC_DATA     (OLDFMT_ROWS_PER_PAT * OLDFMT_ROW_SLOTS * 8) /* 0x2000 */
#define OLDFMT_REC_SIZE     (4 + OLDFMT_REC_DATA)                        /* 0x2004 */

/* Which instrument source supplied an old-format file's OPL2 patches. Shared
 * vocabulary rather than an oldrld.c detail because it is what
 * Song.old_fm_source reports for both loaders - though only B4/B6 offer a
 * choice, so only oldrld.c has a setter (oldrld_set_fm_source). An .ALB has
 * nothing but editor records and always reports EDITOR. */
typedef enum {
    OLDFMT_FM_AUTO = 0,   /* resolve per generation; never reported */
    OLDFMT_FM_BLOCK,      /* the 256-byte instrument block          */
    OLDFMT_FM_EDITOR      /* the 64-byte Adlib editor record        */
} OldFmtFmSource;

/* Generation byte (0xB4, 0xB6 or OLDFMT_GEN_ALB) of `raw`, or 0 if it is not
 * an old-format file. This is the dispatch point: 0xB4/0xB6 go to
 * oldrld_load(), OLDFMT_GEN_ALB to oldalb_load(). */
uint8_t oldfmt_generation(const uint8_t *raw, size_t size);

/* True if `raw` (of length >= 3) begins with any old-format magic. */
int oldfmt_is_old_format(const uint8_t *raw, size_t size);

/* ---- bounds-checked primitive reads ------------------------------------- */
uint16_t oldfmt_u16(const uint8_t *p, size_t off, size_t size);

/* Fixed-width, NUL-padded ASCII, guaranteed terminated (same convention as
 * osl1.c's rd_str: rstrip trailing NULs only). `dst` needs field_len + 1. */
void oldfmt_str(const uint8_t *p, size_t off, size_t size,
                char *dst, size_t field_len);

/* As oldfmt_str, but also strips trailing spaces - editor-bank names are
 * space padded rather than NUL padded. */
void oldfmt_str_sp(const uint8_t *p, size_t off, size_t size,
                   char *dst, size_t field_len);

/* Copy `msg` into errbuf (if any) and return -1, so callers can `return
 * oldfmt_fail(...)` directly. */
int oldfmt_fail(char *errbuf, size_t errlen, const char *msg);

/* ---- the shared part of the header --------------------------------------
 * Title, order table, and the two counts derived from it. Nothing here needs
 * to know the generation: these fields sit at the same offsets in all three,
 * ahead of the point where the slot table's size starts to matter. */
typedef struct {
    char    title[OLDFMT_NAME_LEN + 1];
    uint8_t order[OLDFMT_ORDER_MAX];
    int     order_len;      /* last non-zero index + 1, minimum 1           */
    int     pat_count;      /* highest pattern number referenced + 1        */
} OldFmtHeader;

void oldfmt_read_header(const uint8_t *raw, size_t sz, OldFmtHeader *h);

/* Read `slot_count` { present, volume } pairs from the slot table into two
 * caller-supplied OLDFMT_MAX_SLOTS arrays. Returns the number present. */
int oldfmt_read_slot_table(const uint8_t *raw, size_t sz, int slot_count,
                           uint8_t *present, uint8_t *volume);

/* Resolve the paragraph offset table at `para_off` into pat_count + 1 file
 * offsets (entry pat_count is one past the last pattern, i.e. the end of the
 * pattern stream). Returns 0, or -1 if the table itself runs past EOF. */
int oldfmt_read_para(const uint8_t *raw, size_t sz, size_t para_off,
                     int pat_count, size_t *pat_off);

/* Allocate the synthetic pattern buffer both loaders decode into: pat_count
 * records of [u16 length][u16 header = row count][rows], zeroed, with the
 * four header bytes already written. Returns NULL on allocation failure;
 * *out_size receives the total byte count. */
uint8_t *oldfmt_alloc_patterns(int pat_count, size_t *out_size);

/* Row `row` of record `pat` within such a buffer. */
uint8_t *oldfmt_row(uint8_t *patterns, int pat, int row);

/* ------------------------------------------------------------------------
 * Adlib editor record -> OPL2 register bytes
 *
 * An editor instrument is stored as *field values*, not register bytes:
 * 10 bytes of name, then 13 fields per operator, then two waveform selects
 * and a rhythm code. The driver rebuilds the registers on every patch upload;
 * set_operator_patch @0x0AD0 fans out to seven small builders, and the field
 * meanings below are recovered from those builders directly.
 *
 *   [0] KSL 0..3      [1] MULTIPLE 0..15   [2] FEEDBACK 0..7 (modulator only)
 *   [3] ATTACK 0..15  [4] SUSTAIN 0..15    [5] EG-TYPE bool
 *   [6] DECAY 0..15   [7] RELEASE 0..15    [8] LEVEL 0..63
 *   [9] AM bool       [10] VIBRATO bool    [11] KSR bool
 *   [12] CONNECTION bool
 *
 * Two polarity reversals matter, and they are the reason such a patch read as
 * if it were a B6/.ADL register dump sounds wrong rather than merely odd:
 *
 *   SUSTAIN  the file says "how much sustain", reg 0x80 wants attenuation,
 *            so the driver emits (15 - value)             [write_80 @0x0C06]
 *   LEVEL    the file says "how loud", reg 0x40 wants attenuation, so the
 *            driver emits (63 - value) after scaling it by the slot's
 *            runtime volume                               [write_40 @0x0B1B]
 *
 * We emit LEVEL at full volume (the volume path in opl_dev then applies the
 * real attenuation to the carrier, exactly as it does for OSL1 patches).
 * That is consistent with the driver: at volume 0x7F its scaling
 * ((level * 127 * 2 + 127) / 254) is the identity, leaving 0x3F - level.
 *
 * CONNECTION is decoded and then thrown away - write_c0 @0x0BCE ends with an
 * unconditional `and al,0xFE`, so bit 0 of reg 0xC0 is always 0 and every
 * such instrument is 2-operator serial FM. We reproduce that rather than the
 * field, because reproducing the field would be a bug the hardware never saw.
 *
 * `mod` and `car` point at the two 13-byte field groups (record +0x0A and
 * +0x17); `adl` receives 16 bytes, of which 0..10 are live.
 */
void oldfmt_editor_ops_to_adl(const uint8_t *mod, const uint8_t *car,
                              uint8_t wave_mod, uint8_t wave_car,
                              uint8_t *adl);

/* Editor record +0x26 holds a rhythm code, but only 6..10 (bass drum, snare,
 * tom, top cymbal, hi-hat) are codes at all. Other values do occur - 0x14,
 * 0x28, 0x32, 0x37, 0x4B, 0x55, 0x5A in 9 B6 records, 0x20 in 17 B4 records -
 * and are editor scratch on the 0..100 percentage scale. Returns the code, or
 * 0 for "melodic", which is what the driver's own bounds test does. */
uint8_t oldfmt_rhythm_code(uint8_t raw_field);

/* Does an old-format 256-byte block's first 8 bytes look like an MT-32 Patch
 * Memory entry rather than an OPL2 register pair? MED.EXE's own B6 loader
 * (med.asm 0x25DC-0x26F2) tags every block device 4 (Roland) and reshapes
 * block[0:8] + block[0x12:0x12+236] into a 244-byte timbre dump, so these eight
 * bytes ARE the Patch Memory header (LAPC1.DEV.annotated.asm:780-796):
 *
 *   b0 timbre group 0..3   b1 timbre number     b2 key shift 0..48
 *   b3 fine tune 0..100     b4 bender range 0..24 b5 assign mode 0..3
 *   b6 reverb switch 0..1   b7 dummy (ignored by the MT-32, so not tested)
 *
 * On the labelled OSL1 corpus this passes 99.0% of known-Roland records and
 * 0.0% of known-Adlib ones, so it cleanly separates a Roland block (which must
 * NOT be voiced on the OPL2) from a genuine FM one. `p8` must have 8 readable
 * bytes. */
int oldfmt_mt32_sig(const uint8_t *p8);

/* Fill the device-independent tail of an Instrument: default volume from the
 * slot table, its device code, and the FM/renderability classification.
 * Returns 1 if the instrument carries a usable OPL2 FM patch.
 *
 * `device` is the resolved OSL device code (OSL1_SYNTH_FM for a genuine Adlib
 * patch, OSL1_SYNTH_ROLAND for an MT-32 block the OPL2 cannot voice). A Roland
 * instrument is marked not-FM and its adl[] is cleared so it stays silent
 * rather than rendering the timbre bytes as garbage OPL2 registers. An FM
 * instrument is still checked for actual operator data, because a file authored
 * for another device can leave the FM bytes as unused leftovers. */
int oldfmt_classify_instrument(Instrument *ins, uint8_t slot_volume,
                               uint8_t device);

/* ---- what a loader hands back ------------------------------------------- */
typedef struct {
    uint8_t           gen;           /* 0xB4 / 0xB6 / OLDFMT_GEN_ALB        */
    uint8_t           fm_source;     /* OldFmtFmSource, AUTO already resolved */
    uint8_t           track_count;   /* voices per row the engine plays     */
    uint8_t           restart_idx;
    uint16_t          rhythm_instr;  /* instruments with a rhythm code      */
    uint16_t          instr_size;    /* 256 (block) or 64 (editor record)   */
    int               slot_count;
    uint16_t          valid_count, fm_count, midi_count;
    const Instrument *instr;         /* slot_count entries                  */
} OldFmtResult;

/* Swap the decompressed pattern buffer into `song` (freeing the raw file it
 * replaces) and fill in every remaining Song and PatternBlock field. Takes
 * ownership of `patterns`. */
void oldfmt_commit(Song *song, const OldFmtHeader *h,
                   uint8_t *patterns, size_t patterns_size,
                   const OldFmtResult *r);

#endif /* MEDPLAY_OLDFMT_H */
