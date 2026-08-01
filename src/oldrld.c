/* oldrld.c - pre-OSL1 "old format" song loader, generations B4, B6 and .ALB
 *
 * See oldrld.h for the on-disk layout. This is a clean-room reimplementation
 * of two loaders, plus one format reversed from the corpus alone:
 *
 *   B6 (0xB6 0x9A 0x01)  what MED.EXE does at med.asm ~0x233b-0x2717.
 *   B4 (0xB4 0x9A 0x01)  what the 1991 standalone driver
 *                        LAPMUSIC/OLDMUSIC/BSSJS/ADLIB.DRV does in its
 *                        install entry (offset 0x00EC-0x013C) and
 *                        inst_fetch (0x07B8) / set_operator_patch (0x0AD0).
 *                        Full annotation: BSSJS_ADLIB.DRV.annotated.asm.
 *   ALB (0x20 0xAD 0x01) has no surviving loader to copy: neither MED.EXE nor
 *                        ADLIB.DRV can read it (see the note above
 *                        decode_alb_pattern()). The layout below is derived
 *                        from the 45-file corpus and every claim it makes is
 *                        checkable - see pre-OSL1.md section 11.
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
 * B4 file (and of the 136 B6 files that carry one). .ALB reuses the record
 * layout but stores a variable number of them - see ALB_NINSTR_OFF. */
#define OLD_FMBANK_SLOTS      32
#define OLD_FMBANK_RECSIZE    64
#define OLD_FMBANK_SIZE       (OLD_FMBANK_SLOTS * OLD_FMBANK_RECSIZE)

/* ---- .ALB-only header fields -------------------------------------------
 * The two count bytes at 0x11A/0x11B are what make the format tractable: B4
 * and B6 leave this area zero, .ALB uses it to size the two variable-length
 * regions (the cue table before the paragraph table, and the instrument
 * records after the pattern stream). */
#define ALB_NINSTR_OFF        0x11A  /* number of 64-byte editor records     */
#define ALB_NCUE_OFF          0x11B  /* entries in the cue table             */
#define ALB_CUETAB_OFF        0x158  /* cue table start; n_cue x 16 bytes    */
#define ALB_CELL_SIZE         4      /* note, instrument, effect cmd, param  */

/* .ALB rows carry the melodic tracks *and* the OPL2's five rhythm-mode
 * percussion channels (bass drum, snare, tom, cymbal, hi-hat), so a row has
 * track_count + 5 slots. See decode_alb_pattern(). */
#define ALB_RHYTHM_SLOTS      5

/* Slots a synthetic row record holds, matching decode_row()'s uncompressed
 * branch in replay.c (which advances a fixed 0x80 = 16 * 8 bytes per row). */
#define OLD_ROW_SLOTS         16

/* Per-generation offsets. Everything from the track count onwards shifts by
 * 0x40 because B4 has 32 instrument slots and B6 has 64; .ALB has 32 slots
 * like B4 but leaves 0x0D8..0x117 as a zero gap and keeps B6's field
 * positions (verified: those 64 bytes are zero in all 45 .ALB files). */
typedef struct {
    uint8_t gen;            /* 0xB4 / 0xB6 / OLDRLD_GEN_ALB                 */
    int     slot_count;     /* 32 / 64 / 32                                 */
    size_t  trackcnt_off;   /* 0x0D8 / 0x118 / 0x118                        */
    size_t  restart_off;    /* 0x0D9 / 0x119 / 0x119                        */
    size_t  para_off;       /* 0x118 / 0x958 - also the paragraph origin.
                             * 0 for .ALB: computed from the cue count.     */
} OldFmt;

static const OldFmt FMT_B4  = { 0xB4, 32, 0x0D8, 0x0D9, 0x118 };
static const OldFmt FMT_B6  = { 0xB6, 64, 0x118, 0x119, 0x958 };
static const OldFmt FMT_ALB = { OLDRLD_GEN_ALB, 32, 0x118, 0x119, 0 };

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
    if (size < 3) return 0;
    if (raw[1] == 0x9A && raw[2] == 0x01 && (raw[0] == 0xB4 || raw[0] == 0xB6))
        return raw[0];
    if (raw[0] == 0x20 && raw[1] == 0xAD && raw[2] == 0x01)
        return OLDRLD_GEN_ALB;
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

/* ------------------------------------------------------------------------
 * Pattern decoders
 *
 * Both write into `rows`, a zeroed OLD_ROWS_PER_PAT x OLD_ROW_SLOTS x 8 byte
 * buffer laid out exactly like the uncompressed-position branch of
 * decode_row() in replay.c. Within a slot's 8 bytes (which land at
 * &RVoice.b[1]) the engine reads:
 *
 *   [0] primary note   [1..3] chord notes   [4] instrument selector, 1-based
 *   [5] effect command [6] effect param     [7] second param
 *
 * Each returns 0 on success, non-zero if the stream ran off the end of the
 * file.
 */

