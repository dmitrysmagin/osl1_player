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
} opl_dev;

/* Reset the chip and bring the device up. `opl3_mode` selects 18-voice OPL3.
 * Mirrors the ADLIB.DEV init routine (vtable es:08 -> 0x0475). */
void opl_dev_init(opl_dev *d, opl3_chip *chip, int opl3_mode);

/* Upload a 16-byte ADL patch to `voice` (vtable es:20 -> 0x0617, RE §5). */
void opl_dev_program(opl_dev *d, int voice, const uint8_t *adl);

/* Key a note on: note = block*12 + semitone (vtable es:14 -> 0x05EF). */
void opl_dev_note_on(opl_dev *d, int voice, int note);

/* Key the current note off — clears the KEYON bit (vtable es:10 -> 0x058A). */
void opl_dev_note_off(opl_dev *d, int voice);

/* Set channel volume 0..63 (carrier attenuation; effect 0x0C, RE B.5). */
void opl_dev_set_volume(opl_dev *d, int voice, int vol);

#endif /* MEDPLAY_OPL_DEV_H */
