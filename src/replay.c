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

    /* Song setup @0x415: speed = block+0x2C, tempo = block+0x2A, restart order
     * = block+0x10. A positive `speed` argument (the CLI --speed override) wins;
     * otherwise use the file's value, falling back to 6 if the field is 0. */
    r->speed       = (uint8_t)(speed > 0 ? speed
                             : song->blk.speed ? song->blk.speed : 6);
    r->tick        = 0;
    r->order_idx   = 0;
    r->restart_idx = song->blk.restart_idx;
    r->order_len   = song->blk.order_count;
    r->pos         = 0;
    r->cursor      = 0;
    r->row_limit   = song->blk.row_count ? song->blk.row_count : 1;
    r->tempo       = song->blk.tempo ? song->blk.tempo : 50;  /* set_tempo @0x422 */

    /* Every voice starts pitch-centred (period 0x2000). A note-on resets this,
     * so it only matters if a portamento arrives before the first note. */
    for (int v = 0; v < REPLAY_MAX_VOICES; v++)
        r->voice[v].period = 0x2000;
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

/* Push a voice's engine volume (0..0x7F) to the device as OPL attenuation.
 * Routed through opl_dev_chanvol so every physical voice the logical channel
 * owns (primary + chord voices) tracks the same level, as ADLIB.DEV does. */
static void send_volume(Replay *r, opl_dev *dev, int v)
{
    if (dev) opl_dev_chanvol(dev, v, (r->voice[v].b[12] * 63) / 127);
}

/* Key one voice's primary note, resetting its portamento anchor. Mirrors the
 * ADLIB.DEV primary note-on (es:0x08 @0x475): self-free the channel's voices,
 * reset the period to centre (0x2000) and record the base note for tone porta,
 * then alloc+program+key. */
static void key_primary(Replay *r, opl_dev *dev, int v, uint8_t note,
                        const uint8_t *adl, int vol)
{
    RVoice *vc = &r->voice[v];
    vc->period    = 0x2000;
    vc->base_note = note;
    if (dev) {
        opl_dev_keyoff(dev, v);
        opl_dev_keyon(dev, v, note, adl, vol);
    }
}

/* tone-porta note setup @0x1386: target = 0x2000 + (note - base_note)*0x155,
 * masked to 14 bits; pick the slide direction, or clear the target when the
 * note already matches the current period. */
static void set_tone_porta_target(Replay *r, int v)
{
    RVoice *vc = &r->voice[v];
    int off = (int8_t)(uint8_t)(vc->b[1] - vc->base_note);   /* cbw of (note-base) */
    uint16_t target = (uint16_t)((0x2000 + off * 0x155) & 0x3fff);
    vc->tp_target = target;
    vc->tp_dir    = 0;                       /* default: slide period down    */
    if (target == vc->period)      vc->tp_target = 0;        /* already there  */
    else if (target > vc->period)  vc->tp_dir    = 1;        /* slide up       */
}

/* Row-side effect dispatch (jump table @0x115A). Runs once per voice when a row
 * is decoded, BEFORE the notes are keyed (matching trigger_note @0x1074), so the
 * retrigger setup (0x1F) can consume the primary note. Effects 0x01/0x02/0x03/
 * 0x0A/0x1E have no row-side action (they run per-tick); 0x04/0x05/0x07/0x08 map
 * to ADLIB.DEV vtable stubs (clc/retf) and so are faithfully state-only no-ops. */
