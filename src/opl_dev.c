/* opl_dev.c - clean-room ADLIB.DEV backend (Phase 2)
 *
 * See opl_dev.h and PLAN.md Appendix B. Maps the Adlib note/patch/volume model
 * onto Nuked-OPL3 register writes.
 */
#include "opl_dev.h"

#include <string.h>

/* Adlib note -> OPL F-number table (ADLIB.DEV @0xF61, base 0x157). One entry
 * per semitone; the block (octave) field handles the octave scaling. */
static const uint16_t FNUM[12] = {
    0x157, 0x16C, 0x181, 0x198, 0x1B1, 0x1CB,
    0x1E6, 0x203, 0x222, 0x243, 0x266, 0x28A
};

/* Per-channel modulator operator offset (standard OPL melodic mode, ch 0-8). */
static const uint8_t OP_OFF[9] = {
    0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12
};

/* Register bank for a voice: voices 0-8 -> bank 0, 9-17 -> 0x100 (OPL3). */
static uint16_t voice_bank(int voice) { return voice >= 9 ? 0x100 : 0x000; }

static void wr(opl_dev *d, uint16_t reg, uint8_t val)
{
    OPL3_WriteReg(d->chip, reg, val);
}

void opl_dev_init(opl_dev *d, opl3_chip *chip, int opl3_mode)
{
    memset(d, 0, sizeof(*d));
    d->chip        = chip;
    d->opl3        = opl3_mode ? 1 : 0;
    d->voice_count = opl3_mode ? 18 : 9;

    for (int v = 0; v < OPL_DEV_MAX_VOICES; v++)
        d->vol[v] = 0x3F;                 /* full volume (0x682 init analogue) */

    if (d->opl3)
        wr(d, 0x105, 0x01);               /* enable OPL3 (18 two-op voices)    */
    wr(d, 0x01, 0x20);                    /* enable waveform select (OPL2 cmp) */
}

void opl_dev_program(opl_dev *d, int voice, const uint8_t *adl)
{
    if (voice < 0 || voice >= d->voice_count) return;

    memcpy(d->patch[voice], adl, 16);

    int      ch   = voice % 9;
    uint16_t bank = voice_bank(voice);
    uint8_t  mod  = OP_OFF[ch];           /* modulator slot (op0)          */
    uint8_t  car  = (uint8_t)(mod + 3);   /* carrier slot (op1, audible)   */

    /* ADL byte order: bytes 0..5 = CARRIER, bytes 6..11 = MODULATOR.
     * (RE-REPORT 5.1's labels are reversed vs the OPL operator roles;
     * verified empirically — the original order silences real patches —
     * and corroborated by Appendix B.5, whose volume reg KSL comes from
     * adl[1], i.e. the carrier's 0x40 byte.) */
    wr(d, bank + 0x20 + car, adl[0]);
    wr(d, bank + 0x40 + car, adl[1]);
    wr(d, bank + 0x60 + car, adl[2]);
    wr(d, bank + 0x80 + car, adl[3]);
    wr(d, bank + 0xE0 + car, adl[4]);
    wr(d, bank + 0x20 + mod, adl[6]);
    wr(d, bank + 0x40 + mod, adl[7]);
    wr(d, bank + 0x60 + mod, adl[8]);
    wr(d, bank + 0x80 + mod, adl[9]);
    wr(d, bank + 0xE0 + mod, adl[10]);
    /* feedback/algorithm + force L/R enable (0x30) so OPL3 output is audible */
    wr(d, bank + 0xC0 + ch, (uint8_t)(adl[12] | 0x30));
}

void opl_dev_note_on(opl_dev *d, int voice, int note)
{
    if (voice < 0 || voice >= d->voice_count) return;
    if (note < 0) note = 0;

    uint16_t fnum  = FNUM[note % 12];
    uint8_t  block = (uint8_t)(note / 12);
    if (block > 7) block = 7;

    int      ch   = voice % 9;
    uint16_t bank = voice_bank(voice);

    uint8_t a0 = (uint8_t)(fnum & 0xFF);
    uint8_t b0 = (uint8_t)(((fnum >> 8) & 0x03) | (block << 2) | 0x20 /*KEYON*/);

    d->shadow_a0[voice] = a0;
    d->shadow_b0[voice] = b0;
    wr(d, bank + 0xA0 + ch, a0);
    wr(d, bank + 0xB0 + ch, b0);
}

void opl_dev_note_off(opl_dev *d, int voice)
{
    if (voice < 0 || voice >= d->voice_count) return;

    int      ch   = voice % 9;
    uint16_t bank = voice_bank(voice);
    uint8_t  b0   = (uint8_t)(d->shadow_b0[voice] & ~0x20); /* clear KEYON */

    d->shadow_b0[voice] = b0;
    wr(d, bank + 0xB0 + ch, b0);
}

void opl_dev_set_volume(opl_dev *d, int voice, int vol)
{
    if (voice < 0 || voice >= d->voice_count) return;
    if (vol < 0)  vol = 0;
    if (vol > 63) vol = 63;
    d->vol[voice] = (uint8_t)vol;

    int      ch   = voice % 9;
    uint16_t bank = voice_bank(voice);
    uint8_t  car  = (uint8_t)(OP_OFF[ch] + 3);   /* carrier carries loudness */

    /* TL is attenuation: 0 = loudest. Keep the patch carrier's KSL bits,
     * which live in adl[1] (the carrier 0x40 byte) per Appendix B.5. */
    uint8_t ksl = d->patch[voice][1] & 0xC0;
    uint8_t tl  = (uint8_t)((0x3F - vol) & 0x3F);
    wr(d, bank + 0x40 + car, (uint8_t)(ksl | tl));
}
