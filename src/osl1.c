/* osl1.c - Ocean OSL1 container parser (Phase 1)
 *
 * Clean-room re-implementation of the load-time parsing that MED.EXE performs
 * (RE-REPORT.md sections 4 and 8). Validated for parity against the reference
 * Python dumper osl1_dump.py on OD1.ALB, SHUTIT/TITLE.ADL and ALL.LAP.
 */
#include "osl1.h"
#include "oldrld.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- little-endian readers (bounds-checked against song->size) ----------- */
static uint16_t rd_u16(const uint8_t *p, size_t off, size_t size)
{
    if (off + 2 > size) return 0;
    return (uint16_t)(p[off] | (p[off + 1] << 8));
}

static uint32_t rd_u32(const uint8_t *p, size_t off, size_t size)
{
    if (off + 4 > size) return 0;
    return (uint32_t)p[off] | ((uint32_t)p[off + 1] << 8) |
           ((uint32_t)p[off + 2] << 16) | ((uint32_t)p[off + 3] << 24);
}

/* Copy a fixed-width, null-padded ASCII string and guarantee termination. */
static void rd_str(const uint8_t *p, size_t off, size_t size,
                   char *dst, size_t field_len)
{
    size_t i;
    for (i = 0; i < field_len; i++)
        dst[i] = (off + i < size) ? (char)p[off + i] : '\0';
    dst[field_len] = '\0';
    /* trim trailing NULs already implied; strip any trailing whitespace? no:
     * the reference dumper keeps embedded content verbatim, only rstrip \0. */
    while (field_len > 0 && dst[field_len - 1] == '\0')
        field_len--;
    dst[field_len] = '\0';
}

const char *osl1_device_name(Osl1Device dev)
{
    switch (dev) {
        case OSL1_DEV_ADLIB:  return "Adlib (Yamaha OPL2 FM)";
        case OSL1_DEV_SBLAST: return "Creative Sound Blaster";
        case OSL1_DEV_LAPC1:  return "Roland LAPC-I / MT-32 (MIDI)";
        case OSL1_DEV_SCC1:   return "Roland SCC-1 / Sound Canvas (GS)";
        case OSL1_DEV_SNES:   return "Super Nintendo S-DSP (FIR + echo)";
        default:              return "unknown device";
    }
}

const char *osl1_kind_name(Osl1Kind kind)
{
    switch (kind) {
        case OSL1_KIND_ADLIB: return "Adlib/OPL2 (renderable)";
        case OSL1_KIND_MIXED: return "Mixed FM + MIDI (partly renderable)";
        case OSL1_KIND_MIDI:  return "MIDI/program only (not OPL2-renderable)";
        default:              return "Unknown (no valid instruments)";
    }
}

const char *osl1_gen_name(uint8_t gen)
{
    switch (gen) {
        case OSL1_GEN_0: return "revision 0";
        case OSL1_GEN_2: return "revision 2";
        case OSL1_GEN_4: return "revision 4";
        default:         return "revision ? (unrecognised)";
    }
}

static int fail(char *errbuf, size_t errlen, const char *msg)
{
    if (errbuf && errlen) {
        strncpy(errbuf, msg, errlen - 1);
        errbuf[errlen - 1] = '\0';
    }
    return -1;
}