static void dispatch_row_effect(Replay *r, opl_dev *dev, int v)
{
    RVoice  *vc  = &r->voice[v];
    uint8_t *b   = vc->b;
    uint8_t  cmd = b[6];
    uint8_t  prm = b[7];

    switch (cmd) {
    case 0x04:                                   /* @0x1402: es:0x28 stub    */
    case 0x05:                                   /* @0x130E: es:0x1C stub    */
    case 0x07:                                   /* @0x12D8: es:0x2C stub    */
    case 0x08:                                   /* @0x12E8: es:0x2C stub    */
        break;                                   /* state-only on Adlib      */

    case 0x06:                                   /* @0x12CC: note off        */
        b[6] = 0;
        if (dev) opl_dev_keyoff(dev, v);         /* free all voices of chan v */
        break;

    case 0x09: {                                 /* @0x1361: set speed       */
        uint8_t sp = prm & 0x1F;
        if (sp) { r->speed = sp; r->tick = 0; }
        break;
    }

    case 0x0B:                                   /* @0x133F: position jump   */
        r->jump_pending = 1;
        r->jump_order   = (uint8_t)(prm ? prm - 1 : 0);
        break;

    case 0x0C:                                   /* @0x1358: set volume      */
        b[12] = prm;
        b[6]  = 0;                               /* consumed (not per-tick)  */
        b[7]  = 0;
        send_volume(r, dev, v);
        break;

    case 0x0E:                                   /* @0x1351: pattern break   */
        r->break_pending = 1;
        b[6] = 0;
        break;

    case 0x0F: {                                 /* @0x1376: set tempo       */
        uint16_t t = prm;
        if (t) { if (t < 19) t = 19; r->tempo = t; } /* clamp @0x4B3: min 0x13 */
        break;
    }

    case 0x1F:                                   /* @0x15E8: retrigger setup */
        if (b[1]) {
            vc->retrig_note = b[1];
            /* [di+0xf] = param_hi (initial count) | param_lo<<4 (reload). */
            vc->fx_count = (uint8_t)((prm >> 4) | ((prm & 0x0F) << 4));
            b[1] = 0;                            /* keyed by per-tick 0x1F   */
        }
        break;

    default:                                     /* @0x119A stub: no-op      */
        break;
    }
}

/* trigger_note @0x1013: per voice, select the instrument and initial volume,
 * then (for a note-bearing row) dispatch the row effect and key the notes —
 * unless a tone portamento (0x03) rides with the note, in which case the note
 * only retargets the porta and is not keyed. */
static void trigger_row(Replay *r, opl_dev *dev)
{
    for (int v = 0; v < r->voice_count; v++) {
        RVoice        *vc  = &r->voice[v];
        uint8_t       *b   = vc->b;
        uint8_t        cmd = b[6];
        int            any_note = b[1] || b[2] || b[3] || b[4];
        const uint8_t *adl = NULL;

        /* Step 1 (@0x101a): instrument select + initial volume, only when a
         * selector AND at least one note are present. b[5] selects the patch
         * (file index = b[5]-2; verified against title.dro). A 0x0C riding with
         * the note sets the level (else full), and is consumed here. */
        if (b[5] != 0 && any_note) {
            if (b[5] >= 2) {
                int idx = b[5] - 2;
                if (idx >= 0 && idx < r->song->instr_total &&
                    r->song->instr[idx].valid)
                    adl = r->song->instr[idx].adl;
            }
            uint8_t vol = 0x7F;
            if (cmd == 0x0C) { vol = b[7]; b[6] = 0; b[7] = 0; cmd = 0; }
            b[12] = vol;
            send_volume(r, dev, v);
        }

        if (!any_note) {                         /* @0x104E: no note this row */
            dispatch_row_effect(r, dev, v);
            continue;
        }

        if (cmd == 0x03) {                       /* @0x106E: tone porta       */
            set_tone_porta_target(r, v);         /*   retarget, do NOT key    */
            dispatch_row_effect(r, dev, v);
            continue;
        }

        /* @0x1074: dispatch the row effect first (0x1F clears b[1] here), then
         * key the primary note (es:0x08) and up to three chord notes (es:0x0C),
         * all under the same logical channel id `v`. */
        dispatch_row_effect(r, dev, v);

        int vol = (b[12] * 63) / 127;
        if (b[1] != 0)
            key_primary(r, dev, v, b[1], adl, vol);
        for (int k = 2; k <= 4; k++)
            if (b[k] != 0 && dev)
                opl_dev_keyon(dev, v, b[k], adl, vol);
    }
}

/* tick_effects @0x10E3: per-tick processing on the non-row ticks (jump table
 * @0x1103). Implements porta up/down (0x01/0x02), tone porta (0x03), volume
 * slide (0x0A), arpeggio/strum (0x1E) and note retrigger (0x1F). */