/* B4/B6: per row a u16 code word holding a 2-bit code per track, taken
 * MSB-first, followed by each track's variable-length payload. Only the top
 * 2*track_count bits are ever examined (med.asm 0x24ca drives the extraction
 * loop from the track count; ADLIB.DRV's read_row @0x043A does the same from
 * hdr_trackcount) - not a hardcoded 8 tracks, even though most files have 8. */
static int decode_old_pattern(const uint8_t *raw, size_t sz, size_t cursor,
                              int track_count, uint8_t *rows)
{
    for (int row = 0; row < OLD_ROWS_PER_PAT; row++) {
        uint8_t *rowbuf = rows + (size_t)row * OLD_ROW_SLOTS * 8;

        if (cursor + 2 > sz) return -1;
        uint16_t word = rd_u16(raw, cursor, sz);
        cursor += 2;

        for (int t = 0; t < track_count; t++) {
            int code = (word >> (14 - t * 2)) & 3;
            uint8_t cell[8] = {0};

            switch (code) {
            case 0:
                break;
            case 1:   /* -> effect cmd/param (cell[5],[6]) */
                if (cursor + 2 > sz) return -1;
                cell[5] = raw[cursor];
                cell[6] = raw[cursor + 1];
                cursor += 2;
                break;
            case 2:   /* -> note + instrument (cell[0],[4]) */
                if (cursor + 2 > sz) return -1;
                cell[0] = raw[cursor];
                cell[4] = raw[cursor + 1];
                cursor += 2;
                break;
            default:  /* code 3: full 7-byte event */
                if (cursor + 7 > sz) return -1;
                memcpy(cell, raw + cursor, 7);
                cursor += 7;
                break;
            }
            if (t < OLD_ROW_SLOTS) memcpy(rowbuf + (size_t)t * 8, cell, 8);
        }
    }
    return 0;
}

/* .ALB: a much plainer scheme, and not a variant of the B4/B6 one.
 *
 * Per row: a u16 presence mask followed by one fixed 4-byte cell per set bit,
 * in slot order. Bit 15 is slot 0, so the mask is scanned MSB-first, and a
 * row is therefore always 2 + 4 * popcount(mask) bytes. The cell is
 *
 *   +0  note (0 = none, else 17..96)     +2  effect command
 *   +1  instrument selector, 1-based     +3  effect parameter
 *
 * which drops straight into the engine's slot bytes with no translation: the
 * effect numbering is the same one B4/B6 use.
 *
 * The row has track_count + ALB_RHYTHM_SLOTS slots, not track_count. The five
 * extra slots are the OPL2's rhythm-mode percussion channels, which the 1991
 * ADLIB.DRV keeps permanently enabled. Two independent checks pin this down
 * across all 45 files:
 *
 *   - no mask bit at or above track_count + 5 is ever set (0 violations),
 *     and files with 4, 5 and 6 tracks each stop exactly 5 slots later;
 *   - the instruments selected in slot track_count + k are precisely those
 *     whose editor record carries rhythm code 6 + k (32 of 33 cases; the
 *     names are BDRUM1, SNARE1, ... , HIHAT2, in that order).
 *
 * We decode the mask over all 16 bits regardless, because the payload has to
 * be consumed to stay in sync whatever the bit means.
 *
 * No period/chord bytes exist here, so nothing is written to cell[1..3]; the
 * engine's `any_note` test keys off cell[0] alone for these rows. */
static int decode_alb_pattern(const uint8_t *raw, size_t sz, size_t cursor,
                              uint8_t *rows)
{
    for (int row = 0; row < OLD_ROWS_PER_PAT; row++) {
        uint8_t *rowbuf = rows + (size_t)row * OLD_ROW_SLOTS * 8;

        if (cursor + 2 > sz) return -1;
        uint16_t mask = rd_u16(raw, cursor, sz);
        cursor += 2;

        for (int slot = 0; slot < 16; slot++) {
            if (!((mask >> (15 - slot)) & 1)) continue;
            if (cursor + ALB_CELL_SIZE > sz) return -1;

            if (slot < OLD_ROW_SLOTS) {
                uint8_t *cell = rowbuf + (size_t)slot * 8;
                cell[0] = raw[cursor];         /* note                     */
                cell[4] = raw[cursor + 1];     /* instrument selector      */
                cell[5] = raw[cursor + 2];     /* effect command           */
                cell[6] = raw[cursor + 3];     /* effect parameter         */
            }
            cursor += ALB_CELL_SIZE;
        }
    }
    return 0;
}