int osl1_load(const char *path, Song *song, char *errbuf, size_t errlen)
{
    memset(song, 0, sizeof(*song));

    FILE *f = fopen(path, "rb");
    if (!f) return fail(errbuf, errlen, "cannot open file");

    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz <= 0x50) { fclose(f); return fail(errbuf, errlen, "file too small"); }

    uint8_t *raw = malloc((size_t)fsz);
    if (!raw) { fclose(f); return fail(errbuf, errlen, "out of memory"); }
    if (fread(raw, 1, (size_t)fsz, f) != (size_t)fsz) {
        free(raw); fclose(f); return fail(errbuf, errlen, "short read");
    }
    fclose(f);

    song->raw  = raw;
    song->size = (size_t)fsz;
    const size_t sz = song->size;

    /* ---- header ---------------------------------------------------------- */
    if (memcmp(raw, "OSL1", 4) != 0) {
        /* Not an OSL1 container. Some LAPMUSIC/OLDMUSIC .RLD files predate
         * OSL1 and use a different magic (0xB6 0x9A 0x01); MED.EXE loads
         * those via a separate, older code path - see oldrld.c. */
        if (oldrld_is_old_format(raw, sz)) {
            int rc = oldrld_load(song, errbuf, errlen);
            if (rc != 0) osl1_free(song);
            return rc;
        }
        osl1_free(song);
        return fail(errbuf, errlen, "not an OSL1 file (bad signature)");
    }
    song->version     = raw[0x04];
    song->constant    = rd_u16(raw, 0x05, sz);
    song->gen         = raw[0x07];
    rd_str(raw, 0x28, sz, song->title, 30);
    song->block_off   = rd_u32(raw, 0x48, sz);
    song->instr_count = rd_u16(raw, 0x4C, sz);
    song->instr_tab_off = OSL1_INSTR_TAB_OFF;
    song->instr_size  = OSL1_INSTR_SPAN;

    /* ---- instrument pointer table @0x4E (one u32 entry per instrument) ---
     *
     * The table is `instr_count` file-absolute u32 record offsets starting at
     * 0x4E, and the records follow immediately after it. That invariant is
     * exact: instr[0].offset == 0x4E + 4*instr_count on 304 of the 322 OSL1
     * files in the corpus, and the other 18 simply have a NULL entry 0 (an
     * unused slot).
     *
     * This used to read the table from 0x50 with the offset at entry+2, i.e.
     * effectively from 0x52 - one entry too late. The consequence was that
     * every song lost its instrument 0 while the *last* table entry read
     * garbage from the first record's length field, and replay.c compensated
     * with `idx = b[5] - 2` instead of the correct `b[5] - 1`. The two errors
     * cancelled for selectors >= 2, so only instrument 0 was ever wrong -
     * which is why it survived the earlier parity checks. Found by diffing
     * medplay's register trace against test/ADLMUSIC/od1.dro: OD1.ALB's
     * track 0 uses selector 1 (instrument 0, "Dean Bdrum", transpose -24) and
     * medplay was dropping both the patch and the transpose, playing that
     * bass-drum line two octaves high. */
    uint16_t total = song->instr_count;
    if (total > OSL1_MAX_INSTR) total = OSL1_MAX_INSTR;

    uint16_t valid = 0;
    uint32_t last_valid_off = 0;
    int have_last = 0;

    for (uint16_t i = 0; i < total; i++) {
        size_t te = OSL1_INSTR_TAB_OFF + (size_t)i * 4;
        if (te + 4 > sz) { total = i; break; }

        uint32_t w1 = rd_u32(raw, te, sz);       /* offset (file-absolute) */
        Instrument *ins = &song->instr[i];
        ins->offset = w1;

        /* Validity: the pointer must clear the table itself, leave room for a
         * whole record, and be strictly increasing. A NULL entry (an unused
         * instrument slot) fails the first test and is reported invalid. */
        int ok = (w1 >= OSL1_INSTR_TAB_OFF + (uint32_t)song->instr_count * 4) &&
                 ((size_t)w1 + OSL1_INSTR_SPAN <= sz);
        if (ok && have_last && w1 <= last_valid_off) ok = 0;

        ins->valid = ok;
        if (ok) {
            ins->len = rd_u16(raw, w1, sz);
            ins->p1  = rd_u16(raw, (size_t)w1 + 4, sz);
            ins->p2  = rd_u16(raw, (size_t)w1 + 6, sz);
            rd_str(raw, (size_t)w1 + 0x0A, sz, ins->name, 20);
            /* Adlib device patch lives at record +0x2E (16 bytes). Verified
             * against ADLIB.DEV's operator programmer (0xD69) and the DRO
             * capture: the 16 bytes are mod 0x20/40/60/80/E0 (b0-4), carrier
             * 0x20/40/60/80/E0 (b5-9), 0xC0 (b10). The +0x1E block is a
             * different (non-Adlib) device sub-record. */
            for (int b = 0; b < 16; b++) {
                size_t db = (size_t)w1 + 0x2E + b;
                ins->adl[b] = (db < sz) ? raw[db] : 0;
            }

            /* ---- heuristic synth/renderability probe -------------------- *
             * +0x24 is a per-instrument synth-type code (2/4 = OPL2 FM,
             * 8 = MIDI/program). The decisive, robust signal is whether the
             * 11-byte OPL2 patch at +0x2E carries real operator data: FM
             * patches have ~7-9 non-zero bytes, MIDI/program records have 0-1
             * (just a GM program number at +0x30). Threshold of 4 cleanly
             * separates the two across the whole corpus. */
            ins->synth   = ((size_t)w1 + 0x24 < sz) ? raw[w1 + 0x24] : 0;
            ins->program = ((size_t)w1 + 0x30 < sz) ? raw[w1 + 0x30] : 0;
            /* +0x22 is a signed per-instrument note transpose in semitones
             * (e.g. COLUMBIA.ADL LOGDRUM1 = -24, "saw synth" = +12). The DOS
             * driver adds it to the pattern note before the OPL note->fnum/
             * block conversion; without it the drum kit plays two octaves high. */
            ins->transpose = ((size_t)w1 + 0x22 < sz) ? (int8_t)raw[w1 + 0x22] : 0;
            /* +0x20 is the per-instrument "FineTune" editor field (a signed
             * byte, MED.EXE editor range -99..+99), separate from the +0x22
             * transpose. It is deliberately NOT applied to pitch: both replay
             * drivers quantise pitch to whole semitones from the note number
             * (ADLIB.DEV note-on -> fixed fnum table @0x3B5 with the period
             * forced to 0x2000; SBLAST.DEV DoNoteOn -> rate from FreqTable),
             * so FineTune has no effect on playback. Parsed for display only. */
            ins->finetune = ((size_t)w1 + 0x20 < sz) ? (int8_t)raw[w1 + 0x20] : 0;
            int fm_nz = 0;
            for (int b = 0; b < 11; b++)
                if (ins->adl[b]) fm_nz++;
            ins->fm = (fm_nz >= 4);
            if (ins->fm) song->fm_instr++;
            else         song->midi_instr++;

            last_valid_off = w1;
            have_last = 1;
            valid++;
        } else {
            strcpy(ins->name, "(invalid)");
        }
    }
    song->instr_total = total;
    song->instr_valid = valid;

    /* ---- file-level classification from the instrument mix -------------- */
    if (song->fm_instr == 0 && song->midi_instr == 0)
        song->kind = OSL1_KIND_UNKNOWN;
    else if (song->midi_instr == 0)
        song->kind = OSL1_KIND_ADLIB;
    else if (song->fm_instr == 0)
        song->kind = OSL1_KIND_MIDI;
    else
        song->kind = OSL1_KIND_MIXED;

    /* ---- pattern block --------------------------------------------------- */
    PatternBlock *blk = &song->blk;
    blk->block_off = song->block_off;
    if (song->block_off >= sz) {
        osl1_free(song);
        return fail(errbuf, errlen, "pattern block offset beyond EOF");
    }
    size_t bb = song->block_off;   /* block base, file-absolute */

    rd_str(raw, bb + 0x00, sz, blk->subtitle, 16);
    blk->restart_idx = (bb + 0x10 < sz) ? raw[bb + 0x10] : 0;
    blk->track_count = rd_u16(raw, bb + 0x12, sz);
    blk->row_count   = rd_u16(raw, bb + 0x14, sz);
    for (int i = 0; i < 8; i++)
        blk->defaults[i] = rd_u16(raw, bb + 0x16 + (size_t)i * 2, sz);
    blk->checksum    = rd_u16(raw, bb + 0x26, sz);
    blk->ver_c       = rd_u16(raw, bb + 0x28, sz);
    blk->tempo       = rd_u16(raw, bb + 0x2A, sz);
    blk->speed       = (bb + 0x2C < sz) ? raw[bb + 0x2C] : 0;
    blk->order_count = (bb + 0x4E < sz) ? raw[bb + 0x4E] : 0;

    /* order table @block+0x50 */
    for (uint16_t i = 0; i < blk->order_count; i++) {
        size_t o = bb + 0x50 + i;
        blk->order[i] = (o < sz) ? raw[o] : 0;
    }
    /* position-data pointer table @block+0x150 (file-absolute u32s) */
    for (uint16_t i = 0; i < blk->order_count; i++)
        blk->pos_ptr[i] = rd_u32(raw, bb + 0x150 + (size_t)i * 4, sz);

    return 0;
}

void osl1_free(Song *song)
{
    if (!song) return;
    free(song->raw);
    song->raw = NULL;
    song->size = 0;
}
