/* oldrld.c - pre-OSL1 "old format" .RLD loader, generations B4 and B6
 *
 * See oldrld.h for the on-disk layout. This is a clean-room reimplementation
 * of two loaders:
 *
 *   B6 (0xB6 0x9A 0x01)  what MED.EXE does at med.asm ~0x233b-0x2717.
 *   B4 (0xB4 0x9A 0x01)  what the 1991 standalone driver
 *                        LAPMUSIC/OLDMUSIC/BSSJS/ADLIB.DRV does in its
 *                        install entry (offset 0x00EC-0x013C) and
 *                        inst_fetch (0x07B8) / set_operator_patch (0x0AD0).
 *                        Full annotation: BSSJS_ADLIB.DRV.annotated.asm.
 *
 * Strategy: rather than teaching the replay engine a second pattern format,
 * we decompress every old-format pattern once at load time into the exact
 * byte shape decode_row() already understands for an *uncompressed* OSL1
 * position record - [u16 length][u16 header (bit 0x8000 clear = row count)]
 * [row_count * 16 voices * 8 bytes] - and point blk->pos_ptr[pattern_number]
 * at it (decode_row indexes pos_ptr[] by the pattern number stored in
 * order[], not by order position, so this lines up directly with the old
 * format's own "order table of pattern numbers" model).
 *
 * Patterns are located through the file's own paragraph offset table rather
 * than by walking the stream and re-deriving the 16-byte padding, which is
 * what this loader used to do. The driver does the same (install @0x0114
 * indexes para_table by pattern number), it is robust against any padding
 * edge case, and it gives an exact, checkable end-of-stream address.
 */
#include "oldrld.h"

#include <stdlib.h>
#include <string.h>

/* ---- offsets shared by both generations -------------------------------- */
#define OLD_NAME_OFF          0x03
#define OLD_NAME_LEN          20
#define OLD_ORDER_OFF         0x18
#define OLD_ORDER_MAX         128
#define OLD_SLOTTAB_OFF       0x98
#define OLD_INSTR_BLOCK_SIZE  256
#define OLD_ROWS_PER_PAT      64
#define OLD_PARA_ENTRIES      256

/* Adlib editor bank: 32 fixed-size records, always the last 2048 bytes of a
 * B4 file (and of the 136 B6 files that carry one). */
#define OLD_FMBANK_SLOTS      32
#define OLD_FMBANK_RECSIZE    64
#define OLD_FMBANK_SIZE       (OLD_FMBANK_SLOTS * OLD_FMBANK_RECSIZE)

/* Per-generation offsets. Everything from the track count onwards shifts by
 * 0x40 because B4 has 32 instrument slots and B6 has 64. */
typedef struct {
    uint8_t gen;            /* 0xB4 / 0xB6                                  */
    int     slot_count;     /* 32 / 64                                      */
    size_t  trackcnt_off;   /* 0x0D8 / 0x118                                */
    size_t  restart_off;    /* 0x0D9 / 0x119                                */
    size_t  para_off;       /* 0x118 / 0x958 - also the paragraph origin    */
} OldFmt;

static const OldFmt FMT_B4 = { 0xB4, 32, 0x0D8, 0x0D9, 0x118 };
static const OldFmt FMT_B6 = { 0xB6, 64, 0x118, 0x119, 0x958 };

static OldrldFmSource g_fm_source = OLDRLD_FM_AUTO;

void oldrld_set_fm_source(OldrldFmSource src) { g_fm_source = src; }

static uint16_t rd_u16(const uint8_t *p, size_t off, size_t size)
{
    if (off + 2 > size) return 0;
    return (uint16_t)(p[off] | (p[off + 1] << 8));
}

/* Copy a fixed-width, null-padded ASCII string and guarantee termination
 * (same convention as osl1.c's rd_str: rstrip trailing NULs only). */
static void rd_str(const uint8_t *p, size_t off, size_t size,
                    char *dst, size_t field_len)
{
    size_t i;
    for (i = 0; i < field_len; i++)
        dst[i] = (off + i < size) ? (char)p[off + i] : '\0';
    dst[field_len] = '\0';
    while (field_len > 0 && dst[field_len - 1] == '\0')
        field_len--;
    dst[field_len] = '\0';
}

