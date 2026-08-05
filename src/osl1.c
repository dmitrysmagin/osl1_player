/* osl1.c - Ocean OSL1 container parser (Phase 1)
 *
 * Clean-room re-implementation of the load-time parsing that MED.EXE performs
 * (RE-REPORT.md sections 4 and 8). Validated for parity against the reference
 * Python dumper osl1_dump.py on OD1.ALB, SHUTIT/TITLE.ADL and ALL.LAP.
 */
#include "osl1.h"
#include "oldfmt.h"
#include "oldrld.h"
#include "oldalb.h"

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

/* ---- instrument records are containers of per-device variants ------------
 *
 * MED.EXE builds one of these by hand when it imports an old-format .RLD
 * (med.asm 0x25DC-0x26F2), which pins every field and every constant:
 *
 *   +0x00  u32  record length, EXCLUDING this field
 *   +0x04  u16  n_variants                        (med.asm 0x26BC writes 1)
 *   +0x06  u16  offset of variant 0, from +0x04   (0x26C1 writes 6)
 *   +0x08  u16  0                                 (0x26C7 writes 0)
 *   ...         n_variants-1 further 4-byte descriptors; the only multi-variant
 *               records in the corpus all carry `02 00 12 00` here, too small a
 *               sample to reverse, and nothing needs it - the variants are
 *               contiguous, so walking payload lengths finds them all.
 *   variants[], contiguous, each:
 *     +0x00  char[20] name                (0x2641 copies 10 bytes, pads 10)
 *     +0x14  u16      0xFFFF              (0x2653)
 *     +0x16  s8       finetune
 *     +0x18  s8       transpose           (TRACKER.DRV:10CD  es:[di+0x18])
 *     +0x19  u8       volume ceiling      (TRACKER.DRV:1236  es:[di+0x19])
 *     +0x1A  u8       device code         (0x266B writes 4 = Roland)
 *     +0x20  u32      payload length      (0x2685 writes 244 = MT-32 timbre)
 *     +0x24  payload  <- exactly the pointer D_InstInit gets (ADLIB.DEV:48)
 *
 * The drivers' own base pointer is the VARIANT, not the record: TRACKER.DRV
 * 0x15E1 does `add di,6` right after fetching the record. So record-relative
 * offsets are variant-relative plus 6, and for a single-variant record that
 * gives transpose at +0x22, the device code at +0x24 and the payload at +0x2E
 * - the three offsets medplay used to hard-code.
 *
 * Hard-coding them silently assumed one variant of one device. Walking the
 * chain instead finds the OPL2 variant when a record carries several, and far
 * more importantly notices when the record has no OPL2 variant at all.
 * Verified against the corpus: 6750 of 6757 records chain to exactly their
 * stated length (the 7 that do not are .SNS sample banks). */
#define VAR_FINETUNE   0x16
#define VAR_TRANSPOSE  0x18
#define VAR_SYNTH      0x1A
#define VAR_PAYLEN     0x20
#define VAR_HDR        0x24   /* bytes of variant header before the payload */

/* Is this device code one whose payload layout we know? Anything else means we
 * have mis-parsed, and the caller falls back to the pre-variant heuristic. */
static int known_device(uint8_t syn)
{
    return syn == OSL1_SYNTH_FM   || syn == OSL1_SYNTH_ROLAND ||
           syn == OSL1_SYNTH_SCC1 || syn == OSL1_SYNTH_SNES;
}

static void parse_instr_record(const uint8_t *raw, size_t sz,
                               size_t rec, Instrument *ins)
{
    ins->len = rd_u16(raw, rec, sz);
    ins->p1  = rd_u16(raw, rec + 0x04, sz);   /* n_variants        */
    ins->p2  = rd_u16(raw, rec + 0x06, sz);   /* descriptor 0, low */

    unsigned n = ins->p1;
    if (n < 1 || n > 8) n = 1;      /* nonsense count: read it as one variant */
    ins->n_variants = (uint8_t)n;

    /* Walk the chain, preferring an OPL2 variant over anything else. */
    size_t   var      = rec + 4 + 2 + 4 * (size_t)n;
    size_t   pick     = 0;
    uint8_t  pick_syn = 0;
    uint32_t pick_len = 0;

    for (unsigned v = 0; v < n; v++) {
        if (var + VAR_HDR > sz) break;
        uint8_t  syn = raw[var + VAR_SYNTH];
        uint32_t pl  = rd_u32(raw, var + VAR_PAYLEN, sz);

        if (!pick || (syn == OSL1_SYNTH_FM && pick_syn != OSL1_SYNTH_FM)) {
            pick = var; pick_syn = syn; pick_len = pl;
        }
        if (pl > sz || var + VAR_HDR + pl > sz) break;   /* chain broken */
        var += VAR_HDR + pl;
    }

    if (!pick) {                     /* record does not even hold a header */
        ins->name[0] = '\0';
        ins->fm = 0;
        return;
    }

    rd_str(raw, pick, sz, ins->name, 20);
    ins->finetune    = (pick + VAR_FINETUNE  < sz) ? (int8_t)raw[pick + VAR_FINETUNE]  : 0;
    ins->transpose   = (pick + VAR_TRANSPOSE < sz) ? (int8_t)raw[pick + VAR_TRANSPOSE] : 0;
    ins->synth       = pick_syn;
    ins->paylen      = pick_len;
    ins->payload_off = (uint32_t)(pick + VAR_HDR);

    size_t pay = pick + VAR_HDR;
    for (int b = 0; b < 16; b++) {
        size_t db = pay + (size_t)b;
        ins->adl[b] = (db < sz) ? raw[db] : 0;
    }

    if (known_device(pick_syn)) {
        /* The device code IS the answer; no guessing from the payload bytes.
         * Only an Adlib variant leaves a usable OPL2 patch in adl[]. */
        ins->fm = (pick_syn == OSL1_SYNTH_FM);
    } else {
        /* Unrecognised code: we are probably looking at a non-song file. Fall
         * back to the old "does this look like operator data" probe rather
         * than silently muting whatever it is. */
        int nz = 0;
        for (int b = 0; b < 11; b++) if (ins->adl[b]) nz++;
        ins->fm = (nz >= 4);
    }

    if (!ins->fm) memset(ins->adl, 0, sizeof(ins->adl));
    ins->program = (pick_syn == OSL1_SYNTH_SCC1 && pay + 2 < sz) ? raw[pay + 2] : 0;
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
        /* Not an OSL1 container. Some LAPMUSIC/OLDMUSIC files predate OSL1 and
         * use one of three older magics, in two families different enough to
         * have a loader each: `B4 9A 01` / `B6 9A 01` editor working files
         * (oldrld.c, RLD.md) and `20 AD 01` runtime exports (oldalb.c,
         * ALB.md). Both hand back the same OSL1-shaped Song. */
        uint8_t old_gen = oldfmt_generation(raw, sz);
        if (old_gen) {
            int rc = (old_gen == OLDFMT_GEN_ALB)
                   ? oldalb_load(song, errbuf, errlen)
                   : oldrld_load(song, errbuf, errlen);
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
            /* Parse the record as a chain of per-device variants and pick the
             * OPL2 one if present (parse_instr_record above). This replaces the
             * old fixed-offset read that silently assumed one Adlib variant and
             * so mis-classified every Roland/SCC1-only record as playable FM. */
            parse_instr_record(raw, sz, w1, ins);
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
