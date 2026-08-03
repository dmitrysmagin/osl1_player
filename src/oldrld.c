/* oldrld.c - pre-OSL1 `.RLD` song loader, generations B4 and B6
 *
 * See oldrld.h for the on-disk layout and RLD.md for the full specification.
 * This is a clean-room reimplementation of two loaders, neither of which reads
 * the other's files:
 *
 *   B6 (0xB6 0x9A 0x01)  what MED.EXE does at med.asm ~0x233b-0x2717.
 *   B4 (0xB4 0x9A 0x01)  what the April 1991 standalone driver
 *                        LAPMUSIC/OLDMUSIC/BSSJS/ADLIB.DRV does in its
 *                        install entry (offset 0x00EC-0x013C) and
 *                        inst_fetch (0x07B8) / set_operator_patch (0x0AD0).
 *                        Full annotation: BSSJS_ADLIB.DRV.annotated.asm.
 *
 * The third pre-OSL1 generation, `20 AD 01`, is a different enough format to
 * warrant its own translation unit - see oldalb.c. What the two share lives in
 * oldfmt.c.
 *
 * Strategy: rather than teaching the replay engine a second pattern format,
 * we decompress every pattern once at load time into the exact byte shape
 * decode_row() already understands for an *uncompressed* OSL1 position record
 * - [u16 length][u16 header (bit 0x8000 clear = row count)][row_count * 16
 * voices * 8 bytes] - and point blk->pos_ptr[pattern_number] at it (decode_row
 * indexes pos_ptr[] by the pattern number stored in order[], not by order
 * position, so this lines up directly with the old format's own "order table
 * of pattern numbers" model).
 *
 * Patterns are located through the file's own paragraph offset table rather
 * than by walking the stream and re-deriving the 16-byte padding, which is
 * what this loader used to do. The driver does the same (install @0x0114
 * indexes para_table by pattern number), it is robust against any padding
 * edge case, and it gives an exact, checkable end-of-stream address.
 */
#include "oldrld.h"
#include "oldfmt.h"

#include <stdlib.h>
#include <string.h>

#define OLD_INSTR_BLOCK_SIZE  256

/* Adlib editor bank: 32 fixed-size records, always the last 2048 bytes of a
 * B4 file (and of the 138 B6 files that carry one). */
#define OLD_FMBANK_SLOTS      32
#define OLD_FMBANK_SIZE       (OLD_FMBANK_SLOTS * OLDFMT_EDREC_SIZE)

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

static OldFmtFmSource g_fm_source = OLDFMT_FM_AUTO;

void oldrld_set_fm_source(OldFmtFmSource src) { g_fm_source = src; }

/* ------------------------------------------------------------------------
 * Pattern decoder
 *
 * Writes into `rows`, a zeroed 64 x 16 x 8 byte buffer laid out exactly like
 * the uncompressed-position branch of decode_row() in replay.c. Within a
 * slot's 8 bytes (which land at &RVoice.b[1]) the engine reads:
 *
 *   [0] primary note   [1..3] chord notes   [4] instrument selector, 1-based
 *   [5] effect command [6] effect param     [7] second param
 *
 * Per row a u16 code word holds a 2-bit code per track, taken MSB-first,
 * followed by each track's variable-length payload. Only the top
 * 2*track_count bits are ever examined (med.asm 0x24ca drives the extraction
 * loop from the track count; ADLIB.DRV's read_row @0x043A does the same from
 * hdr_trackcount) - not a hardcoded 8 tracks, even though most files have 8.
 *
 * Returns 0 on success, non-zero if the stream ran off the end of the file.
 */
static int decode_old_pattern(const uint8_t *raw, size_t sz, size_t cursor,
                              int track_count, uint8_t *patterns, int pat)
{
    for (int row = 0; row < OLDFMT_ROWS_PER_PAT; row++) {
        uint8_t *rowbuf = oldfmt_row(patterns, pat, row);

        if (cursor + 2 > sz) return -1;
        uint16_t word = oldfmt_u16(raw, cursor, sz);
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
            if (t < OLDFMT_ROW_SLOTS) memcpy(rowbuf + (size_t)t * 8, cell, 8);
        }
    }
    return 0;
}