static void tick_effects(Replay *r, opl_dev *dev)
{
    for (int v = 0; v < r->voice_count; v++) {
        RVoice  *vc = &r->voice[v];
        uint8_t *b  = vc->b;
        if (b[6] == 0 && b[7] == 0) continue;    /* @0x10E6: cmd|prm == 0     */

        switch (b[6]) {
        case 0x01:                               /* @0x129D: porta up         */
            vc->period = (uint16_t)(vc->period + b[7] * 20);
            if (dev) opl_dev_set_period(dev, v, vc->period);
            break;

        case 0x02:                               /* @0x12B0: porta down       */
            vc->period = (uint16_t)(vc->period - b[7] * 20);
            if (dev) opl_dev_set_period(dev, v, vc->period);
            break;

        case 0x03: {                             /* @0x13B5: tone porta       */
            if (b[7] != 0) { vc->tp_speed = b[7]; b[7] = 0; }  /* latch speed */
            if (vc->tp_target == 0) break;                     /* idle        */
            uint16_t per   = vc->period;
            uint16_t delta = (uint16_t)(vc->tp_speed * 20);
            if (vc->tp_dir) {                    /* slide up (add)            */
                per = (uint16_t)(per + delta);
                if (per < vc->tp_target) vc->period = per;
                else { vc->period = vc->tp_target; vc->tp_target = 0; }
            } else {                             /* slide down (sub)          */
                per = (uint16_t)(per - delta);
                if (per > vc->tp_target) vc->period = per;
                else { vc->period = vc->tp_target; vc->tp_target = 0; }
            }
            if (dev) opl_dev_set_period(dev, v, vc->period);
            break;
        }

        case 0x0A: {                             /* @0x1201: volume slide     */
            uint8_t hi = (uint8_t)(b[7] >> 4);
            int vol = b[12];
            if (hi) { vol += hi * 2;              if (vol > 0x7F) vol = 0x7F; }
            else    { vol -= (b[7] & 0x0F) * 2;   if (vol < 0)    vol = 0;    }
            b[12] = (uint8_t)vol;
            send_volume(r, dev, v);
            break;
        }

        case 0x1E: {                             /* @0x11A0: arpeggio/strum   */
            int use_primary = 1;                 /* dl bit 0x10 set (es:0x08) */
            if (b[1] == 0) {                     /* no fresh note: count down */
                uint8_t c = (uint8_t)(vc->fx_count - 1);
                vc->fx_count = c;
                if ((int8_t)c < 0)               /* underflowed -> reload+key */
                    use_primary = (b[7] & 0x10) != 0;
                else
                    break;
            }
            vc->fx_count = (uint8_t)(b[7] & 0x0F);
            uint8_t note = 0;
            for (int k = 1; k <= 4; k++)
                if (b[k]) { note = b[k]; b[k] = 0; break; }
            if (note) {
                int vol = (b[12] * 63) / 127;
                if (use_primary) key_primary(r, dev, v, note, NULL, vol);
                else if (dev)    opl_dev_keyon(dev, v, note, NULL, vol);
            }
            break;
        }

        case 0x1F: {                             /* @0x1611: note retrigger   */
            if ((vc->fx_count & 0x0F) > 1) { vc->fx_count--; break; }
            vc->fx_count |= (uint8_t)(vc->fx_count >> 4);   /* reload count    */
            if (vc->retrig_note) {
                int vol = (b[12] * 63) / 127;
                key_primary(r, dev, v, vc->retrig_note, NULL, vol);
            }
            break;
        }

        default:                                 /* @0x119A/0x1200: no-op     */
            break;
        }
    }
}

/* Advance the song position (0xFCB) honouring effect-driven jump/break. */
static void advance_position(Replay *r)
{
    int loop_event = 0;                          /* a wrap or backward jump  */

    if (r->jump_pending) {                       /* effect 0x0B (@0x133F)    */
        r->jump_pending = 0;
        uint8_t target = r->jump_order;
        if (target <= r->order_idx) loop_event = 1;  /* jump back == loop    */
        r->order_idx = target;
        r->pos = 0; r->cursor = 0;
        if (r->order_idx >= r->order_len) {
            r->order_idx = r->restart_idx;
            loop_event = 1;
        }
    } else if (r->break_pending || r->pos >= r->row_limit) {
        r->break_pending = 0;                    /* effect 0x0E ends the pos  */
        r->pos = 0; r->cursor = 0;
        if (++r->order_idx >= r->order_len) {
            r->order_idx = r->restart_idx;
            loop_event = 1;
        }
    }

    if (loop_event) {
        r->orders_played++;
        if (r->orders_played >= 1) r->finished = 1;  /* one pass for --wav   */
    }
}

void replay_tick(Replay *r, opl_dev *dev)
{
    if (r->finished) return;

    if (++r->tick < r->speed) {
        tick_effects(r, dev);                    /* non-row tick: run effects */
        return;
    }
    r->tick = 0;

    decode_row(r);
    trigger_row(r, dev);
    advance_position(r);
}
