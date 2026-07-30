/* oldrld.c - pre-OSL1 "old format" .RLD loader
 *
 * See oldrld.h for the on-disk layout summary. This is a clean-room
 * reimplementation of the load-time parsing MED.EXE performs for files that
 * begin with the magic { 0xB6, 0x9A, 0x01 } (med.asm ~0x233b-0x2717),
 * reverse-engineered from disassembly plus empirical hex-dumps of
 * MEDIT/LAPMUSIC/OLDMUSIC/INFERNO.RLD and .../BSSJS/BSSADLIB.RLD.
 *
 * Strategy: rather than teaching the replay engine a second pattern format,
 * we decompress every old-format pattern once at load time into the exact
 * byte shape decode_row() already understands for an *uncompressed* OSL1
 * position record - [u16 length][u16 header (bit 0x8000 clear = row count)]
 * [row_count * 16 voices * 8 bytes] - and point blk->pos_ptr[pattern_number]
 * at it (decode_row indexes pos_ptr[] by the pattern number stored in
 * order[], not by order position, so this lines up directly with the old
 * format's own "order table of pattern numbers" model).
 */
#include "oldrld.h"

#include <stdlib.h>
#include <string.h>

#define OLD_NAME_OFF          0x03
#define OLD_NAME_LEN          20
#define OLD_ORDER_OFF         0x18
#define OLD_ORDER_MAX         128
#define OLD_INSTR_OFF         0x98
#define OLD_INSTR_MAX         64
#define OLD_TRACKCNT_OFF      0x118
#define OLD_PATTERN_OFF       0xB58
#define OLD_INSTR_BLOCK_SIZE  256
#define OLD_ROWS_PER_PAT      64

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

static int fail(char *errbuf, size_t errlen, const char *msg)
{
    if (errbuf && errlen) {
        strncpy(errbuf, msg, errlen - 1);
        errbuf[errlen - 1] = '\0';
    }
    return -1;
}

int oldrld_is_old_format(const uint8_t *raw, size_t size)
{
    return size >= 3 && raw[0] == 0xB6 && raw[1] == 0x9A && raw[2] == 0x01;
}

