/* replay.c - TRACKER.DRV replay engine (Phase 3)
 *
 * See replay.h. This is a deliberately literal port: variable names and byte
 * offsets follow the annotated disassembly so the behaviour can be diffed
 * against MED.EXE. decode_row decodes exactly one row per call.
 */
#include "replay.h"

#include <string.h>

/* ALIGN_TAB @0x14DA: header bytes needed to hold `bp` 2-bit codes. */
static const uint8_t ALIGN_TAB[17] = {
    1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4
};

static uint16_t ru16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* helper kept tiny so the window load below reads cleanly */
static int flagsbit(uint16_t flags, uint16_t mask) { return (flags & mask) != 0; }

void replay_init(Replay *r, const Song *song, int speed)
{
    memset(r, 0, sizeof(*r));
    r->song        = song;
    r->voice_count = (uint8_t)song->blk.track_count;
    if (r->voice_count > REPLAY_MAX_VOICES) r->voice_count = REPLAY_MAX_VOICES;
    r->speed       = (uint8_t)(speed > 0 ? speed : 6);
    r->tick        = 0;
    r->order_idx   = 0;
    r->restart_idx = 0;
    r->order_len   = song->blk.order_count;
    r->pos         = 0;
    r->cursor      = 0;
    r->row_limit   = song->blk.row_count ? song->blk.row_count : 1;

    /* Mark every voice's "last note" as none so the first note keys on. */
    for (int v = 0; v < REPLAY_MAX_VOICES; v++)
        r->voice[v].b[14] = 0xFF;
}

/* decode_cell @0x1526: pull a 2-bit code from the top of `*window` and fill
 * the 8-byte `cell` (= &voiceblock[1]). Advances the stream pointer. */
static void decode_cell(uint8_t *cell, const uint8_t **sp,
                        uint32_t *window, uint16_t flags)
{
    memset(cell, 0, 8);

    uint32_t w   = *window;
    int      code = (int)((w >> 30) & 3u);
    *window      = w << 2;

    if (code == 0) return;                       /* empty                  */

    const uint8_t *s = *sp;
    if (code == 1) {                             /* 2-byte param -> [5..6] */
        cell[5] = s[0];
        cell[6] = s[1];
        s += 2;
    } else if (code == 2) {                      /* two 1-byte -> [0],[4]  */
        cell[0] = s[0];
        cell[4] = s[1];
        s += 2;
    } else {                                     /* code 3: full event     */
        cell[0] = *s++;                          /* first byte             */
        if (!(flags & 0x1000)) {                 /* bit12 clear: period    */
            cell[1] = s[0];
            cell[2] = s[1];
            cell[3] = s[2];
            s += 3;
        }
        cell[4] = s[0];                          /* note (low)             */
        cell[5] = s[1];                          /* effect cmd (high)      */
        cell[6] = s[2];                          /* effect param           */
        s += 3;
        if (!(flags & 0x2000)) {                 /* bit13 clear: param 2   */
            cell[7] = *s++;
        }
    }
    *sp = s;
}