/* Editor-bank names are space padded, not NUL padded. */
static void rd_str_sp(const uint8_t *p, size_t off, size_t size,
                      char *dst, size_t field_len)
{
    rd_str(p, off, size, dst, field_len);
    size_t n = strlen(dst);
    while (n > 0 && (dst[n - 1] == ' ' || dst[n - 1] == '\0')) n--;
    dst[n] = '\0';
}

static int fail(char *errbuf, size_t errlen, const char *msg)
{
    if (errbuf && errlen) {
        strncpy(errbuf, msg, errlen - 1);
        errbuf[errlen - 1] = '\0';
    }
    return -1;
}

uint8_t oldrld_generation(const uint8_t *raw, size_t size)
{
    if (size < 3 || raw[1] != 0x9A || raw[2] != 0x01) return 0;
    if (raw[0] == 0xB4 || raw[0] == 0xB6) return raw[0];
    return 0;
}

int oldrld_is_old_format(const uint8_t *raw, size_t size)
{
    return oldrld_generation(raw, size) != 0;
}

/* ------------------------------------------------------------------------
 * Adlib editor record -> OPL2 register bytes
 *
 * A B4 instrument is stored as *editor field values*, not register bytes:
 * 10 bytes of name, then 13 fields per operator, then two waveform selects
 * and a rhythm code. The driver rebuilds the registers on every patch
 * upload; set_operator_patch @0x0AD0 fans out to seven small builders, and
 * the field meanings below are recovered from those builders directly.
 *
 *   [0] KSL 0..3      [1] MULTIPLE 0..15   [2] FEEDBACK 0..7 (modulator only)
 *   [3] ATTACK 0..15  [4] SUSTAIN 0..15    [5] EG-TYPE bool
 *   [6] DECAY 0..15   [7] RELEASE 0..15    [8] LEVEL 0..63
 *   [9] AM bool       [10] VIBRATO bool    [11] KSR bool
 *   [12] CONNECTION bool
 *
 * Two polarity reversals matter, and they are the reason a B4 patch read as
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
 * B4 instrument is 2-operator serial FM. We reproduce that rather than the
 * field, because reproducing the field would be a bug the hardware never saw.
 */
static void editor_ops_to_adl(const uint8_t *mod, const uint8_t *car,
                              uint8_t wave_mod, uint8_t wave_car,
                              uint8_t *adl)
{
    const uint8_t *op[2] = { mod, car };
    uint8_t wave[2] = { (uint8_t)(wave_mod & 3), (uint8_t)(wave_car & 3) };

    for (int i = 0; i < 2; i++) {
        const uint8_t *f = op[i];
        uint8_t *o = adl + i * 5;      /* adl[0..4] mod, adl[5..9] carrier */

        /* 0x20  AM | VIB | EG-TYPE | KSR | MULTIPLE       [write_20 @0x0C3C] */
        o[0] = (uint8_t)((f[9]  ? 0x80 : 0) |
                         (f[10] ? 0x40 : 0) |
                         (f[5]  ? 0x20 : 0) |
                         (f[11] ? 0x10 : 0) |
                         (f[1] & 0x0F));
        /* 0x40  KSL | (63 - LEVEL)                        [write_40 @0x0B1B] */
        o[1] = (uint8_t)(((f[0] & 3) << 6) |
                         ((0x3F - (f[8] & 0x3F)) & 0x3F));
        /* 0x60  ATTACK | DECAY                            [write_60 @0x0BD5] */
        o[2] = (uint8_t)(((f[3] & 0x0F) << 4) | (f[6] & 0x0F));
        /* 0x80  (15 - SUSTAIN) | RELEASE                  [write_80 @0x0C06] */
        o[3] = (uint8_t)((((0x0F - (f[4] & 0x0F)) & 0x0F) << 4) |
                         (f[7] & 0x0F));
        /* 0xE0  waveform select                           [write_e0 @0x0CC9] */
        o[4] = wave[i];
    }

    /* 0xC0  FEEDBACK << 1, connection bit forced low      [write_c0 @0x0B91]
     * Only the modulator's feedback field is ever consulted; the carrier's
     * copy holds junk in the corpus (0x2F, 0x3B, 0x65 ...) precisely because
     * nothing reads it. */
    adl[10] = (uint8_t)((mod[2] & 0x07) << 1);
    adl[11] = adl[12] = adl[13] = adl[14] = adl[15] = 0;
}

