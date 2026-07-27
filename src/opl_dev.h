/* opl_dev.h - clean-room ADLIB.DEV backend (Phase 2)
 *
 * Re-implements the Adlib (OPL2) note/patch/volume layer that TRACKER.DRV
 * reaches through the ADLIB.DEV far-call vtable (PLAN.md Appendix B). Register
 * writes go to a Nuked-OPL3 chip via OPL3_WriteReg; Nuked handles all timing,
 * so the original OUT 0x388/0x389 delays are dropped.
 *
 * Voices: 9 melodic in OPL2 mode, 18 in OPL3 mode (upper 9 in the 0x100 bank).
 * The caller owns the opl3_chip (and SDL); this module owns register state.
 */
#ifndef MEDPLAY_OPL_DEV_H
#define MEDPLAY_OPL_DEV_H

#include <stdint.h>
#include <stdio.h>
#include "opl3.h"

#define OPL_DEV_MAX_VOICES 18

typedef struct {
    opl3_chip *chip;
    int        opl3;             /* 0 = OPL2 (9 voices), 1 = OPL3 (18)      */
    int        voice_count;      /* 9 or 18                                 */
    uint8_t    patch[OPL_DEV_MAX_VOICES][16]; /* per-voice ADL patch (0x729) */
    uint8_t    vol[OPL_DEV_MAX_VOICES];       /* per-voice volume 0..63 (0x682) */
    uint8_t    shadow_a0[OPL_DEV_MAX_VOICES]; /* reg 0xA0 shadow (0x355)    */
    uint8_t    shadow_b0[OPL_DEV_MAX_VOICES]; /* reg 0xB0 shadow (0x365)    */

    /* Dynamic voice allocator (ADLIB.DEV slot table @0x53C, 9 slots x 8B).
     * used[]  mirrors the slot's free-marker byte: 0 = free (0xFF), 1 = busy.
     * owner[] is the logical channel id a busy voice belongs to (-1 = free),
     * i.e. ADLIB.DEV's `bl` marker written to slot+0 at key-on. */
    uint8_t    used[OPL_DEV_MAX_VOICES];
    int16_t    owner[OPL_DEV_MAX_VOICES];

    FILE      *trace;            /* if set, log every register write here   */
} opl_dev;

/* Reset the chip and bring the device up. `opl3_mode` selects 18-voice OPL3.
 * Mirrors the ADLIB.DEV init routine (vtable es:08 -> 0x0475). */
void opl_dev_init(opl_dev *d, opl3_chip *chip, int opl3_mode);

/* ---- physical-voice layer (a `voice` is a concrete OPL channel 0..N-1) ---- */

/* Upload a 16-byte ADL patch to a physical `voice` (operator programmer @0xD69). */
void opl_dev_program(opl_dev *d, int voice, const uint8_t *adl);

/* Key a note on a physical `voice`: note = block*12 + semitone (pitch @0x3B5). */
void opl_dev_note_on(opl_dev *d, int voice, int note);

/* Key a physical `voice` off — clears the KEYON bit (@0x5B1). */
void opl_dev_note_off(opl_dev *d, int voice);

/* Set a physical `voice`'s volume 0..63 (carrier attenuation; RE B.5). */
void opl_dev_set_volume(opl_dev *d, int voice, int vol);

/* ---- dynamic allocation layer (faithful to ADLIB.DEV es:0x08/0x0C/0x10) ---
 * `chan` is the caller's logical channel id (ADLIB.DEV's `bl` slot marker).
 * A single logical channel may own several physical voices (chord notes),
 * all tagged with the same id, exactly as TRACKER.DRV's trigger_note does. */

/* Free every physical voice owned by `chan` and key it off (ADLIB.DEV @0x58F). */
void opl_dev_keyoff(opl_dev *d, int chan);

/* Allocate the first free physical voice (linear scan @0x50C), program `adl`
 * (may be NULL to keep the voice's current patch), set `vol` (0..63) and key on
 * `note` for logical channel `chan`. Returns the physical voice, or -1 if all
 * voices are busy — in which case the note is dropped, as the driver does (the
 * es:0x08 path bails on a full scan with no note-stealing). Mirrors the
 * self-free-then-alloc of the primary note-on (@0x475) only for the primary;
 * call this directly (without a preceding keyoff) for chord voices. */
int  opl_dev_keyon(opl_dev *d, int chan, int note, const uint8_t *adl, int vol);

/* Set the volume of every physical voice currently owned by `chan`. */
void opl_dev_chanvol(opl_dev *d, int chan, int vol);

/* Route every subsequent register write to `f` as `RRR=VV` text (one per line)
 * for diffing against a DOSBox OPL capture (PLAN.md §10). NULL disables. */
void opl_dev_set_trace(opl_dev *d, FILE *f);

#endif /* MEDPLAY_OPL_DEV_H */
