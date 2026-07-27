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

/* ---- micro-pitch bend engine (ADLIB.DEV 0xe46/0xe80/0xfa9) --------------- *
 * The period at slot+5 is a 14-bit pitch value centred on 0x2000. The bend
 * routine converts a period offset from centre into an F-number/block delta by
 * a piecewise-linear interpolation over the F-number table @0xf61, using the
 * per-note index map @0xf11. Ported verbatim so a portamento sweep reproduces
 * the DOS driver's exact stair-stepped fnum path (diffable against a DRO).
 *
 * f11 @0xf11 stores BYTE offsets into f61 (element index = value/2).
 * f61 is preceded in the driver's data segment by the 24-word table @0xf31;
 * the down-bend reads negative indices into it (e.g. f61[-1] @0xf5f = 0x0145),
 * so f61c[] carries both, with F61_BASE marking f61[0]. */
#define F61_BASE 24
static const uint16_t f61c[24 + 36] = {
    /* @0xf31: 24 words the down-bend may reach via negative indices */
    0x0055,0x005b,0x0060,0x0066,0x006c,0x0072,0x0079,0x0080,
    0x0088,0x0090,0x0099,0x00a2,0x00ab,0x00b6,0x00c0,0x00cc,
    0x00d8,0x00e5,0x00f3,0x0101,0x0111,0x0121,0x0133,0x0145,
    /* @0xf61: F-numbers across three octaves */
    0x0157,0x016c,0x0181,0x0198,0x01b1,0x01cb,0x01e6,0x0203,
    0x0222,0x0243,0x0266,0x028a,0x02ae,0x02d8,0x0302,0x0330,
    0x0362,0x0396,0x03cc,0x0406,0x0444,0x0486,0x04cc,0x0514,
    0x055c,0x05b0,0x0604,0x0660,0x06c4,0x072c,0x0798,0x080c,
    0x0888,0x090c,0x0998,0x0a28
};
static const uint16_t f11[16] = {
    0x0000,0x0002,0x0004,0x0006,0x0008,0x000a,0x0000,0x000c,
    0x000e,0x0000,0x0010,0x0012,0x0000,0x0014,0x0000,0x0016
};

/* f61 indexed by a signed element offset relative to f61[0]. */
static uint16_t f61e(int i) { return f61c[F61_BASE + i]; }

/* Fold `period` (centre 0x2000) into the base note's fnum/block, returning the
 * bent F-number (*out_fnum) and block (*out_block). Mirrors 0xe46 exactly. */
static void bend_pitch(uint16_t base_fnum, uint8_t base_block, uint16_t period,
                       uint16_t *out_fnum, uint8_t *out_block)
{
    int ax = (int16_t)period;              /* 0xe49 'jns' tests the sign  */
    if (ax < 0) ax = 0;                    /* 0xe4d                        */
    if (ax >= 0x3fff) ax = 0x3fff;         /* 0xe4f/0xe54                  */
    int off = ax - 0x2000;                 /* 0xe57                        */

    uint16_t dx = base_fnum;
    int      cl = base_block;

    if (off != 0) {
        /* base element index within f61 for this note's octave-0 fnum. */
        int base_off = f11[((base_fnum - 0x157) / 20) & 0xf];  /* byte off */
        int idx_b    = base_off / 2;                           /* element  */
        unsigned mag = (unsigned)(off > 0 ? off : -off);
        unsigned q   = (mag * 4) / 0x55;                       /* 0xe81/0xfcd */
        int coff     = (int)((q >> 3) & 0xfe) / 2;             /* elements */
        int fineq    = (int)(q & 0x0f);

        if (off > 0) {                     /* up-bend 0xe80                */
            int coarse = (int)f61e(idx_b + coff) - (int)f61e(idx_b);
            int slope  = (int)f61e(idx_b + 1)    - (int)f61e(idx_b);
            int fine   = (slope * fineq) >> 4;
            int v      = (int)base_fnum + coarse + fine;
            if      (v >= 0xab8) { v = ((v - 0xab8) >> 3) + 0x157; cl += 3; }
            else if (v >= 0x55c) { v = ((v - 0x55c) >> 2) + 0x157; cl += 2; }
            else if (v >= 0x2ae) { v = ((v - 0x2ae) >> 1) + 0x157; cl += 1; }
            dx = (uint16_t)v;
        } else {                           /* down-bend 0xfa9              */
            int coarse = (int)f61e(idx_b) - (int)f61e(idx_b - coff);
            int slope  = (int)f61e(idx_b) - (int)f61e(idx_b - 1); /* 0xf5f */
            int fine   = (slope * fineq) >> 4;
            int v      = (int)base_fnum - (coarse + fine);
            if (v <= 0x157) {
                if      (v > 0xab) { v = ((v - 0xab) << 1) + 0x157; cl -= 1; }
                else if (v > 0x55) { v = ((v - 0x55) << 2) + 0x157; cl -= 2; }
                else               { v = ((v - 0x2a) << 3) + 0x157; cl -= 3; }
            }
            dx = (uint16_t)v;
        }
    }

    if (cl < 0) cl = 0;
    *out_fnum  = dx;
    *out_block = (uint8_t)cl;
}

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

    for (int v = 0; v < OPL_DEV_MAX_VOICES; v++) {
        d->vol[v]   = 0x3F;               /* full volume (0x682 init analogue) */
        d->used[v]  = 0;                  /* slot free (0xFF marker @0x53C)    */
        d->owner[v] = -1;
    }

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