int oldrld_load(Song *song, char *errbuf, size_t errlen)
{
    const uint8_t *raw = song->raw;
    size_t sz = song->size;

    if (sz <= OLD_PATTERN_OFF)
        return fail(errbuf, errlen, "old-format file too small");

    /* ---- header: name, order table, instrument presence table ----------- */
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
    if (order_len == 0) order_len = 1;   /* a song always plays at least one position */

    /* pattern count = highest pattern number referenced + 1 */
    int pat_count = 0;
    for (int i = 0; i < OLD_ORDER_MAX; i++)
        if (order[i] > pat_count) pat_count = order[i];
    pat_count += 1;

    uint8_t instr_present[OLD_INSTR_MAX];
    for (int i = 0; i < OLD_INSTR_MAX; i++) {
        size_t eo = OLD_INSTR_OFF + (size_t)i * 2;
        instr_present[i] = (eo < sz) ? raw[eo] : 0;
    }

    uint8_t track_count = (OLD_TRACKCNT_OFF < sz) ? raw[OLD_TRACKCNT_OFF] : 8;
    if (track_count == 0 || track_count > 8) track_count = 8;

    /* ---- pattern decompression -------------------------------------------
     * Each synthetic record: [u16 len][u16 hdr=row_count][row data].
     * Row data: row_count rows * 16 voice-slots * 8 bytes, matching the
     * uncompressed-position branch of decode_row() in replay.c verbatim. */
    size_t rec_data = (size_t)OLD_ROWS_PER_PAT * 16 * 8;   /* 0x2000 */
    size_t rec_size = 4 + rec_data;                        /* 0x2004 */
    size_t out_size = (size_t)pat_count * rec_size;

    uint8_t *out = calloc(1, out_size ? out_size : 1);
    if (!out) return fail(errbuf, errlen, "out of memory");

    size_t cursor = OLD_PATTERN_OFF;     /* file read cursor, file-absolute */
    int overrun = 0;

    for (int p = 0; p < pat_count && !overrun; p++) {
        uint8_t *recbase = out + (size_t)p * rec_size;
        recbase[0] = (uint8_t)(rec_data & 0xFF);
        recbase[1] = (uint8_t)((rec_data >> 8) & 0xFF);
        recbase[2] = (uint8_t)(OLD_ROWS_PER_PAT & 0xFF);
        recbase[3] = (uint8_t)((OLD_ROWS_PER_PAT >> 8) & 0xFF);
        uint8_t *rows = recbase + 4;

        /* Per-pattern byte accumulator (med.asm 0x40aa): reset to 0 at the
         * start of every pattern (confirmed: `movw $0x0,0x40aa` at 0x2473,
         * immediately before the 64-row loop) - it is NOT a running total
         * across the whole song. */
        uint32_t acc = 0;

        for (int row = 0; row < OLD_ROWS_PER_PAT; row++) {
            uint8_t *rowbuf = rows + (size_t)row * 16 * 8;  /* pre-zeroed by calloc */

            if (cursor + 2 > sz) { overrun = 1; break; }
            uint16_t word = rd_u16(raw, cursor, sz);
            cursor += 2; acc += 2;

            /* The code word is always a fixed 2-byte read, but only the top
             * 2*track_count bits are ever examined (med.asm 0x24ca: `mov
             * 0x3fc1,%cl` drives the extraction loop count) - not a
             * hardcoded 8 tracks, even though our two samples both have
             * track_count==8. */
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
                    cursor += 2; acc += 2;
                    break;
                case 2:   /* -> note + instrument (cell[0],[4]) */
                    if (cursor + 2 > sz) { overrun = 1; break; }
                    cell[0] = raw[cursor];
                    cell[4] = raw[cursor + 1];
                    cursor += 2; acc += 2;
                    break;
                default:  /* code 3: full 7-byte event */
                    if (cursor + 7 > sz) { overrun = 1; break; }
                    cell[0] = raw[cursor];
                    cell[1] = raw[cursor + 1];
                    cell[2] = raw[cursor + 2];
                    cell[3] = raw[cursor + 3];
                    cell[4] = raw[cursor + 4];
                    cell[5] = raw[cursor + 5];
                    cell[6] = raw[cursor + 6];
                    cursor += 7; acc += 7;
                    break;
                }
                if (overrun) break;
                if (t < track_count)
                    memcpy(rowbuf + (size_t)t * 8, cell, 8);
            }
            if (overrun) break;
        }
        if (overrun) break;

        /* 16-byte alignment (med.asm 0x2486-0x2499): pad the file cursor
         * forward to the next multiple of 16 bytes of THIS pattern's own
         * (just-reset) accumulator, skipped entirely if already aligned.
         * Runs unconditionally after every pattern, including the last -
         * confirmed by disassembly: the seek is inside the same outer `loop`
         * iteration that decrements the pattern counter, with no special
         * case for the final iteration. */
        uint32_t pad = (16 - (acc & 15)) & 15;
        cursor += pad;
    }

    if (overrun) {
        free(out);
        return fail(errbuf, errlen, "old-format pattern data truncated");
    }

    /* ---- instrument blocks: one 256-byte block per PRESENT entry, read
     * sequentially from wherever pattern decompression left the cursor ---- */
    Instrument tmp_instr[OLD_INSTR_MAX];
    memset(tmp_instr, 0, sizeof(tmp_instr));
    uint16_t fm_count = 0, midi_count = 0, valid_count = 0;

    for (int i = 0; i < OLD_INSTR_MAX; i++) {
        Instrument *ins = &tmp_instr[i];
        if (!instr_present[i]) {
            ins->valid = 0;
            strcpy(ins->name, "(invalid)");
            continue;
        }
        if (cursor + OLD_INSTR_BLOCK_SIZE > sz) {
            free(out);
            return fail(errbuf, errlen, "old-format instrument data truncated");
        }
        size_t rb = cursor;

        ins->offset = (uint32_t)rb;
        ins->len    = OLD_INSTR_BLOCK_SIZE;
        ins->synth  = raw[rb + 0x00];
        rd_str(raw, rb + 0x08, sz, ins->name, 10);
        for (int b = 0; b < 16; b++)
            ins->adl[b] = raw[rb + 0x2E + b];
        ins->transpose = 0;   /* no reliable transpose field in this format */
        ins->finetune  = 0;
        {
            size_t po = rb + 0x30;
            ins->program = (po < sz) ? raw[po] : 0;
        }

        int fm_nz = 0;
        for (int b = 0; b < 11; b++)
            if (ins->adl[b]) fm_nz++;
        ins->fm    = (fm_nz >= 4);
        ins->valid = 1;
        if (ins->fm) fm_count++; else midi_count++;
        valid_count++;

        cursor += OLD_INSTR_BLOCK_SIZE;
    }

    /* Note: `cursor` does not always land exactly at `sz` here - many source
     * files carry a trailing block of unused, space-filled (0x20) reserved
     * space after the last real instrument record (verified: e.g.
     * FLOOR.RLD and DEMO/NOREWARD.RLD both end with a run of 0x20 bytes with
     * no further structure). MED.EXE's loader closes the file right after
     * the last present instrument, same as here, and never reads that
     * trailer - so this is expected and not a truncation error. */

    /* ---- commit: swap in the synthetic decompressed buffer, fill Song ---- */
    free(song->raw);
    song->raw  = out;
    song->size = out_size;

    song->version  = 0;
    song->constant = 0;
    song->gen      = 0;
    strncpy(song->title, title, sizeof(song->title) - 1);
    song->title[sizeof(song->title) - 1] = '\0';
    song->block_off = 0;

    song->instr_count   = OLD_INSTR_MAX;
    song->instr_size    = OLD_INSTR_BLOCK_SIZE;
    song->instr_tab_off = 0;    /* old format has no pointer table */
    song->instr_total = OLD_INSTR_MAX;
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
    blk->restart_idx = 0;
    blk->track_count = track_count;
    blk->row_count   = OLD_ROWS_PER_PAT;
    blk->tempo       = 50;  /* fixed by the old-format loader (med.asm 0x2374) */
    blk->speed       = 6;
    blk->order_count = (uint8_t)order_len;
    for (int i = 0; i < order_len; i++)
        blk->order[i] = order[i];
    for (int p = 0; p < pat_count && p < OSL1_MAX_ORDER; p++)
        blk->pos_ptr[p] = (uint32_t)((size_t)p * rec_size);

    return 0;
}