/* decode_row @0x1416: advance the row counter and decode one row's cells. */
static void decode_row(Replay *r)
{
    const Song *song = r->song;
    const PatternBlock *blk = &song->blk;

    r->pos++;                                    /* inc [si+0xA]           */

    uint8_t  pat  = blk->order[r->order_idx];    /* order table @block+0x50 */
    uint32_t poff = blk->pos_ptr[pat];           /* pos-ptr table @+0x150  */
    if (poff + 6 > song->size) return;           /* malformed: bail        */

    /* Each position record is [u16 length][u16 header][u16 bp][data...]. The
     * pos-ptr table points at the 2-byte length prefix (used by the editor to
     * skip positions); the replay header the engine decodes begins right after
     * it. Verified empirically: at pos_ptr+2 the header always has bit 0x8000
     * set and the bp word equals the position's cell/voice count (e.g. SAVAGE
     * .SCC bp=4, OD1.ALB bp=7). */
    const uint8_t *base = song->raw + poff + 2;  /* skip the length prefix */

    uint16_t hdr = ru16(base);
    if (!(hdr & 0x8000)) {
        /* Uncompressed position (raw-copy path @0x14eb): row_limit = hdr&0x7fff,
         * then copy 8 bytes/voice from base+2+cursor (the 2-byte header is
         * skipped via `add di,2`). The stream cursor advances a fixed 0x80 per
         * row (16 voice-slots * 8 bytes) regardless of voice_count. The flags
         * word (ds:0x15c0) is intentionally NOT touched here. */
        r->row_limit = hdr & 0x7FFF;
        const uint8_t *s = base + 2 + r->cursor;
        for (int v = 0; v < r->voice_count; v++) {
            if ((size_t)(s - song->raw) + 8 <= song->size)
                memcpy(&r->voice[v].b[1], s, 8);
            s += 8;
        }
        r->cursor = (uint16_t)(r->cursor + 0x80);
        return;
    }

    r->flags     = hdr;
    r->row_limit = hdr & 0x0FFF;

    int bp = ru16(base + 2);                     /* cells to decode        */
    const uint8_t *s = base + 4 + r->cursor;     /* resume in the stream   */

    /* Load the 32-bit code window (dx:bx, dx = high word, MSB-first). */
    uint32_t window;
    if (flagsbit(r->flags, 0x4000)) {            /* variable bit-alignment */
        uint32_t lo = ru16(s);
        uint32_t hi = ru16(s + 2);
        int idx = bp <= 16 ? bp : 16;
        int n   = ALIGN_TAB[idx];
        s += n;
        window = (hi << 16) | lo;
        window <<= (4 - n) * 8;                  /* preshift valid bits up */
    } else {
        uint32_t lo = ru16(s);
        uint32_t hi = ru16(s + 2);
        s += 4;
        window = (hi << 16) | lo;
    }

    for (int v = 0; v < r->voice_count; v++) {
        uint8_t *cell = &r->voice[v].b[1];
        if (bp > 0) {
            decode_cell(cell, &s, &window, r->flags);
            bp--;
        } else {
            memset(cell, 0, 8);
        }
    }

    r->cursor = (uint16_t)(s - (base + 4));
}

/* trigger_note @0x1013: key notes / apply volume for the freshly decoded row. */
static void trigger_row(Replay *r, opl_dev *dev)
{
    for (int v = 0; v < r->voice_count; v++) {
        RVoice *vc = &r->voice[v];
        uint8_t note = vc->b[5];
        uint16_t period = (uint16_t)(ru16(&vc->b[1]) | ru16(&vc->b[3]));

        if (note != 0 && period != 0) {
            uint8_t n = (uint8_t)(note - 1);         /* dec al              */
            if (n != vc->b[14]) {
                vc->b[14] = n;
                if (dev) opl_dev_note_on(dev, v, n);
            }
            uint8_t vol = 0x7F;
            if (vc->b[6] == 0x0C) {                  /* effect 0x0C = volume */
                vol = vc->b[7];
                vc->b[6] = 0;
                vc->b[7] = 0;
            }
            vc->b[12] = vol;
            /* scale the 0..0x7F engine volume to OPL's 0..63 attenuation. */
            if (dev) opl_dev_set_volume(dev, v, (vol * 63) / 127);
        }
    }
}

void replay_tick(Replay *r, opl_dev *dev)
{
    if (r->finished) return;

    if (++r->tick < r->speed)
        return;                                  /* same row: per-tick fx   */
    r->tick = 0;

    decode_row(r);
    trigger_row(r, dev);

    /* end-of-position / loop handling (0xFCB). */
    if (r->pos >= r->row_limit) {
        r->pos    = 0;
        r->cursor = 0;
        if (++r->order_idx >= r->order_len) {
            r->order_idx   = r->restart_idx;
            r->orders_played++;
            if (r->orders_played >= 1) r->finished = 1; /* one pass for --wav */
        }
    }
}
