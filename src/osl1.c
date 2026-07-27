/* osl1.c - Ocean OSL1 container parser (Phase 1)
 *
 * Clean-room re-implementation of the load-time parsing that MED.EXE performs
 * (RE-REPORT.md sections 4 and 8). Validated for parity against the reference
 * Python dumper osl1_dump.py on OD1.ALB, SHUTIT/TITLE.ADL and ALL.LAP.
 */
#include "osl1.h"

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

const char *osl1_device_name(uint8_t device)
{
    switch (device) {
        case OSL1_DEV_GENERIC: return "Generic/LAP";
        case OSL1_DEV_ROLAND:  return "Roland RLD";
        case OSL1_DEV_MED_LAP: return "MED LAP";
        case OSL1_DEV_SCC:     return "Sound Blaster SCC";
        default:               return "Unknown";
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
        osl1_free(song);
        return fail(errbuf, errlen, "not an OSL1 file (bad signature)");
    }
    song->version     = raw[0x04];
    song->constant    = rd_u16(raw, 0x05, sz);
    song->device      = raw[0x07];
    rd_str(raw, 0x28, sz, song->title, 30);
    song->block_off   = rd_u32(raw, 0x48, sz);
    song->instr_count = rd_u16(raw, 0x4C, sz);
    song->instr_size  = rd_u16(raw, 0x4E, sz);

    /* ---- instrument pointer table @0x50 (one u32 entry per instrument) --- */
    uint16_t total = song->instr_count;
    if (total > OSL1_MAX_INSTR) total = OSL1_MAX_INSTR;

    uint16_t valid = 0;
    uint32_t last_valid_off = 0;
    int have_last = 0;

    for (uint16_t i = 0; i < total; i++) {
        size_t te = 0x50 + (size_t)i * 4;
        if (te + 4 > sz) { total = i; break; }

        uint16_t w1 = rd_u16(raw, te + 2, sz);   /* offset (file-absolute) */
        Instrument *ins = &song->instr[i];
        ins->offset = w1;

        /* Mirror the reference validity logic: pointer must clear the header,
         * leave room for instr_size bytes, and be strictly increasing. */
        int ok = (w1 > 0x4F) && ((size_t)w1 + song->instr_size <= sz);
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
            last_valid_off = w1;
            have_last = 1;
            valid++;
        } else {
            strcpy(ins->name, "(invalid)");
        }
    }
    song->instr_total = total;
    song->instr_valid = valid;

    /* ---- pattern block --------------------------------------------------- */
    PatternBlock *blk = &song->blk;
    blk->block_off = song->block_off;
    if (song->block_off >= sz) {
        osl1_free(song);
        return fail(errbuf, errlen, "pattern block offset beyond EOF");
    }
    size_t bb = song->block_off;   /* block base, file-absolute */

    rd_str(raw, bb + 0x00, sz, blk->subtitle, 16);
    blk->track_count = rd_u16(raw, bb + 0x12, sz);
    blk->row_count   = rd_u16(raw, bb + 0x14, sz);
    for (int i = 0; i < 8; i++)
        blk->defaults[i] = rd_u16(raw, bb + 0x16 + (size_t)i * 2, sz);
    blk->checksum    = rd_u16(raw, bb + 0x26, sz);
    blk->ver_c       = rd_u16(raw, bb + 0x28, sz);
    blk->format_id   = rd_u16(raw, bb + 0x2A, sz);
    blk->track_rel   = rd_u16(raw, bb + 0x2C, sz);
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
