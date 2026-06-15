/* medplay - Phase 2 scale test
 *
 * Validates the full audio chain through the clean-room ADLIB.DEV backend:
 * ADL-patch -> opl_dev (Appendix B) -> Nuked-OPL3 -> SDL2 output. Plays a
 * two-octave chromatic scale then a C-major arpeggio using a real 16-byte ADL
 * instrument, exercising the F-number table across several OPL blocks and the
 * volume (carrier-attenuation) path.
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
#include "opl_dev.h"

/* A real instrument: ADLIB/INST0000.ADL (RE-REPORT section 5.2). */
static uint8_t g_patch[16] = {
    0x64, 0x4f, 0xf2, 0x0b, 0x00, 0x71,
    0x3f, 0x52, 0x0b, 0x00, 0x0e, 0x00,
    0x00, 0x00, 0x00, 0x00
};

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

    /* Bring up the Adlib backend in OPL3 mode and load the patch on voice 0. */
    opl_dev dev_state;
    opl_dev_init(&dev_state, &chip, 1 /*opl3*/);
    opl_dev_program(&dev_state, 0, g_patch);

    const int note_frames = have.freq * 25 / 100;  /* 0.25 s sustain */
    const int gap_frames  = have.freq *  6 / 100;  /* 0.06 s release */

    SDL_PauseAudioDevice(dev, 0);

    /* Part 1: two-octave chromatic scale (notes 36..60) crosses OPL blocks. */
    for (int note = 36; note <= 60; note++) {
        opl_dev_note_on(&dev_state, 0, note);
        if (render_queue(dev, &chip, note_frames) != 0) {
            fprintf(stderr, "queue: %s\n", SDL_GetError()); break;
        }
        opl_dev_note_off(&dev_state, 0);
        render_queue(dev, &chip, gap_frames);
    }

    /* Part 2: C-major arpeggio with a descending volume ramp (effect 0x0C). */
    const int arp[] = { 48, 52, 55, 60, 64, 60, 55, 52 };
    const int n = (int)(sizeof(arp) / sizeof(arp[0]));
    for (int i = 0; i < n; i++) {
        opl_dev_set_volume(&dev_state, 0, 63 - i * 7);   /* 63,56,...,14 */
        opl_dev_note_on(&dev_state, 0, arp[i]);
        if (render_queue(dev, &chip, note_frames) != 0) {
            fprintf(stderr, "queue: %s\n", SDL_GetError()); break;
        }
        opl_dev_note_off(&dev_state, 0);
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