/* Emit A0/B0 for `voice` from its base fnum/block bent by its current period
 * (pitch writer 0xe0c). With `retrigger` set we restart the OPL envelope (a
 * fresh attack) by pulsing KEYON off->on; otherwise we preserve the current
 * KEYON bit, exactly as the 0xe0c path does when only re-pitching. */
static void emit_pitch(opl_dev *d, int voice, int retrigger)
{
    uint16_t fnum;
    uint8_t  block;
    bend_pitch(d->base_fnum[voice], d->base_block[voice], d->period[voice],
               &fnum, &block);

    int      ch   = voice % 9;
    uint16_t bank = voice_bank(voice);
    uint8_t  a0   = (uint8_t)(fnum & 0xFF);
    uint8_t  keyon = (uint8_t)(d->shadow_b0[voice] & 0x20);
    uint8_t  b0   = (uint8_t)(((fnum >> 8) & 0x03) | (block << 2));

    d->shadow_a0[voice] = a0;

    if (retrigger) {
        /* MED restarts the envelope on every keyed note: write fnum low, then
         * B0 with KEYON clear, then B0 with KEYON set. Without the intermediate
         * key-off the envelope never re-attacks and notes slur together.
         * Verified against a DOSBox OPL capture: A5=57 B5=11 B5=31. */
        d->shadow_b0[voice] = (uint8_t)(b0 | 0x20);
        wr(d, bank + 0xA0 + ch, a0);
        wr(d, bank + 0xB0 + ch, b0);                        /* KEYON clear */
        wr(d, bank + 0xB0 + ch, (uint8_t)(b0 | 0x20));      /* KEYON set   */
    } else {
        /* 0xe0c: keep the live KEYON bit, just re-pitch (portamento). */
        d->shadow_b0[voice] = (uint8_t)(b0 | keyon);
        wr(d, bank + 0xA0 + ch, a0);
        wr(d, bank + 0xB0 + ch, (uint8_t)(b0 | keyon));
    }
}

void opl_dev_note_on(opl_dev *d, int voice, int note)
{
    if (voice < 0 || voice >= d->voice_count) return;

    /* ADLIB.DEV's pitch table (@0x3B5) is indexed by (note-12): the lowest
     * playable note is 12, and block = (note-12)/12, semitone = (note-12)%12.
     * Verified against title.dro (e.g. note 65 -> fnum 0x1CB, block 4). */
    int n = note - 12;
    if (n < 0) n = 0;

    d->base_fnum[voice]  = FNUM[n % 12];
    d->base_block[voice] = (uint8_t)(n / 12 > 7 ? 7 : n / 12);
    d->period[voice]     = 0x2000;   /* primary note-on resets bend (@0x475) */

    emit_pitch(d, voice, 1);
}

void opl_dev_set_period(opl_dev *d, int chan, int period)
{
    /* Store the raw 16-bit period (the DOS engine only ever clamps inside the
     * bend @0xe46), so accumulating portamento wraps exactly as it does there. */
    for (int v = 0; v < d->voice_count; v++)
        if (d->used[v] && d->owner[v] == chan) {
            d->period[v] = (uint16_t)period;
            emit_pitch(d, v, 0);
        }
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

/* ---- dynamic allocation layer (ADLIB.DEV slot table @0x53C) ------------- */

/* Linear first-free scan of the physical voices (allocator @0x50C). Returns
 * the voice index, or -1 if every voice is busy — ADLIB.DEV sets carry and the
 * caller drops the note (no voice-stealing). */
static int alloc_voice(opl_dev *d)
{
    for (int v = 0; v < d->voice_count; v++)
        if (!d->used[v]) return v;
    return -1;
}

void opl_dev_keyoff(opl_dev *d, int chan)
{
    /* Match `chan` against every slot marker and free all that belong to it
     * (@0x58F: a logical channel may own several physical voices — chords). */
    for (int v = 0; v < d->voice_count; v++) {
        if (d->used[v] && d->owner[v] == chan) {
            opl_dev_note_off(d, v);
            d->used[v]  = 0;
            d->owner[v] = -1;
        }
    }
}

int opl_dev_keyon(opl_dev *d, int chan, int note, const uint8_t *adl, int vol)
{
    int v = alloc_voice(d);
    if (v < 0) return -1;                 /* all voices busy -> note dropped */

    d->used[v]  = 1;
    d->owner[v] = (int16_t)chan;          /* slot+0 = `bl` marker (key-on)   */

    if (adl) opl_dev_program(d, v, adl);  /* NULL keeps the current patch    */
    opl_dev_set_volume(d, v, vol);
    opl_dev_note_on(d, v, note);
    return v;
}

void opl_dev_chanvol(opl_dev *d, int chan, int vol)
{
    for (int v = 0; v < d->voice_count; v++)
        if (d->used[v] && d->owner[v] == chan)
            opl_dev_set_volume(d, v, vol);
}
