/* oldalb.c - pre-OSL1 `.ALB` song loader, magic `20 AD 01`
 *
 * See oldalb.h for the on-disk layout and ALB.md for the full specification.
 * This is a clean-room reimplementation of what "ADLIB DRIVER (Version 3.00)",
 * September 1991, does in its INT 60h AX=2 "initialise tune data" call. The
 * driver is LAPMUSIC/OLDMUSIC/OLDMUSIC/PIT/ADLIB/ADLIB.EXE - a flat binary
 * behind a 32-paragraph MZ stub, so body offsets below are file offset - 0x200.
 *
 *   header copy and paragraph table   body 0x00E9-0x012D
 *   row decoder                       body 0x054C-0x0575
 *   cell handler                      body 0x05C6
 *
 * The layout was originally reversed from the corpus alone; ADLIB.EXE was only
 * found afterwards, and agrees with it exactly. ALB.md section 11 quotes the
 * code.
 *
 * The strategy is the one oldrld.c uses and for the same reason: decompress
 * every pattern once at load time into the byte shape decode_row() in replay.c
 * already understands for an uncompressed OSL1 position record, so the replay
 * engine never learns that this format exists. What the two loaders share is
 * in oldfmt.c; what is here is what makes .ALB its own format.
 */
#include "oldalb.h"
#include "oldfmt.h"

#include <stdlib.h>
#include <string.h>

/* ---- .ALB-only header fields --------------------------------------------
 * The two count bytes at 0x11A/0x11B are what make the format tractable: B4
 * and B6 leave this area zero, .ALB uses it to size the two variable-length
 * regions (the cue table before the paragraph table, and the instrument
 * records after the pattern stream). */
#define ALB_TRACKCNT_OFF      0x118
#define ALB_RESTART_OFF       0x119
#define ALB_NINSTR_OFF        0x11A  /* number of 64-byte editor records     */
#define ALB_NCUE_OFF          0x11B  /* entries in the cue table             */
#define ALB_CUETAB_OFF        0x158  /* cue table start; n_cue x 16 bytes    */
#define ALB_CELL_SIZE         4      /* note, instrument, effect cmd, param  */

/* The physical size of the slot table, which .ALB inherits from B4 even though
 * n_instr is what actually bounds it. */
#define ALB_SLOT_TABLE_SLOTS  32

/* .ALB rows carry the melodic tracks *and* the OPL2's five rhythm-mode
 * percussion channels (bass drum, snare, tom, cymbal, hi-hat), so a row has
 * track_count + 5 slots. See decode_alb_pattern(). */
#define ALB_RHYTHM_SLOTS      5

/* ------------------------------------------------------------------------
 * Pattern decoder
 *
 * A much plainer scheme than B4/B6's, and not a variant of it.
 *
 * Per row: a u16 presence mask followed by one fixed 4-byte cell per set bit,
 * in slot order. Bit 15 is slot 0, so the mask is scanned MSB-first, and a row
 * is therefore always 2 + 4 * popcount(mask) bytes. The cell is
 *
 *   +0  note (0 = none, else 17..96)     +2  effect command
 *   +1  instrument selector, 1-based     +3  effect parameter
 *
 * which drops straight into the engine's slot bytes with no translation: the
 * effect numbering is the same one B4/B6 use.
 *
 * The row has track_count + ALB_RHYTHM_SLOTS slots, not track_count. The five
 * extra slots are the OPL2's rhythm-mode percussion channels, which the 1991
 * ADLIB.DRV keeps permanently enabled. Three independent checks pin this down:
 *
 *   - no mask bit at or above track_count + 5 is ever set (0 violations),
 *     and files with 4, 5 and 6 tracks each stop exactly 5 slots later;
 *   - the instruments selected in slot track_count + k are precisely those
 *     whose editor record carries rhythm code 6 + k (32 of 33 cases; the
 *     names are BDRUM1, SNARE1, ... , HIHAT2, in that order);
 *   - the v3.00 driver says so outright. Its row reader takes the mask with a
 *     single lodsw, decodes [es:0xdd7] = header+0x118 = track_count melodic
 *     slots, then does `mov cx,5` and decodes five more (body 0x055F-0x0575).
 *     Its slot decoder is `shl dx,1 / jnc / movsw / movsw` - MSB-first, four
 *     bytes per set bit, an explicitly zeroed cell when the bit is clear.
 *
 * We decode the mask over all 16 bits regardless, because the payload has to
 * be consumed to stay in sync whatever the bit means.
 *
 * No period/chord bytes exist here, so nothing is written to cell[1..3]; the
 * engine's `any_note` test keys off cell[0] alone for these rows.
 *
 * Returns 0 on success, non-zero if the stream ran off the end of the file.
 */
