/* replay.h - TRACKER.DRV replay engine (Phase 3)
 *
 * Faithful C port of the 50 Hz tick/order engine and the compressed-row
 * decoder from TRACKER.DRV: replay_tick (0xF36), decode_row (0x1416),
 * decode_cell (0x1526), trigger_note (0x1013). See PLAN.md Appendix A and the
 * annotated disassembly.
 *
 * The per-voice runtime block mirrors the original byte layout so the decode
 * (which writes the 8-byte cell at voiceblock+1) and the trigger (which reads
 * note=vb+5, effect=vb+6, param=vb+7) line up exactly with the DOS engine.
 */
#ifndef MEDPLAY_REPLAY_H
#define MEDPLAY_REPLAY_H

#include <stdint.h>
#include "osl1.h"
#include "opl_dev.h"

#define REPLAY_MAX_VOICES 16
#define RVOICE_SIZE       24   /* >= the fields the engine touches (vb+0..+19) */

/* Per-voice runtime block. Field meaning by byte offset (matches the DOS
 * voice block; decode_cell @0x1526 writes the 8-byte cell into b[0..7]):
 *   b[0]      duration (rows the cell sustains)
 *   b[1]      note   (0 = rest; this is the audible pitch fed to es:0x08 @0x0494)
 *   b[2..3]   period word (chord/portamento pitch, es:0x0C extra voices)
 *   b[4]      chord/extra-voice byte
 *   b[5]      instrument selector (constant per track; file instr = b[5]-2,
 *             drives the es:0x14 patch-upload path @0x06F1)
 *   b[6]      effect command
 *   b[7]      effect parameter
 *   b[12]     current volume (0..0x7F)
 *   b[14]     last note played
 */
typedef struct { uint8_t b[RVOICE_SIZE]; } RVoice;

typedef struct {
    const Song *song;

    /* channel-block analogues (offsets noted from the 57-byte DOS block) */
    uint8_t  voice_count;   /* +0x01 */
    uint8_t  speed;         /* +0x02 ticks per row                         */
    uint8_t  tick;          /* +0x05 tick counter                          */
    uint8_t  order_idx;     /* +0x06 current order position                */
    uint8_t  restart_idx;   /* +0x07 loop-restart order position           */
    uint8_t  order_len;     /* +0x0E number of order entries               */
    uint16_t pos;           /* +0x0A row counter within the position       */
    uint16_t cursor;        /* +0x0C byte cursor into the position stream   */

    uint16_t flags;         /* 0x15C0 position header flags                */
    uint16_t row_limit;     /* 0x15C2 rows in this position (hdr & 0x0FFF) */

    /* Effect engine state (Phase 5). The DOS engine carries tempo on the PIT
     * (set_tempo @0x49D) and speed in the channel block (+0x02). */
    uint16_t tempo;         /* timer Hz; effect 0x0F. Default 50.           */
    uint8_t  jump_pending;  /* effect 0x0B (position jump) requested        */
    uint8_t  jump_order;    /* target order index for the pending jump      */
    uint8_t  break_pending; /* effect 0x0E (pattern break) requested        */

    int      orders_played; /* incremented each time the order index wraps */
    int      finished;      /* set after the song loops back to the start  */

    RVoice   voice[REPLAY_MAX_VOICES];
} Replay;

/* Prepare playback of `song` at `speed` ticks/row (0 -> default 6). */
void replay_init(Replay *r, const Song *song, int speed);

/* Advance one 50 Hz tick. On a row boundary this decodes the next row and
 * triggers notes/volume on `dev`; otherwise it is (currently) a no-op tick.
 * `dev` may be NULL to run the engine without producing audio. */
void replay_tick(Replay *r, opl_dev *dev);

#endif /* MEDPLAY_REPLAY_H */