int oldrld_load(Song *song, char *errbuf, size_t errlen)
{
    const uint8_t *raw = song->raw;
    size_t sz = song->size;

    uint8_t gen = oldfmt_generation(raw, sz);
    if (gen != 0xB4 && gen != 0xB6)
        return oldfmt_fail(errbuf, errlen, "not a B4/B6 old-format .RLD");

    const OldFmt *fmt = (gen == 0xB4) ? &FMT_B4 : &FMT_B6;
    size_t para_off = fmt->para_off;

    /* Smallest possible file is header + full paragraph table + one pattern. */
    if (sz <= para_off + OLDFMT_PARA_ENTRIES * 2)
        return oldfmt_fail(errbuf, errlen, "old-format file too small");

    /* ---- header: name, order table, instrument slot table ---------------- */
    OldFmtHeader hdr;
    oldfmt_read_header(raw, sz, &hdr);

    int slot_count = fmt->slot_count;
    uint8_t slot_present[OLDFMT_MAX_SLOTS], slot_volume[OLDFMT_MAX_SLOTS];
    int present_count = oldfmt_read_slot_table(raw, sz, slot_count,
                                               slot_present, slot_volume);

    uint8_t track_count = (fmt->trackcnt_off < sz) ? raw[fmt->trackcnt_off] : 8;
    if (track_count == 0 || track_count > 8) track_count = 8;

    uint8_t restart_idx = (fmt->restart_off < sz) ? raw[fmt->restart_off] : 0;
    if (restart_idx >= hdr.order_len) restart_idx = 0;

    /* ---- paragraph offset table ------------------------------------------ */
    size_t pat_off[OLDFMT_PARA_ENTRIES];
    if (oldfmt_read_para(raw, sz, para_off, hdr.pat_count, pat_off) != 0)
        return oldfmt_fail(errbuf, errlen, "old-format paragraph table truncated");

    /* ---- pattern decompression ------------------------------------------- */
    size_t out_size = 0;
    uint8_t *out = oldfmt_alloc_patterns(hdr.pat_count, &out_size);
    if (!out) return oldfmt_fail(errbuf, errlen, "out of memory");

    int overrun = 0;
    for (int p = 0; p < hdr.pat_count && !overrun; p++) {
        size_t cursor = pat_off[p];
        if (cursor >= sz) { overrun = 1; break; }
        overrun = decode_old_pattern(raw, sz, cursor, track_count, out, p);
    }

    if (overrun) {
        free(out);
        return oldfmt_fail(errbuf, errlen, "old-format pattern data truncated");
    }

    /* ---- instrument sources ----------------------------------------------
     * blocks_off  first 256-byte block; one per PRESENT slot, in slot order.
     * fmbank_off  the Adlib editor bank, or 0 if absent.
     *
     * The bank is identified positionally: it is what remains after the
     * blocks, and it is either exactly 2048 bytes or nothing. That holds for
     * every one of the 502 B4/B6 files in the corpus (all 189 B4 files have
     * one, though only 99 are non-blank; 138 of 313 B6 files do, of which only
     * 17 are non-blank), so a size other than 0 or 2048 means our offsets are
     * wrong, and we would rather notice that than silently mis-parse. */
    size_t blocks_off  = pat_off[hdr.pat_count];
    size_t blocks_end  = blocks_off
                       + (size_t)present_count * OLD_INSTR_BLOCK_SIZE;
    size_t fmbank_off  = 0;
    int    fmbank_recs = 0;

    if (blocks_off > sz || blocks_end > sz) {
        free(out);
        return oldfmt_fail(errbuf, errlen, "old-format instrument data truncated");
    }
    if (sz - blocks_end >= OLD_FMBANK_SIZE) {
        fmbank_off  = blocks_end;
        fmbank_recs = OLD_FMBANK_SLOTS;
    }

    /* Does the bank hold anything? A file targeted at Roland carries the
     * 2048 bytes but leaves every record blank (space or NUL filled). */
    int fmbank_named = 0;
    if (fmbank_off) {
        for (int i = 0; i < fmbank_recs && !fmbank_named; i++) {
            for (int b = 0; b < 10; b++) {
                uint8_t c = raw[fmbank_off + (size_t)i * OLDFMT_EDREC_SIZE + b];
                if (c != ' ' && c != 0) { fmbank_named = 1; break; }
            }
        }
    }

    /* Resolve AUTO to the source the era's own driver used. */
    OldFmtFmSource src = g_fm_source;
    if (src == OLDFMT_FM_AUTO)
        src = (gen == 0xB4) ? OLDFMT_FM_EDITOR : OLDFMT_FM_BLOCK;
    if (src == OLDFMT_FM_EDITOR && !fmbank_named)
        src = OLDFMT_FM_BLOCK;      /* nothing to read; fall back */

    /* ---- build the instrument table --------------------------------------
     * One entry per slot, present or not, so pattern instrument selectors
     * (which are slot numbers) index it directly. */
    Instrument tmp_instr[OLDFMT_MAX_SLOTS];
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
         *   +0x00..+0x07  adl[0..7]  (mod 20/40/60/80/E0, car 20/40/60)
         *   +0x08..+0x11  name[10]
         *   +0x12..+0x19  adl[8..15] (car 80/E0, C0, then unused tail)
         *
         * i.e. exactly the OSL1 record's +0x2E..+0x3D patch and +0x0A name,
         * re-interleaved. Established by cross-referencing every old-format
         * instrument against the OSL1 corpus by name: 789 pairs agree on all
         * 11 live patch bytes under this split, and none agree under any
         * contiguous read. See RLD.md section 7. */
        for (int b = 0; b < 8; b++) {
            ins->adl[b]     = raw[block + 0x00 + b];
            ins->adl[8 + b] = raw[block + 0x12 + b];
        }
        /* Only adl[0..10] are live registers; the rest of the second half is
         * the block's own tail (often 0x20 fill) and would otherwise show up
         * as noise in dumps and in the FM/MIDI heuristic. */
        for (int b = 11; b < 16; b++) ins->adl[b] = 0;
        oldfmt_str(raw, block + 0x08, sz, ins->name, 10);
        block += OLD_INSTR_BLOCK_SIZE;

        /* --- the 64-byte editor record, if this file has a LIVE bank ---
         *
         * Guard on fmbank_named, not merely on fmbank_off. A blank bank is
         * space-filled, so +0x26 reads 0x20 rather than 0 and every record
         * would otherwise be reported as a percussion instrument: 3392 false
         * positives across the B6 corpus alone. Even in a live bank the field
         * needs range-checking - see oldfmt_rhythm_code(). */
        if (fmbank_named && i < fmbank_recs) {
            size_t rec = fmbank_off + (size_t)i * OLDFMT_EDREC_SIZE;
            ins->rhythm = oldfmt_rhythm_code(raw[rec + 0x26]);
            if (ins->rhythm) rhythm_count++;
            if (src == OLDFMT_FM_EDITOR) {
                oldfmt_editor_ops_to_adl(raw + rec + 0x0A, raw + rec + 0x17,
                                         raw[rec + 0x24], raw[rec + 0x25],
                                         ins->adl);
                char nm[16];
                oldfmt_str_sp(raw, rec + 0x00, sz, nm, 10);
                if (nm[0]) { strncpy(ins->name, nm, sizeof(ins->name) - 1);
                             ins->name[sizeof(ins->name) - 1] = '\0'; }
            }
        }

        if (oldfmt_classify_instrument(ins, slot_volume[i])) fm_count++;
        else midi_count++;
        valid_count++;
    }

    /* ---- commit: swap in the synthetic decompressed buffer, fill Song ---- */
    OldFmtResult res = {
        .gen          = gen,
        .fm_source    = (uint8_t)src,
        .track_count  = track_count,
        .restart_idx  = restart_idx,
        .rhythm_instr = rhythm_count,
        .instr_size   = OLD_INSTR_BLOCK_SIZE,
        .slot_count   = slot_count,
        .valid_count  = valid_count,
        .fm_count     = fm_count,
        .midi_count   = midi_count,
        .instr        = tmp_instr,
    };
    oldfmt_commit(song, &hdr, out, out_size, &res);
    return 0;
}