static int decode_alb_pattern(const uint8_t *raw, size_t sz, size_t cursor,
                              uint8_t *patterns, int pat)
{
    for (int row = 0; row < OLDFMT_ROWS_PER_PAT; row++) {
        uint8_t *rowbuf = oldfmt_row(patterns, pat, row);

        if (cursor + 2 > sz) return -1;
        uint16_t mask = oldfmt_u16(raw, cursor, sz);
        cursor += 2;

        for (int slot = 0; slot < 16; slot++) {
            if (!((mask >> (15 - slot)) & 1)) continue;
            if (cursor + ALB_CELL_SIZE > sz) return -1;

            if (slot < OLDFMT_ROW_SLOTS) {
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

int oldalb_load(Song *song, char *errbuf, size_t errlen)
{
    const uint8_t *raw = song->raw;
    size_t sz = song->size;

    if (oldfmt_generation(raw, sz) != OLDFMT_GEN_ALB)
        return oldfmt_fail(errbuf, errlen, "not a `20 AD 01` .ALB file");

    if (ALB_NCUE_OFF >= sz)
        return oldfmt_fail(errbuf, errlen, ".ALB file too small");

    /* The paragraph table follows a variable-length cue table, so resolve it
     * before anything that depends on it. n_cue is a byte, so the table
     * cannot start past 0x158 + 0xFF0. */
    size_t para_off  = ALB_CUETAB_OFF + 16u * (size_t)raw[ALB_NCUE_OFF];
    int    n_records = raw[ALB_NINSTR_OFF];

    /* One editor record per slot, so more records than the slot table can
     * describe means we are reading the wrong byte, not an exotic file. The
     * corpus maximum is 24. */
    if (n_records > ALB_SLOT_TABLE_SLOTS)
        return oldfmt_fail(errbuf, errlen, ".ALB instrument count out of range");

    /* ---- header: name, order table, instrument slot table ---------------- */
    OldFmtHeader hdr;
    oldfmt_read_header(raw, sz, &hdr);

    /* .ALB stores exactly one instrument record per slot and says how many, so
     * the record count - not the table's physical 32 entries - is the number
     * of slots that mean anything. Entries past it are stale: UTOPIA/TITLE.ALB
     * declares 4 records but still has `present=1, volume=0` sitting in slots
     * 4..31 from whatever the file was edited down from. */
    int slot_count = n_records;
    uint8_t slot_present[OLDFMT_MAX_SLOTS], slot_volume[OLDFMT_MAX_SLOTS];
    oldfmt_read_slot_table(raw, sz, slot_count, slot_present, slot_volume);

    uint8_t track_count = (ALB_TRACKCNT_OFF < sz) ? raw[ALB_TRACKCNT_OFF] : 8;
    if (track_count == 0 || track_count > 8) track_count = 8;

    uint8_t restart_idx = (ALB_RESTART_OFF < sz) ? raw[ALB_RESTART_OFF] : 0;
    if (restart_idx >= hdr.order_len) restart_idx = 0;

    /* ---- paragraph offset table ------------------------------------------
     * Unlike B4/B6 this holds exactly pattern_count + 1 entries and stops, so
     * there is no fixed-size table to bounds-check against; the entries
     * actually read are the check. */
    size_t pat_off[OLDFMT_PARA_ENTRIES];
    if (oldfmt_read_para(raw, sz, para_off, hdr.pat_count, pat_off) != 0)
        return oldfmt_fail(errbuf, errlen, ".ALB paragraph table truncated");

    /* ---- pattern decompression ------------------------------------------- */
    size_t out_size = 0;
    uint8_t *out = oldfmt_alloc_patterns(hdr.pat_count, &out_size);
    if (!out) return oldfmt_fail(errbuf, errlen, "out of memory");

    int overrun = 0;
    for (int p = 0; p < hdr.pat_count && !overrun; p++) {
        size_t cursor = pat_off[p];
        if (cursor >= sz) { overrun = 1; break; }
        overrun = decode_alb_pattern(raw, sz, cursor, out, p);
    }

    if (overrun) {
        free(out);
        return oldfmt_fail(errbuf, errlen, ".ALB pattern data truncated");
    }

    /* ---- instrument source -----------------------------------------------
     * There are no 256-byte blocks at all: the pattern stream is followed
     * directly by n_records editor records, one per slot including the blank
     * ones, ending exactly at EOF. So B4/B6's "blocks then bank" shape
     * collapses to "records only", and the region is sized from the header
     * rather than from whatever happens to be left over. */
    size_t recs_off = pat_off[hdr.pat_count];
    if (recs_off > sz ||
        recs_off + (size_t)n_records * OLDFMT_EDREC_SIZE > sz) {
        free(out);
        return oldfmt_fail(errbuf, errlen, ".ALB instrument records truncated");
    }
    if (n_records == 0) recs_off = 0;

    /* Do the records hold anything? A file targeted at Roland carries the
     * bytes but leaves every record blank (space or NUL filled), and a blank
     * record's +0x26 reads 0x20 rather than 0, which would otherwise be
     * reported as a percussion instrument. */
    int recs_named = 0;
    if (recs_off) {
        for (int i = 0; i < n_records && !recs_named; i++) {
            for (int b = 0; b < 10; b++) {
                uint8_t c = raw[recs_off + (size_t)i * OLDFMT_EDREC_SIZE + b];
                if (c != ' ' && c != 0) { recs_named = 1; break; }
            }
        }
    }

    /* ---- build the instrument table --------------------------------------
     * One entry per slot, present or not, so pattern instrument selectors
     * (which are slot numbers) index it directly. */
    Instrument tmp_instr[OLDFMT_MAX_SLOTS];
    memset(tmp_instr, 0, sizeof(tmp_instr));
    uint16_t fm_count = 0, midi_count = 0, valid_count = 0, rhythm_count = 0;

    for (int i = 0; i < slot_count; i++) {
        Instrument *ins = &tmp_instr[i];
        if (!slot_present[i]) {
            ins->valid = 0;
            strcpy(ins->name, "(invalid)");
            continue;
        }

        /* No block to read; the editor record is the only instrument data the
         * file has, so point the dump offset at it. */
        size_t rec = recs_off + (size_t)i * OLDFMT_EDREC_SIZE;
        ins->offset = (uint32_t)rec;
        ins->len    = OLDFMT_EDREC_SIZE;

        if (recs_named && i < n_records) {
            ins->rhythm = oldfmt_rhythm_code(raw[rec + 0x26]);
            if (ins->rhythm) rhythm_count++;
            oldfmt_editor_ops_to_adl(raw + rec + 0x0A, raw + rec + 0x17,
                                     raw[rec + 0x24], raw[rec + 0x25],
                                     ins->adl);
            oldfmt_str_sp(raw, rec + 0x00, sz, ins->name, 10);
        }

        if (oldfmt_classify_instrument(ins, slot_volume[i])) fm_count++;
        else midi_count++;
        valid_count++;
    }

    /* ---- commit: swap in the synthetic decompressed buffer, fill Song ----
     * .ALB rows carry five percussion slots after the melodic tracks. medplay
     * has no rhythm-mode emulation, so rather than drop them we play them as
     * ordinary melodic voices - the patches in the file are real 2-operator
     * OPL2 patches, and opl_dev's allocator handles the extra channels. That
     * is exact for the bass drum (2-op in rhythm mode too) and an
     * approximation for the other four, which the chip drives with single
     * operators. See ALB.md section 8.
     *
     * There is no second instrument source to choose between, so the
     * --fm-source setting cannot apply here; the resolved source is always
     * EDITOR. */
    OldFmtResult res = {
        .gen          = OLDFMT_GEN_ALB,
        .fm_source    = OLDFMT_FM_EDITOR,
        .track_count  = (uint8_t)(track_count + ALB_RHYTHM_SLOTS),
        .restart_idx  = restart_idx,
        .rhythm_instr = rhythm_count,
        .instr_size   = OLDFMT_EDREC_SIZE,
        .slot_count   = slot_count,
        .valid_count  = valid_count,
        .fm_count     = fm_count,
        .midi_count   = midi_count,
        .instr        = tmp_instr,
    };
    oldfmt_commit(song, &hdr, out, out_size, &res);
    return 0;
}
