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
    if (d->trace) fprintf(d->trace, "%03X=%02X\n", reg, val);
    /* Buffered write: Nuked queues the register change with a small sample
     * delay and applies it inside OPL3_GenerateStream, so a burst of writes at
     * a tick boundary (patch upload + key-off/on) is spread across samples the
     * way a real OPL sees them over the ISA bus, instead of landing on one
     * instant. Both the WAV render and the SDL callback pump GenerateStream. */
    OPL3_WriteRegBuffered(d->chip, reg, val);
}

void opl_dev_set_trace(opl_dev *d, FILE *f)
{
    d->trace = f;
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

    /* 16-byte ADL patch layout, from ADLIB.DEV's operator programmer @0xD69
     * (verified byte-for-byte against the title.dro capture):
     *   b0..b4  = MODULATOR 0x20,0x40,0x60,0x80,0xE0
     *   b5..b9  = CARRIER   0x20,0x40,0x60,0x80,0xE0
     *   b10     = 0xC0 feedback/connection
     * The carrier 0x40 (b6) contributes only its KSL bits; the TL (loudness)
     * is driven from the volume path, so it is uploaded silent (|0x3F) here
     * exactly as the driver does, then opl_dev_set_volume writes the real TL. */
    wr(d, bank + 0x20 + mod, adl[0]);
    wr(d, bank + 0x40 + mod, adl[1]);
    wr(d, bank + 0x60 + mod, adl[2]);
    wr(d, bank + 0x80 + mod, adl[3]);
    wr(d, bank + 0xE0 + mod, adl[4]);
    wr(d, bank + 0x20 + car, adl[5]);
    wr(d, bank + 0x40 + car, (uint8_t)((adl[6] & 0xC0) | 0x3F));
    wr(d, bank + 0x60 + car, adl[7]);
    wr(d, bank + 0x80 + car, adl[8]);
    wr(d, bank + 0xE0 + car, adl[9]);
    /* feedback/algorithm + force L/R enable (0x30) so OPL3 output is audible */
    wr(d, bank + 0xC0 + ch, (uint8_t)(adl[10] | 0x30));
}

void opl_dev_note_on(opl_dev *d, int voice, int note)
{
    if (voice < 0 || voice >= d->voice_count) return;

    /* ADLIB.DEV's pitch table (@0x3B5) is indexed by (note-12): the lowest
     * playable note is 12, and block = (note-12)/12, semitone = (note-12)%12.
     * Verified against title.dro (e.g. note 65 -> fnum 0x1CB, block 4). */
    int n = note - 12;
    if (n < 0) n = 0;

    uint16_t fnum  = FNUM[n % 12];
    uint8_t  block = (uint8_t)(n / 12);
    if (block > 7) block = 7;

    int      ch   = voice % 9;
    uint16_t bank = voice_bank(voice);

    uint8_t a0 = (uint8_t)(fnum & 0xFF);
    uint8_t b0 = (uint8_t)(((fnum >> 8) & 0x03) | (block << 2) | 0x20 /*KEYON*/);

    d->shadow_a0[voice] = a0;
    d->shadow_b0[voice] = b0;

    /* Retrigger so the OPL envelope restarts (a fresh attack) on every keyed
     * note, exactly as MED.EXE does: write the new fnum low, then B0 with KEYON
     * *clear*, then B0 with KEYON *set*. Without the intermediate key-off the
     * amplitude envelope never re-attacks and successive notes on a voice slur
     * together. Verified against a DOSBox OPL capture: A5=57 B5=11 B5=31. */
    wr(d, bank + 0xA0 + ch, a0);
    wr(d, bank + 0xB0 + ch, (uint8_t)(b0 & ~0x20));
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
     * which live in adl[6] (the carrier 0x40 byte) per ADLIB.DEV @0xD69. */
    uint8_t ksl = d->patch[voice][6] & 0xC0;
    uint8_t tl  = (uint8_t)((0x3F - vol) & 0x3F);
    wr(d, bank + 0x40 + car, (uint8_t)(ksl | tl));
}