int oldrld_load(Song *song, char *errbuf, size_t errlen)
{
    const uint8_t *raw = song->raw;
    size_t sz = song->size;

    uint8_t gen = oldrld_generation(raw, sz);
    if (gen == 0) return fail(errbuf, errlen, "not an old-format .RLD");

    int is_alb = (gen == OLDRLD_GEN_ALB);
    const OldFmt *fmt = is_alb    ? &FMT_ALB
                      : (gen == 0xB4) ? &FMT_B4 : &FMT_B6;

    /* The paragraph table sits at a fixed offset in B4/B6 but follows a
     * variable-length cue table in .ALB, so resolve it before anything that
     * depends on it. n_cue is a byte, so para_off <= 0x158 + 0xFF0. */
    size_t para_off = fmt->para_off;
    int    n_records = 0;
    if (is_alb) {
        if (ALB_NCUE_OFF >= sz)
            return fail(errbuf, errlen, ".ALB file too small");
        para_off  = ALB_CUETAB_OFF + 16u * (size_t)raw[ALB_NCUE_OFF];
        n_records = raw[ALB_NINSTR_OFF];
        /* One editor record per slot, so more records than the slot table can
         * describe means we are reading the wrong byte, not an exotic file.
         * The corpus maximum is 24. */
        if (n_records > fmt->slot_count)
            return fail(errbuf, errlen, ".ALB instrument count out of range");
    }

    /* Smallest possible file is header + full paragraph table + one pattern.
     * .ALB stores only as many paragraph entries as it needs, so it is
     * checked against the entries actually read instead (below). */
    if (!is_alb && sz <= para_off + OLD_PARA_ENTRIES * 2)
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

    /* .ALB stores exactly one instrument record per slot and says how many, so
     * the record count - not the table's physical 32 entries - is the number
     * of slots that mean anything. Entries past it are stale: UTOPIA/TITLE.ALB
     * declares 4 records but still has `present=1, volume=0` sitting in slots
     * 4..31 from whatever the file was edited down from. */
    int slot_count = is_alb ? n_records : fmt->slot_count;
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
    if (para_off + 2 * (size_t)(pat_count + 1) > sz)
        return fail(errbuf, errlen, "old-format paragraph table truncated");

    size_t pat_off[OLD_PARA_ENTRIES];
    for (int p = 0; p <= pat_count && p < OLD_PARA_ENTRIES; p++)
        pat_off[p] = para_off + (size_t)rd_u16(raw, para_off + 2u * p, sz) * 16;

    /* ---- pattern decompression -------------------------------------------
     * Each synthetic record: [u16 len][u16 hdr=row_count][row data].
     * Row data: row_count rows * 16 voice-slots * 8 bytes, matching the
     * uncompressed-position branch of decode_row() in replay.c verbatim. */
    size_t rec_data = (size_t)OLD_ROWS_PER_PAT * OLD_ROW_SLOTS * 8;  /* 0x2000 */
    size_t rec_size = 4 + rec_data;                                  /* 0x2004 */
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
        uint8_t *rows = recbase + 4;                    /* zeroed by calloc */

        size_t cursor = pat_off[p];
        if (cursor >= sz) { overrun = 1; break; }

        overrun = is_alb ? decode_alb_pattern(raw, sz, cursor, rows)
                         : decode_old_pattern(raw, sz, cursor, track_count, rows);
    }

    if (overrun) {
        free(out);
        return fail(errbuf, errlen, "old-format pattern data truncated");
    }

    /* ---- instrument sources ----------------------------------------------
     * blocks_off  first 256-byte block; one per PRESENT slot, in slot order.
     * fmbank_off  the Adlib editor bank, or 0 if absent.
     * fmbank_recs how many records that bank holds.
     *
     * For B4/B6 the bank is identified positionally: it is what remains after
     * the blocks, and it is either exactly 2048 bytes or nothing. That holds
     * for every one of the 500 B4/B6 files in the corpus (all 189 B4 files
     * have one, though only 99 are non-blank; 138 of 311 B6 files do, of
     * which only 17 are non-blank), so a size other than 0 or 2048 means our
     * offsets are wrong, and we would rather notice that than silently
     * mis-parse.
     *
     * .ALB has no 256-byte blocks at all - the pattern stream is followed
     * directly by n_records editor records, one per slot including the blank
     * ones, ending exactly at EOF. So the whole "blocks then bank" shape
     * collapses to "bank only", and the region is sized from the header
     * rather than from what happens to be left over. */
    size_t blocks_off  = pat_off[pat_count];
    size_t blocks_end  = blocks_off;
    size_t fmbank_off  = 0;
    int    fmbank_recs = 0;

    if (is_alb) {
        fmbank_recs = n_records;
        fmbank_off  = blocks_off;
        if (blocks_off > sz ||
            blocks_off + (size_t)fmbank_recs * OLD_FMBANK_RECSIZE > sz) {
            free(out);
            return fail(errbuf, errlen, ".ALB instrument records truncated");
        }
        if (fmbank_recs == 0) fmbank_off = 0;
    } else {
        blocks_end = blocks_off + (size_t)present_count * OLD_INSTR_BLOCK_SIZE;
        if (blocks_off > sz || blocks_end > sz) {
            free(out);
            return fail(errbuf, errlen, "old-format instrument data truncated");
        }
        if (sz - blocks_end >= OLD_FMBANK_SIZE) {
            fmbank_off  = blocks_end;
            fmbank_recs = OLD_FMBANK_SLOTS;
        }
    }

    /* Does the bank hold anything? A file targeted at Roland carries the
     * 2048 bytes but leaves every record blank (space or NUL filled). */
    int fmbank_named = 0;
    if (fmbank_off) {
        for (int i = 0; i < fmbank_recs && !fmbank_named; i++) {
            for (int b = 0; b < 10; b++) {
                uint8_t c = raw[fmbank_off + (size_t)i * OLD_FMBANK_RECSIZE + b];
                if (c != ' ' && c != 0) { fmbank_named = 1; break; }
            }
        }
    }

    /* Resolve AUTO to the source the era's own driver used. .ALB has only one
     * source, so the --fm-source setting cannot apply to it. */
    OldrldFmSource src = g_fm_source;
    if (is_alb) {
        src = OLDRLD_FM_EDITOR;
    } else {
        if (src == OLDRLD_FM_AUTO)
            src = (gen == 0xB4) ? OLDRLD_FM_EDITOR : OLDRLD_FM_BLOCK;
        if (src == OLDRLD_FM_EDITOR && !fmbank_named)
            src = OLDRLD_FM_BLOCK;      /* nothing to read; fall back */
    }

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

        if (is_alb) {
            /* No 256-byte block to read; the editor record below is the only
             * instrument data the file has, so point the dump offset at it. */
            ins->offset = (uint32_t)(fmbank_off + (size_t)i * OLD_FMBANK_RECSIZE);
            ins->len    = OLD_FMBANK_RECSIZE;
        } else {
            ins->offset = (uint32_t)block;
            ins->len    = OLD_INSTR_BLOCK_SIZE;

            /* --- the 256-byte block ---
             * The 16-byte OPL2 patch is NOT contiguous here: the 10-byte name
             * is embedded in the middle of it, so the block opens
             *
             *   +0x00..+0x07  adl[0..7]  (mod 20/40/60/80/E0, car 20/40/60)
             *   +0x08..+0x11  name[10]
             *   +0x12..+0x19  adl[8..15] (car 80/E0, C0, then unused tail)
             *
             * i.e. exactly the OSL1 record's +0x2E..+0x3D patch and +0x0A
             * name, re-interleaved. Established by cross-referencing every
             * old-format instrument against the OSL1 corpus by name: 789 pairs
             * agree on all 11 live patch bytes under this split, and none
             * agree under any contiguous read. See pre-OSL1.md section 7. */
            for (int b = 0; b < 8; b++) {
                ins->adl[b]     = raw[block + 0x00 + b];
                ins->adl[8 + b] = raw[block + 0x12 + b];
            }
            /* Only adl[0..10] are live registers; the rest of the second half
             * is the block's own tail (often 0x20 fill) and would otherwise
             * show up as noise in dumps and in the FM/MIDI heuristic below. */
            for (int b = 11; b < 16; b++) ins->adl[b] = 0;
            rd_str(raw, block + 0x08, sz, ins->name, 10);
            block += OLD_INSTR_BLOCK_SIZE;
        }

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
        if (fmbank_named && i < fmbank_recs) {
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

        /* Default level: the slot table stores 0..0x40 (0..64, an editor
         * percentage-style scale - the corpus maximum is exactly 64 in all
         * three generations) and both era loaders double it to 0..0x80,
         * clamping at 0x7F (ADLIB.DRV @0x0508). Before this was parsed every
         * old-format instrument played at full level. */
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
    song->instr_size    = is_alb ? OLD_FMBANK_RECSIZE : OLD_INSTR_BLOCK_SIZE;
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
    /* .ALB rows carry five percussion slots after the melodic tracks. medplay
     * has no rhythm-mode emulation, so rather than drop them we play them as
     * ordinary melodic voices - the patches in the file are real 2-operator
     * OPL2 patches, and opl_dev's allocator handles the extra channels. That
     * is exact for the bass drum (2-op in rhythm mode too) and an
     * approximation for the other four, which the chip drives with single
     * operators. See pre-OSL1.md section 11.4. */
    blk->track_count = (uint8_t)(is_alb ? track_count + ALB_RHYTHM_SLOTS
                                        : track_count);
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