int oldrld_load(Song *song, char *errbuf, size_t errlen)
{
    const uint8_t *raw = song->raw;
    size_t sz = song->size;

    uint8_t gen = oldrld_generation(raw, sz);
    const OldFmt *fmt = (gen == 0xB4) ? &FMT_B4 : &FMT_B6;
    if (gen == 0) return fail(errbuf, errlen, "not an old-format .RLD");

    /* Smallest possible file is header + full paragraph table + one pattern. */
    if (sz <= fmt->para_off + OLD_PARA_ENTRIES * 2)
        return fail(errbuf, errlen, "old-format file too small");

    /* ---- header: name, order table, instrument slot table ---------------- */
    char title[OLD_NAME_LEN + 1];
    rd_str(raw, OLD_NAME_OFF, sz, title, OLD_NAME_LEN);

    uint8_t order[OLD_ORDER_MAX];
    for (int i = 0; i < OLD_ORDER_MAX; i++)
        order[i] = ((size_t)OLD_ORDER_OFF + i < sz) ? raw[OLD_ORDER_OFF + i] : 0;

    /* order length = index of last non-zero byte + 1 (backward scan) */
    int order_len = 0;
    for (int i = OLD_ORDER_MAX - 1; i >= 0; i--) {
        if (order[i] != 0) { order_len = i + 1; break; }
    }
    if (order_len == 0) order_len = 1;   /* a song always plays one position */

    /* pattern count = highest pattern number referenced + 1 */
    int pat_count = 0;
    for (int i = 0; i < OLD_ORDER_MAX; i++)
        if (order[i] > pat_count) pat_count = order[i];
    pat_count += 1;

    int slot_count = fmt->slot_count;
    uint8_t slot_present[64], slot_volume[64];
    int present_count = 0;
    for (int i = 0; i < slot_count; i++) {
        size_t eo = OLD_SLOTTAB_OFF + (size_t)i * 2;
        slot_present[i] = (eo     < sz) ? raw[eo]     : 0;
        slot_volume[i]  = (eo + 1 < sz) ? raw[eo + 1] : 0;
        if (slot_present[i]) present_count++;
    }

    uint8_t track_count = (fmt->trackcnt_off < sz) ? raw[fmt->trackcnt_off] : 8;
    if (track_count == 0 || track_count > 8) track_count = 8;

    uint8_t restart_idx = (fmt->restart_off < sz) ? raw[fmt->restart_off] : 0;
    if (restart_idx >= order_len) restart_idx = 0;

    /* ---- paragraph offset table ------------------------------------------
     * Entry i is pattern i's start in 16-byte paragraphs, measured from the
     * table's own file offset. Entry pat_count (one past the last pattern)
     * marks the end of the pattern stream, which is where the instrument
     * blocks begin - the driver uses exactly this to find them
     * (install @0x0107: max_pattern, inc, index para_table). */
    if (fmt->para_off + 2 * (size_t)(pat_count + 1) > sz)
        return fail(errbuf, errlen, "old-format paragraph table truncated");

    size_t pat_off[OLD_PARA_ENTRIES];
    for (int p = 0; p <= pat_count && p < OLD_PARA_ENTRIES; p++)
        pat_off[p] = fmt->para_off + (size_t)rd_u16(raw, fmt->para_off + 2u * p, sz) * 16;

    /* ---- pattern decompression -------------------------------------------
     * Each synthetic record: [u16 len][u16 hdr=row_count][row data].
     * Row data: row_count rows * 16 voice-slots * 8 bytes, matching the
     * uncompressed-position branch of decode_row() in replay.c verbatim. */
    size_t rec_data = (size_t)OLD_ROWS_PER_PAT * 16 * 8;   /* 0x2000 */
    size_t rec_size = 4 + rec_data;                        /* 0x2004 */
    size_t out_size = (size_t)pat_count * rec_size;

    uint8_t *out = calloc(1, out_size ? out_size : 1);
    if (!out) return fail(errbuf, errlen, "out of memory");

    int overrun = 0;

    for (int p = 0; p < pat_count && !overrun; p++) {
        uint8_t *recbase = out + (size_t)p * rec_size;
        recbase[0] = (uint8_t)(rec_data & 0xFF);
        recbase[1] = (uint8_t)((rec_data >> 8) & 0xFF);
        recbase[2] = (uint8_t)(OLD_ROWS_PER_PAT & 0xFF);
        recbase[3] = (uint8_t)((OLD_ROWS_PER_PAT >> 8) & 0xFF);
        uint8_t *rows = recbase + 4;

        size_t cursor = pat_off[p];
        if (cursor >= sz) { overrun = 1; break; }

        for (int row = 0; row < OLD_ROWS_PER_PAT; row++) {
            uint8_t *rowbuf = rows + (size_t)row * 16 * 8;  /* zeroed by calloc */

            if (cursor + 2 > sz) { overrun = 1; break; }
            uint16_t word = rd_u16(raw, cursor, sz);
            cursor += 2;

            /* The code word is always a fixed 2-byte read, but only the top
             * 2*track_count bits are ever examined (med.asm 0x24ca: `mov
             * 0x3fc1,%cl` drives the extraction loop count; ADLIB.DRV's
             * read_row @0x043A does the same from hdr_trackcount) - not a
             * hardcoded 8 tracks, even though most files have 8. */
            for (int t = 0; t < track_count; t++) {
                int code = (word >> (14 - t * 2)) & 3;
                uint8_t cell[8] = {0};

                switch (code) {
                case 0:
                    break;
                case 1:   /* -> effect cmd/param (cell[5],[6]) */
                    if (cursor + 2 > sz) { overrun = 1; break; }
                    cell[5] = raw[cursor];
                    cell[6] = raw[cursor + 1];
                    cursor += 2;
                    break;
                case 2:   /* -> note + instrument (cell[0],[4]) */
                    if (cursor + 2 > sz) { overrun = 1; break; }
                    cell[0] = raw[cursor];
                    cell[4] = raw[cursor + 1];
                    cursor += 2;
                    break;
                default:  /* code 3: full 7-byte event */
                    if (cursor + 7 > sz) { overrun = 1; break; }
                    memcpy(cell, raw + cursor, 7);
                    cursor += 7;
                    break;
                }
                if (overrun) break;
                memcpy(rowbuf + (size_t)t * 8, cell, 8);
            }
            if (overrun) break;
        }
    }

    if (overrun) {
        free(out);
        return fail(errbuf, errlen, "old-format pattern data truncated");
    }

    /* ---- instrument sources ----------------------------------------------
     * blocks_off  first 256-byte block; one per PRESENT slot, in slot order.
     * fmbank_off  the 32 x 64-byte Adlib editor bank, or 0 if absent.
     *
     * The bank is identified positionally: it is what remains after the
     * blocks, and it is either exactly 2048 bytes or nothing. That holds for
     * every one of the 500 old-format files in the corpus (all 189 B4 files
     * have one, though only 99 are non-blank; 138 of 311 B6 files do, of
     * which only 17 are non-blank), so
     * a size other than 0 or 2048 means our offsets are wrong, and we would
     * rather notice that than silently mis-parse. */
    size_t blocks_off = pat_off[pat_count];
    size_t blocks_end = blocks_off + (size_t)present_count * OLD_INSTR_BLOCK_SIZE;
    if (blocks_off > sz || blocks_end > sz) {
        free(out);
        return fail(errbuf, errlen, "old-format instrument data truncated");
    }

    size_t fmbank_off = 0;
    size_t trailer    = sz - blocks_end;
    if (trailer >= OLD_FMBANK_SIZE)
        fmbank_off = blocks_end;

    /* Does the bank hold anything? A file targeted at Roland carries the
     * 2048 bytes but leaves every record blank (space or NUL filled). */
    int fmbank_named = 0;
    if (fmbank_off) {
        for (int i = 0; i < OLD_FMBANK_SLOTS && !fmbank_named; i++) {
            for (int b = 0; b < 10; b++) {
                uint8_t c = raw[fmbank_off + (size_t)i * OLD_FMBANK_RECSIZE + b];
                if (c != ' ' && c != 0) { fmbank_named = 1; break; }
            }
        }
    }

    /* Resolve AUTO to the source the era's own driver used. */
    OldrldFmSource src = g_fm_source;
    if (src == OLDRLD_FM_AUTO)
        src = (gen == 0xB4) ? OLDRLD_FM_EDITOR : OLDRLD_FM_BLOCK;
    if (src == OLDRLD_FM_EDITOR && !fmbank_named)
        src = OLDRLD_FM_BLOCK;      /* nothing to read; fall back */

    /* ---- build the instrument table --------------------------------------
     * One entry per slot, present or not, so pattern instrument selectors
     * (which are slot numbers) index it directly. */
    Instrument tmp_instr[64];
    memset(tmp_instr, 0, sizeof(tmp_instr));
    uint16_t fm_count = 0, midi_count = 0, valid_count = 0, rhythm_count = 0;

    size_t block = blocks_off;
    for (int i = 0; i < slot_count; i++) {
        Instrument *ins = &tmp_instr[i];
        if (!slot_present[i]) {
            ins->valid = 0;
            strcpy(ins->name, "(invalid)");
            continue;
        }

        ins->offset = (uint32_t)block;
        ins->len    = OLD_INSTR_BLOCK_SIZE;

        /* --- the 256-byte block ---
         * The 16-byte OPL2 patch is NOT contiguous here: the 10-byte name is
         * embedded in the middle of it, so the block opens
         *
         *   +0x00..+0x07  adl[0..7]    (mod 20/40/60/80/E0, car 20/40/60)
         *   +0x08..+0x11  name[10]
         *   +0x12..+0x19  adl[8..15]   (car 80/E0, C0, then unused tail)
         *
         * i.e. exactly the OSL1 record's +0x2E..+0x3D patch and +0x0A name,
         * re-interleaved. Established by cross-referencing every old-format
         * instrument against the OSL1 corpus by name: 789 pairs agree on all
         * 11 live patch bytes under this split, and none agree under any
         * contiguous read. See OLD_RLD.md section 7. */
        for (int b = 0; b < 8; b++) {
            ins->adl[b]     = raw[block + 0x00 + b];
            ins->adl[8 + b] = raw[block + 0x12 + b];
        }
        /* Only adl[0..10] are live registers; the rest of the second half is
         * the block's own tail (often 0x20 fill) and would otherwise show up
         * as noise in dumps and in the FM/MIDI heuristic below. */
        for (int b = 11; b < 16; b++) ins->adl[b] = 0;
        rd_str(raw, block + 0x08, sz, ins->name, 10);
        block += OLD_INSTR_BLOCK_SIZE;

        /* --- the 64-byte editor record, if this file has a LIVE bank ---
         *
         * Guard on fmbank_named, not merely on fmbank_off. A blank bank is
         * space-filled, so +0x26 reads 0x20 rather than 0 and every record
         * would otherwise be reported as a percussion instrument: 3392 false
         * positives across the B6 corpus alone.
         *
         * Even in a live bank the field needs range-checking. The driver's
         * dispatch only recognises 6..10 (bass drum, snare, tom, cymbal,
         * hi-hat); other values do occur (0x14, 0x28, 0x32, 0x37, 0x4B, 0x55,
         * 0x5A in 9 B6 records, 0x20 in 17 B4 records) and are editor scratch
         * on the 0..100 percentage scale, not rhythm codes. Treat them as
         * melodic, which is what the driver's own bounds test does. */
        if (fmbank_named && i < OLD_FMBANK_SLOTS) {
            size_t rec = fmbank_off + (size_t)i * OLD_FMBANK_RECSIZE;
            uint8_t rhy = raw[rec + 0x26];
            ins->rhythm = (rhy >= 6 && rhy <= 10) ? rhy : 0;
            if (ins->rhythm) rhythm_count++;
            if (src == OLDRLD_FM_EDITOR) {
                editor_ops_to_adl(raw + rec + 0x0A, raw + rec + 0x17,
                                  raw[rec + 0x24], raw[rec + 0x25],
                                  ins->adl);
                char nm[16];
                rd_str_sp(raw, rec + 0x00, sz, nm, 10);
                if (nm[0]) { strncpy(ins->name, nm, sizeof(ins->name) - 1);
                             ins->name[sizeof(ins->name) - 1] = '\0'; }
            }
        }

        /* Default level: the slot table stores 0..0x3F and both era loaders
         * double it to 0..0x7E, clamping at 0x7F (ADLIB.DRV @0x0508). Before
         * this was parsed every old-format instrument played at full level. */
        {
            int v = slot_volume[i] * 2;
            ins->def_volume = (uint8_t)(v > 0x7F ? 0x7F : v);
        }

        /* The old format predates MED's multi-device support: there is no
         * synth-type or GM-program selector in either instrument source, so
         * every instrument is nominally an OPL2 patch. We still classify by
         * how much of the patch is actually populated, because a file
         * authored for Roland leaves the FM bytes as unused leftovers and
         * would otherwise be reported as playable Adlib. */
        ins->synth     = OSL1_SYNTH_FM_SHORT;
        ins->program   = 0;
        ins->transpose = 0;
        ins->finetune  = 0;

        int fm_nz = 0;
        for (int b = 0; b < 11; b++)
            if (ins->adl[b]) fm_nz++;
        ins->fm    = (fm_nz >= 4);
        ins->valid = 1;
        if (ins->fm) fm_count++; else midi_count++;
        valid_count++;
    }

    /* ---- commit: swap in the synthetic decompressed buffer, fill Song ---- */
    free(song->raw);
    song->raw  = out;
    song->size = out_size;

    song->version  = 0;
    song->constant = 0;
    song->gen      = 0;
    song->old_magic        = gen;
    song->old_fm_source    = (uint8_t)src;
    song->old_rhythm_instr = rhythm_count;
    strncpy(song->title, title, sizeof(song->title) - 1);
    song->title[sizeof(song->title) - 1] = '\0';
    song->block_off = 0;

    song->instr_count   = (uint16_t)slot_count;
    song->instr_size    = OLD_INSTR_BLOCK_SIZE;
    song->instr_tab_off = 0;    /* old format has no pointer table */
    song->instr_total = (uint16_t)slot_count;
    song->instr_valid = valid_count;
    song->fm_instr    = fm_count;
    song->midi_instr  = midi_count;
    memcpy(song->instr, tmp_instr, sizeof(tmp_instr));

    if (song->fm_instr == 0 && song->midi_instr == 0)
        song->kind = OSL1_KIND_UNKNOWN;
    else if (song->midi_instr == 0)
        song->kind = OSL1_KIND_ADLIB;
    else if (song->fm_instr == 0)
        song->kind = OSL1_KIND_MIDI;
    else
        song->kind = OSL1_KIND_MIXED;

    PatternBlock *blk = &song->blk;
    memset(blk, 0, sizeof(*blk));
    blk->block_off   = 0;
    blk->subtitle[0] = '\0';
    blk->restart_idx = restart_idx;
    blk->track_count = track_count;
    blk->row_count   = OLD_ROWS_PER_PAT;
    /* Both era drivers fix these rather than storing them: ADLIB.DRV programs
     * the PIT with divisor 0x5D37 (50 Hz) and does `mov byte [speed],6` at
     * install @0x017A; MED.EXE's old-format path does the same at 0x2374. */
    blk->tempo       = 50;
    blk->speed       = 6;
    blk->order_count = (uint8_t)order_len;
    for (int i = 0; i < order_len; i++)
        blk->order[i] = order[i];
    for (int p = 0; p < pat_count && p < OSL1_MAX_ORDER; p++)
        blk->pos_ptr[p] = (uint32_t)((size_t)p * rec_size);

    return 0;
}
