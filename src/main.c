/* medplay - Phase 0 tone test
 *
 * Validates the full audio chain: ADL-patch -> OPL register mapping ->
 * Nuked-OPL3 emulation -> SDL2 output. Plays a C-major arpeggio using a real
 * 16-byte ADL instrument (Appendix B.4) and the Adlib F-number table
 * (Appendix B.3) recovered from ADLIB.DEV.
 *
 * The helpers below (fnum table, ADL->OPL mapping) are deliberately inline for
 * Phase 0; they move into opl_dev.c in Phase 2.
 *
 * Build:  make            (see ../Makefile)
 * Run:    ./medplay.exe [optional path to a 16-byte .ADL instrument]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <SDL.h>
#include "opl3.h"

/* ---- Adlib note -> OPL frequency (ADLIB.DEV FNUM @0xF61, base 0x157) ------ */
static const uint16_t FNUM[12] = {
    0x157, 0x16C, 0x181, 0x198, 0x1B1, 0x1CB,
    0x1E6, 0x203, 0x222, 0x243, 0x266, 0x28A
};

/* Per-channel operator base offsets (standard OPL melodic mode, channels 0-8) */
static const uint8_t OP_OFF[9] = {
    0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12
};

/* A real instrument: ADLIB/INST0000.ADL (RE-REPORT section 5.2). */
static uint8_t g_patch[16] = {
    0x64, 0x4f, 0xf2, 0x0b, 0x00, 0x71,
    0x3f, 0x52, 0x0b, 0x00, 0x0e, 0x00,
    0x00, 0x00, 0x00, 0x00
};

/* Upload a 16-byte ADL patch to OPL melodic channel `ch` (Appendix B.4). */
static void opl_program(opl3_chip *chip, int ch, const uint8_t *adl)
{
    uint8_t m = OP_OFF[ch];        /* modulator operator offset */
    uint8_t c = (uint8_t)(m + 3);  /* carrier operator offset   */
    OPL3_WriteReg(chip, 0x20 + m, adl[0]);
    OPL3_WriteReg(chip, 0x40 + m, adl[1]);
    OPL3_WriteReg(chip, 0x60 + m, adl[2]);
    OPL3_WriteReg(chip, 0x80 + m, adl[3]);
    OPL3_WriteReg(chip, 0xE0 + m, adl[4]);
    OPL3_WriteReg(chip, 0x20 + c, adl[6]);
    OPL3_WriteReg(chip, 0x40 + c, adl[7]);
    OPL3_WriteReg(chip, 0x60 + c, adl[8]);
    OPL3_WriteReg(chip, 0x80 + c, adl[9]);
    OPL3_WriteReg(chip, 0xE0 + c, adl[10]);
    /* feedback/algorithm + force L/R enable (0x30) for audible OPL3 output */
    OPL3_WriteReg(chip, 0xC0 + ch, (uint8_t)(adl[12] | 0x30));
}

/* Key a note on (note = octave*12 + semitone). */
static void opl_note_on(opl3_chip *chip, int ch, int note)
{
    uint16_t fnum = FNUM[note % 12];
    uint8_t  block = (uint8_t)(note / 12);
    OPL3_WriteReg(chip, 0xA0 + ch, (uint8_t)(fnum & 0xFF));
    OPL3_WriteReg(chip, 0xB0 + ch,
                  (uint8_t)(((fnum >> 8) & 0x03) | (block << 2) | 0x20 /*KEYON*/));
}

static void opl_note_off(opl3_chip *chip, int ch, int note)
{
    uint16_t fnum = FNUM[note % 12];
    uint8_t  block = (uint8_t)(note / 12);
    OPL3_WriteReg(chip, 0xB0 + ch,
                  (uint8_t)(((fnum >> 8) & 0x03) | (block << 2))); /* KEYON cleared */
}

/* Render `frames` stereo frames and queue them on the audio device. */
static int render_queue(SDL_AudioDeviceID dev, opl3_chip *chip, int frames)
{
    int16_t *buf = malloc((size_t)frames * 2 * sizeof(int16_t));
    if (!buf) return -1;
    OPL3_GenerateStream(chip, buf, (uint32_t)frames);
    int rc = SDL_QueueAudio(dev, buf, (Uint32)(frames * 2 * sizeof(int16_t)));
    free(buf);
    return rc;
}

int main(int argc, char **argv)
{
    /* Optionally load a real ADL instrument from disk. */
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
        if (fread(g_patch, 1, 16, f) != 16)
            fprintf(stderr, "warning: %s is not 16 bytes\n", argv[1]);
        fclose(f);
        printf("loaded patch from %s\n", argv[1]);
    }

    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq     = 49716;          /* Nuked native rate */
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 1024;
    want.callback = NULL;           /* queue mode (no audio thread / no races) */

    SDL_AudioDeviceID dev =
        SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!dev) {
        fprintf(stderr, "SDL_OpenAudioDevice: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    printf("audio: %d Hz, %d ch, format 0x%04x\n", have.freq, have.channels, have.format);

    opl3_chip chip;
    OPL3_Reset(&chip, (uint32_t)have.freq);
    OPL3_WriteReg(&chip, 0x105, 0x01);   /* enable OPL3 mode (18 channels)   */
    OPL3_WriteReg(&chip, 0x01, 0x20);    /* enable waveform select (OPL2 cmp) */
    opl_program(&chip, 0, g_patch);

    const int note_frames = have.freq * 40 / 100;  /* 0.40 s sustain */
    const int gap_frames  = have.freq * 10 / 100;  /* 0.10 s release */
    const int notes[] = { 48, 52, 55, 60, 64, 60, 55, 52 }; /* C maj arpeggio */
    const int n = (int)(sizeof(notes) / sizeof(notes[0]));

    SDL_PauseAudioDevice(dev, 0);
    for (int i = 0; i < n; i++) {
        opl_note_on(&chip, 0, notes[i]);
        if (render_queue(dev, &chip, note_frames) != 0) {
            fprintf(stderr, "queue: %s\n", SDL_GetError()); break;
        }
        opl_note_off(&chip, 0, notes[i]);
        render_queue(dev, &chip, gap_frames);
    }

    /* Wait for the queue to drain, then clean up. */
    while (SDL_GetQueuedAudioSize(dev) > 0)
        SDL_Delay(20);
    SDL_Delay(100);

    SDL_CloseAudioDevice(dev);
    SDL_Quit();
    printf("done\n");
    return 0;
}
