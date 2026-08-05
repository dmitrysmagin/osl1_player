/* oldfmt.c - the mechanical parts shared by oldrld.c and oldalb.c
 *
 * See oldfmt.h for what belongs here and what does not. In short: the header
 * bytes the two families agree on, the paragraph addressing scheme, the
 * 64-byte Adlib editor record, and the synthetic OSL1-shaped pattern buffer.
 */
#include "oldfmt.h"

#include <stdlib.h>
#include <string.h>

uint8_t oldfmt_generation(const uint8_t *raw, size_t size)
{
    if (size < 3) return 0;
    if (raw[1] == 0x9A && raw[2] == 0x01 && (raw[0] == 0xB4 || raw[0] == 0xB6))
        return raw[0];
    if (raw[0] == 0x20 && raw[1] == 0xAD && raw[2] == 0x01)
        return OLDFMT_GEN_ALB;
    return 0;
}

int oldfmt_is_old_format(const uint8_t *raw, size_t size)
{
    return oldfmt_generation(raw, size) != 0;
}

uint16_t oldfmt_u16(const uint8_t *p, size_t off, size_t size)
{
    if (off + 2 > size) return 0;
    return (uint16_t)(p[off] | (p[off + 1] << 8));
}

void oldfmt_str(const uint8_t *p, size_t off, size_t size,
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

void oldfmt_str_sp(const uint8_t *p, size_t off, size_t size,
                   char *dst, size_t field_len)
{
    oldfmt_str(p, off, size, dst, field_len);
    size_t n = strlen(dst);
    while (n > 0 && (dst[n - 1] == ' ' || dst[n - 1] == '\0')) n--;
    dst[n] = '\0';
}

int oldfmt_fail(char *errbuf, size_t errlen, const char *msg)
{
    if (errbuf && errlen) {
        strncpy(errbuf, msg, errlen - 1);
        errbuf[errlen - 1] = '\0';
    }
    return -1;
}

void oldfmt_read_header(const uint8_t *raw, size_t sz, OldFmtHeader *h)
{
    oldfmt_str(raw, OLDFMT_NAME_OFF, sz, h->title, OLDFMT_NAME_LEN);

    for (int i = 0; i < OLDFMT_ORDER_MAX; i++)
        h->order[i] = ((size_t)OLDFMT_ORDER_OFF + i < sz)
                    ? raw[OLDFMT_ORDER_OFF + i] : 0;

    /* order length = index of last non-zero byte + 1 (backward scan) */
    h->order_len = 0;
    for (int i = OLDFMT_ORDER_MAX - 1; i >= 0; i--) {
        if (h->order[i] != 0) { h->order_len = i + 1; break; }
    }
    if (h->order_len == 0) h->order_len = 1;  /* a song always plays one pos */

    /* pattern count = highest pattern number referenced + 1 */
    h->pat_count = 0;
    for (int i = 0; i < OLDFMT_ORDER_MAX; i++)
        if (h->order[i] > h->pat_count) h->pat_count = h->order[i];
    h->pat_count += 1;
}

int oldfmt_read_slot_table(const uint8_t *raw, size_t sz, int slot_count,
                           uint8_t *present, uint8_t *volume)
{
    int n = 0;
    for (int i = 0; i < slot_count; i++) {
        size_t eo = OLDFMT_SLOTTAB_OFF + (size_t)i * 2;
        present[i] = (eo     < sz) ? raw[eo]     : 0;
        volume[i]  = (eo + 1 < sz) ? raw[eo + 1] : 0;
        if (present[i]) n++;
    }
    return n;
}

/* Entry i is pattern i's start in 16-byte paragraphs, measured from the
 * table's own file offset. Entry pat_count (one past the last pattern) marks
 * the end of the pattern stream, which is where the instrument region begins -
 * both era drivers use exactly this (ADLIB.DRV install @0x0107: max_pattern,
 * inc, index para_table). */
int oldfmt_read_para(const uint8_t *raw, size_t sz, size_t para_off,
                     int pat_count, size_t *pat_off)
{
    if (para_off + 2 * (size_t)(pat_count + 1) > sz) return -1;
    for (int p = 0; p <= pat_count && p < OLDFMT_PARA_ENTRIES; p++)
        pat_off[p] = para_off
                   + (size_t)oldfmt_u16(raw, para_off + 2u * (size_t)p, sz) * 16;
    return 0;
}

/* Each synthetic record: [u16 len][u16 hdr = row_count][row data], where the
 * row data is row_count rows x 16 voice-slots x 8 bytes. That is the
 * uncompressed-position branch of decode_row() in replay.c verbatim, which is
 * the whole point of the exercise: neither old format needs to be taught to
 * the replay engine. */
uint8_t *oldfmt_alloc_patterns(int pat_count, size_t *out_size)
{
    size_t total = (size_t)pat_count * OLDFMT_REC_SIZE;
    uint8_t *out = calloc(1, total ? total : 1);
    if (!out) return NULL;

    for (int p = 0; p < pat_count; p++) {
        uint8_t *rec = out + (size_t)p * OLDFMT_REC_SIZE;
        rec[0] = (uint8_t)(OLDFMT_REC_DATA & 0xFF);
        rec[1] = (uint8_t)((OLDFMT_REC_DATA >> 8) & 0xFF);
        rec[2] = (uint8_t)(OLDFMT_ROWS_PER_PAT & 0xFF);
        rec[3] = (uint8_t)((OLDFMT_ROWS_PER_PAT >> 8) & 0xFF);
    }
    *out_size = total;
    return out;
}

uint8_t *oldfmt_row(uint8_t *patterns, int pat, int row)
{
    return patterns + (size_t)pat * OLDFMT_REC_SIZE + 4
                    + (size_t)row * OLDFMT_ROW_SLOTS * 8;
}

void oldfmt_editor_ops_to_adl(const uint8_t *mod, const uint8_t *car,
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

uint8_t oldfmt_rhythm_code(uint8_t raw_field)
{
    return (raw_field >= 6 && raw_field <= 10) ? raw_field : 0;
}

int oldfmt_mt32_sig(const uint8_t *p8)
{
    return p8[0] <= 3 && p8[2] <= 48 && p8[3] <= 100 &&
           p8[4] <= 24 && p8[5] <= 3 && p8[6] <= 1;
}

int oldfmt_classify_instrument(Instrument *ins, uint8_t slot_volume,
                               uint8_t device)
{
    /* Default level: the slot table stores 0..0x40 (0..64, an editor
     * percentage-style scale - the corpus maximum is exactly 64 in all three
     * generations) and both era loaders double it to 0..0x80, clamping at
     * 0x7F (ADLIB.DRV @0x0508). Before this was parsed every old-format
     * instrument played at full level. */
    int v = slot_volume * 2;
    ins->def_volume = (uint8_t)(v > 0x7F ? 0x7F : v);

    ins->synth     = device;
    ins->program   = 0;
    ins->transpose = 0;
    ins->finetune  = 0;

    if (device == OSL1_SYNTH_FM) {
        /* An FM patch still has to carry real operator data: a file authored
         * for another device leaves the FM bytes as unused leftovers. */
        int fm_nz = 0;
        for (int b = 0; b < 11; b++)
            if (ins->adl[b]) fm_nz++;
        ins->fm = (fm_nz >= 4);
    } else {
        ins->fm = 0;   /* Roland/other: the OPL2 cannot voice it */
    }

    /* Silence anything not playable on the OPL2 so its bytes are never voiced
     * as registers (the JINGLE bug: MT-32 timbre data read as OPL2 garbage). */
    if (!ins->fm) memset(ins->adl, 0, sizeof(ins->adl));

    ins->valid = 1;
    return ins->fm;
}

void oldfmt_commit(Song *song, const OldFmtHeader *h,
                   uint8_t *patterns, size_t patterns_size,
                   const OldFmtResult *r)
{
    free(song->raw);
    song->raw  = patterns;
    song->size = patterns_size;

    song->version  = 0;
    song->constant = 0;
    song->gen      = 0;
    song->old_magic        = r->gen;
    song->old_fm_source    = r->fm_source;
    song->old_rhythm_instr = r->rhythm_instr;
    strncpy(song->title, h->title, sizeof(song->title) - 1);
    song->title[sizeof(song->title) - 1] = '\0';
    song->block_off = 0;

    song->instr_count   = (uint16_t)r->slot_count;
    song->instr_size    = r->instr_size;
    song->instr_tab_off = 0;    /* old format has no pointer table */
    song->instr_total   = (uint16_t)r->slot_count;
    song->instr_valid   = r->valid_count;
    song->fm_instr      = r->fm_count;
    song->midi_instr    = r->midi_count;
    memcpy(song->instr, r->instr, (size_t)r->slot_count * sizeof(Instrument));

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
    blk->restart_idx = r->restart_idx;
    blk->track_count = r->track_count;
    blk->row_count   = OLDFMT_ROWS_PER_PAT;
    /* Both era drivers fix these rather than storing them: ADLIB.DRV programs
     * the PIT with divisor 0x5D37 (50 Hz) and does `mov byte [speed],6` at
     * install @0x017A; MED.EXE's old-format path does the same at 0x2374. */
    blk->tempo       = 50;
    blk->speed       = 6;
    blk->order_count = (uint8_t)h->order_len;
    for (int i = 0; i < h->order_len; i++)
        blk->order[i] = h->order[i];
    for (int p = 0; p < h->pat_count && p < OSL1_MAX_ORDER; p++)
        blk->pos_ptr[p] = (uint32_t)((size_t)p * OLDFMT_REC_SIZE);
}
