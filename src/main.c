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
#include "osl1.h"
#include "replay.h"

/* A real instrument: ADLIB/INST0000.ADL (RE-REPORT section 5.2). */
static uint8_t g_patch[16] = {
    0x64, 0x4f, 0xf2, 0x0b, 0x00, 0x71,
    0x3f, 0x52, 0x0b, 0x00, 0x0e, 0x00,
    0x00, 0x00, 0x00, 0x00
};

/* Native Nuked-OPL3 sample rate; the DOS engine ticks at 50 Hz. */
#define WAV_RATE  49716u
#define TICK_HZ   50u

/* Write a 16-bit stereo little-endian WAV. Returns 0 on success. */
static int write_wav(const char *path, const int16_t *pcm,
                     size_t frames, uint32_t rate)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    uint32_t data_bytes = (uint32_t)(frames * 2u * sizeof(int16_t));
    uint32_t byte_rate  = rate * 2u * (uint32_t)sizeof(int16_t);
    uint16_t block_align = (uint16_t)(2u * sizeof(int16_t));

    /* Helpers writing little-endian fields. */
    uint8_t hdr[44];
    memcpy(hdr + 0,  "RIFF", 4);
    uint32_t riff = 36u + data_bytes;
    hdr[4]=(uint8_t)riff; hdr[5]=(uint8_t)(riff>>8); hdr[6]=(uint8_t)(riff>>16); hdr[7]=(uint8_t)(riff>>24);
    memcpy(hdr + 8,  "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    hdr[16]=16; hdr[17]=0; hdr[18]=0; hdr[19]=0;     /* fmt chunk size = 16 */
    hdr[20]=1;  hdr[21]=0;                            /* PCM                 */
    hdr[22]=2;  hdr[23]=0;                            /* channels = 2        */
    hdr[24]=(uint8_t)rate; hdr[25]=(uint8_t)(rate>>8); hdr[26]=(uint8_t)(rate>>16); hdr[27]=(uint8_t)(rate>>24);
    hdr[28]=(uint8_t)byte_rate; hdr[29]=(uint8_t)(byte_rate>>8); hdr[30]=(uint8_t)(byte_rate>>16); hdr[31]=(uint8_t)(byte_rate>>24);
    hdr[32]=(uint8_t)block_align; hdr[33]=(uint8_t)(block_align>>8);
    hdr[34]=16; hdr[35]=0;                            /* bits per sample     */
    memcpy(hdr + 36, "data", 4);
    hdr[40]=(uint8_t)data_bytes; hdr[41]=(uint8_t)(data_bytes>>8); hdr[42]=(uint8_t)(data_bytes>>16); hdr[43]=(uint8_t)(data_bytes>>24);

    int ok = (fwrite(hdr, 1, 44, f) == 44) &&
             (fwrite(pcm, 1, data_bytes, f) == data_bytes);
    fclose(f);
    return ok ? 0 : -1;
}

/* Offline render: drive the replay engine and bake a WAV. No SDL/audio device
 * needed. Uses a known-good standalone ADL patch on every voice (the embedded
 * song-instrument upload path is deferred to a later phase). */
static int render_wav(const char *wavpath, const char *songpath)
{
    Song song;
    char err[256];
    if (osl1_load(songpath, &song, err, sizeof(err)) != 0) {
        fprintf(stderr, "load %s: %s\n", songpath, err);
        return 1;
    }
    printf("song: \"%s\" device=%s tracks=%u orders=%u\n",
           song.title, osl1_device_name(song.device),
           song.blk.track_count, song.blk.order_count);

    opl3_chip chip;
    OPL3_Reset(&chip, WAV_RATE);

    opl_dev dev;
    opl_dev_init(&dev, &chip, 1 /*opl3*/);

    Replay r;
    replay_init(&r, &song, 6);
    for (int v = 0; v < r.voice_count; v++)
        opl_dev_program(&dev, v, g_patch);   /* known-good patch on each voice */

    const uint32_t frames_per_tick = WAV_RATE / TICK_HZ;     /* ~994 */
    const size_t   max_frames = (size_t)WAV_RATE * 120;      /* 120 s safety cap */

    size_t cap = frames_per_tick * 64;
    int16_t *pcm = malloc(cap * 2 * sizeof(int16_t));
    if (!pcm) { osl1_free(&song); return 1; }
    size_t frames = 0;

    while (!r.finished && frames < max_frames) {
        replay_tick(&r, &dev);

        if ((frames + frames_per_tick) * 2 > cap * 2) {     /* grow buffer */
            size_t ncap = cap * 2;
            int16_t *np = realloc(pcm, ncap * 2 * sizeof(int16_t));
            if (!np) break;
            pcm = np; cap = ncap;
        }
        OPL3_GenerateStream(&chip, pcm + frames * 2, frames_per_tick);
        frames += frames_per_tick;
    }

    printf("rendered %zu frames (%.2f s), finished=%d\n",
           frames, (double)frames / WAV_RATE, r.finished);

    int rc = write_wav(wavpath, pcm, frames, WAV_RATE);
    if (rc == 0) printf("wrote %s\n", wavpath);
    else fprintf(stderr, "failed to write %s\n", wavpath);

    free(pcm);
    osl1_free(&song);
    return rc == 0 ? 0 : 1;
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
    /* Offline render mode: medplay --wav <out.wav> <songfile> */
    if (argc >= 4 && strcmp(argv[1], "--wav") == 0)
        return render_wav(argv[2], argv[3]);

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
