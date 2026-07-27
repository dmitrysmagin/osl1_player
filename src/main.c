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
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#define SDL_MAIN_HANDLED            /* we supply main(); console subsystem */
#include <SDL.h>
#include "opl3.h"
#include "opl_dev.h"
#include "osl1.h"
#include "replay.h"

/* Set by the SIGINT (Ctrl-C) handler; polled by the playback/render loops so
 * the audio device is torn down cleanly instead of the process being killed. */
static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { (void)sig; g_stop = 1; }

/* Playback options parsed from the command line (Phase 6). */
typedef struct {
    const char *wav;     /* WAV output path/dir, or NULL for live playback   */
    const char *trace;   /* OPL register-trace output path, or NULL          */
    int         rate;    /* output sample rate (Hz)                          */
    int         opl3;    /* -1 = auto (track_count>9), 0 = OPL2, 1 = OPL3     */
    int         speed;   /* initial ticks/row (effect 0x09 may change it)    */
    int         status;  /* 1 = print a progress/status line                 */
} Options;

/* Default output sample rate. This is deliberately NOT Nuked's internal
 * 49716 Hz native rate: OPL3_Reset(chip, rate) sets an internal resample ratio
 * (rate<<RSM_FRAC)/49716 and OPL3_GenerateStream then emits at the requested
 * rate, so we ask for a standard device rate (48 kHz) and let Nuked resample.
 * The DOS engine ticks at 50 Hz regardless. */
#define DEFAULT_RATE  48000u
#define TICK_HZ       50u

/* Fractional samples-per-tick (rate/50; e.g. 48000/50 = 960): the render/live
 * loops keep a double accumulator so the long-run rate stays exact (no drift).
 * Computed from the active rate at run time, not from this default. */
#define SAMPLES_PER_TICK ((double)DEFAULT_RATE / (double)TICK_HZ)

/* OPL2 (9 voices) is enough for <=9 tracks; switch to OPL3 (18) above that. */
static int opl3_needed(const Song *song)
{
    return song->blk.track_count > 9;
}

/* Seed every replay voice with a real song instrument so an OPL channel that
 * is keyed before its first per-note program upload still has a valid timbre.
 * The audible path is opl_dev_program driven per note by replay.c (instrument
 * selected from the cell's b[5] byte, verified against ADLIB.DEV @0xD69 and the
 * title.dro capture); this is only the power-on default. We use the first valid
 * embedded instrument rather than a hardcoded patch. */
static void program_voices(opl_dev *dev, const Replay *r)
{
    const Song *song = r->song;
    const uint8_t *seed = NULL;
    for (uint16_t i = 0; i < song->instr_total; i++) {
        if (song->instr[i].valid) { seed = song->instr[i].adl; break; }
    }
    if (!seed) return;
    for (int v = 0; v < r->voice_count; v++)
        opl_dev_program(dev, v, seed);
}

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
static int render_wav(const char *songpath, const char *wavpath,
                      const Options *opt)
{
    Song song;
    char err[256];
    if (osl1_load(songpath, &song, err, sizeof(err)) != 0) {
        fprintf(stderr, "load %s: %s\n", songpath, err);
        return 1;
    }
    int use_opl3 = opt->opl3 < 0 ? opl3_needed(&song) : opt->opl3;
    uint32_t rate = (uint32_t)opt->rate;
    printf("song: \"%s\" device=%s tracks=%u orders=%u (OPL%d, %u Hz)\n",
           song.title, osl1_device_name(song.device),
           song.blk.track_count, song.blk.order_count,
           use_opl3 ? 3 : 2, rate);

    opl3_chip chip;
    OPL3_Reset(&chip, rate);

    opl_dev dev;
    opl_dev_init(&dev, &chip, use_opl3);

    FILE *tracef = NULL;
    if (opt->trace) {
        tracef = fopen(opt->trace, "w");
        if (!tracef) fprintf(stderr, "warning: cannot open trace %s\n", opt->trace);
        else opl_dev_set_trace(&dev, tracef);
    }

    Replay r;
    replay_init(&r, &song, opt->speed);
    program_voices(&dev, &r);             /* known-good patch on each voice */

    const size_t max_frames = (size_t)rate * 120;            /* 120 s safety cap */

    /* Fractional samples-per-tick accumulator: carry the sub-frame remainder
     * across ticks so the long-run sample rate stays exact (no drift). */
    double accum = 0.0;

    size_t cap = (size_t)((double)rate / TICK_HZ * 64);
    int16_t *pcm = malloc(cap * 2 * sizeof(int16_t));
    if (!pcm) { osl1_free(&song); if (tracef) fclose(tracef); return 1; }
    size_t frames = 0;
    int last_order = -1;

    while (!r.finished && frames < max_frames && !g_stop) {
        replay_tick(&r, &dev);

        if (opt->status && (int)r.order_idx != last_order) {
            last_order = (int)r.order_idx;
            printf("  order %u/%u\n", r.order_idx, r.order_len);
        }

        /* Tempo (effect 0x0F) sets the tick rate; recompute per tick. */
        accum += (double)rate / (r.tempo ? r.tempo : TICK_HZ);
        uint32_t n = (uint32_t)accum;       /* whole frames to emit this tick */
        accum -= n;

        if ((frames + n) * 2 > cap * 2) {                   /* grow buffer */
            size_t ncap = cap * 2;
            while ((frames + n) * 2 > ncap * 2) ncap *= 2;
            int16_t *np = realloc(pcm, ncap * 2 * sizeof(int16_t));
            if (!np) break;
            pcm = np; cap = ncap;
        }
        OPL3_GenerateStream(&chip, pcm + frames * 2, n);
        frames += n;
    }

    printf("rendered %zu frames (%.2f s), finished=%d\n",
           frames, (double)frames / rate, r.finished);

    int rc = write_wav(wavpath, pcm, frames, rate);
    if (rc == 0) printf("wrote %s\n", wavpath);
    else fprintf(stderr, "failed to write %s\n", wavpath);

    free(pcm);
    if (tracef) fclose(tracef);
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

/* Shared state between the main thread and the SDL audio callback. The replay
 * engine, OPL backend and chip are all driven exclusively from the callback
 * (audio thread), so there are no data races on them; the main thread only
 * reads `ended` and, while holding the device lock, the replay counters for the
 * status line. */
typedef struct {
    opl3_chip chip;
    opl_dev   dev;
    Replay    r;
    double    tick_accum;   /* whole+fractional frames owed before next tick   */
    int       rate;         /* device sample rate                              */
    volatile int ended;     /* set by the callback when the song finishes      */
} LiveState;

/* SDL audio callback: the audio device is the master clock. For each frame we
 * owe, advance one replay tick whenever the accumulator runs dry (samples per
 * tick = rate / tempo, fractional so it never drifts), then render OPL frames
 * in chunks up to the next tick boundary. Modelled on musicv/wm-player's
 * sdl_audio.c, adapted to chunked OPL3_GenerateStream + tempo-aware pacing. */
static void audio_cb(void *userdata, Uint8 *stream, int len)
{
    LiveState *ls = (LiveState *)userdata;
    int16_t  *out = (int16_t *)stream;
    uint32_t frames = (uint32_t)len / 4;        /* 2 ch * int16 = 4 bytes/frame */
    uint32_t i = 0;

    while (i < frames) {
        if (ls->tick_accum < 1.0) {
            if (!ls->r.finished)
                replay_tick(&ls->r, &ls->dev);
            else
                ls->ended = 1;                  /* keep ringing the chip out    */
            ls->tick_accum += (double)ls->rate / (ls->r.tempo ? ls->r.tempo : TICK_HZ);
        }

        uint32_t avail = (uint32_t)ls->tick_accum;
        uint32_t chunk = frames - i;
        if (chunk > avail) chunk = avail;
        if (chunk == 0) { ls->tick_accum += 1.0; continue; }  /* paranoia       */

        OPL3_GenerateStream(&ls->chip, out + i * 2, chunk);
        i += chunk;
        ls->tick_accum -= chunk;
    }
}

/* Live playback: the SDL audio callback (audio_cb) drives the replay engine.
 * The main thread just installs a Ctrl-C handler and idles until the song ends
 * or the user interrupts, then tears the device down cleanly. */
static int play_live(const char *songpath, const Options *opt)
{
    Song song;
    char err[256];
    if (osl1_load(songpath, &song, err, sizeof(err)) != 0) {
        fprintf(stderr, "load %s: %s\n", songpath, err);
        return 1;
    }
    int use_opl3 = opt->opl3 < 0 ? opl3_needed(&song) : opt->opl3;
    printf("song: \"%s\" device=%s tracks=%u orders=%u\n",
           song.title, osl1_device_name(song.device),
           song.blk.track_count, song.blk.order_count);

    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        osl1_free(&song);
        return 1;
    }

    LiveState *ls = calloc(1, sizeof(*ls));      /* large; keep off the stack    */
    if (!ls) { SDL_Quit(); osl1_free(&song); return 1; }
    ls->ended = 0;
    ls->tick_accum = 0.0;

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq     = opt->rate;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 1024;
    want.callback = audio_cb;            /* callback mode (audio is the clock)   */
    want.userdata = ls;

    /* Pass NULL for the obtained spec (and no ALLOW_* flags) so SDL delivers the
     * exact format we asked for, converting internally if the hardware differs.
     * Opens paused, so it is safe to finish initialising `ls` before unpausing. */
    SDL_AudioDeviceID adev =
        SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (!adev) {
        fprintf(stderr, "SDL_OpenAudioDevice: %s\n", SDL_GetError());
        free(ls); SDL_Quit(); osl1_free(&song);
        return 1;
    }
    printf("audio: %d Hz, %d ch (OPL%d, %u voices)\n",
           want.freq, want.channels, use_opl3 ? 3 : 2,
           song.blk.track_count);

    ls->rate = want.freq;
    OPL3_Reset(&ls->chip, (uint32_t)want.freq);
    opl_dev_init(&ls->dev, &ls->chip, use_opl3);

    FILE *tracef = NULL;
    if (opt->trace) {
        tracef = fopen(opt->trace, "w");
        if (!tracef) fprintf(stderr, "warning: cannot open trace %s\n", opt->trace);
        else opl_dev_set_trace(&ls->dev, tracef);
    }

    replay_init(&ls->r, &song, opt->speed);
    program_voices(&ls->dev, &ls->r);

    printf("playing... press Ctrl-C to stop\n");

    SDL_PauseAudioDevice(adev, 0);       /* start the callback                   */

    Uint32 next_status = 0;
    while (!g_stop && !ls->ended) {
        if (opt->status && SDL_GetTicks() >= next_status) {
            next_status = SDL_GetTicks() + 200;
            SDL_LockAudioDevice(adev);   /* consistent snapshot of the counters  */
            unsigned oi = ls->r.order_idx, ol = ls->r.order_len;
            unsigned pos = ls->r.pos, rl = ls->r.row_limit;
            unsigned sp = ls->r.speed, tp = ls->r.tempo;
            SDL_UnlockAudioDevice(adev);
            printf("\r  order %2u/%-2u  row %3u/%-3u  speed %u  tempo %u   ",
                   oi, ol, pos, rl, sp, tp);
            fflush(stdout);
        }
        SDL_Delay(50);
    }
    if (opt->status) printf("\n");

    SDL_PauseAudioDevice(adev, 1);       /* stop the callback before teardown    */
    SDL_CloseAudioDevice(adev);
    SDL_Quit();
    if (tracef) fclose(tracef);
    osl1_free(&song);
    free(ls);
    printf(g_stop ? "stopped\n" : "done\n");
    return 0;
}

/* Legacy standalone scale/arpeggio demo. `patch_path` may be NULL. */
static int play_scale_demo(const char *patch_path);

static int path_is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Play (or render) every parseable song in a directory, in readdir order.
 * Files osl1 cannot parse (e.g. 16-byte instrument files) are skipped. When
 * --wav is given, `opt->wav` is treated as an output directory and each song
 * is rendered to <wavdir>/<name>.wav. */
static int render_wav(const char *songpath, const char *wavpath, const Options *opt);
static int play_live(const char *songpath, const Options *opt);

static int play_directory(const char *dir, const Options *opt)
{
    DIR *d = opendir(dir);
    if (!d) { fprintf(stderr, "cannot open dir %s\n", dir); return 1; }

    struct dirent *e;
    int count = 0, rc = 0;
    while ((e = readdir(d)) != NULL && !g_stop) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);

        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        /* Probe: only handle files the parser accepts. */
        Song probe; char err[256];
        if (osl1_load(path, &probe, err, sizeof err) != 0) continue;
        osl1_free(&probe);

        printf("=== [%d] %s ===\n", ++count, e->d_name);
        if (opt->wav) {
            char wav[1024];
            snprintf(wav, sizeof wav, "%s/%s.wav", opt->wav, e->d_name);
            rc |= render_wav(path, wav, opt);
        } else {
            rc |= play_live(path, opt);
        }
    }
    closedir(d);
    if (!count) { fprintf(stderr, "no playable songs in %s\n", dir); return 1; }
    return rc;
}

static void usage(void)
{
    fprintf(stderr,
        "medplay - OSL1/Adlib player (OPL2/OPL3 via Nuked-OPL3)\n\n"
        "usage:\n"
        "  medplay <song|dir> [options]      live playback\n"
        "  medplay --wav <out> <song>        render one song to a WAV file\n"
        "  medplay --wav <dir> <songdir>     render every song in <songdir>\n"
        "  medplay --scale [patch.adl]       standalone scale/arpeggio demo\n\n"
        "options:\n"
        "  --wav <path>     offline render (file for a song, dir for a dir)\n"
        "  --trace <file>   log every OPL register write (RRR=VV) for diffing\n"
        "  --rate <hz>      output sample rate (default 48000; Nuked resamples)\n"
        "  --opl2 / --opl3  force OPL2 (9 voices) or OPL3 (18); default: auto\n"
        "  --speed <n>      initial ticks/row (default: read from the song)\n"
        "  --status         print a progress/status line\n");
}

int main(int argc, char **argv)
{
    SDL_SetMainReady();                 /* we handle main(); see SDL_MAIN_HANDLED */
    g_stop = 0;
    signal(SIGINT, on_sigint);          /* Ctrl-C stops playback/render cleanly  */

    Options opt;
    opt.wav = NULL; opt.trace = NULL; opt.rate = DEFAULT_RATE;
    opt.opl3 = -1;  opt.speed = 0;     opt.status = 0;  /* speed 0 = from song */

    const char *input = NULL;
    int scale_mode = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if      (!strcmp(a, "--wav")   && i + 1 < argc) opt.wav   = argv[++i];
        else if (!strcmp(a, "--trace") && i + 1 < argc) opt.trace = argv[++i];
        else if (!strcmp(a, "--rate")  && i + 1 < argc) opt.rate  = atoi(argv[++i]);
        else if (!strcmp(a, "--speed") && i + 1 < argc) opt.speed = atoi(argv[++i]);
        else if (!strcmp(a, "--opl2"))                  opt.opl3  = 0;
        else if (!strcmp(a, "--opl3"))                  opt.opl3  = 1;
        else if (!strcmp(a, "--status"))                opt.status = 1;
        else if (!strcmp(a, "--scale"))                 scale_mode = 1;
        else if (a[0] == '-') { fprintf(stderr, "unknown option: %s\n", a); usage(); return 2; }
        else if (!input)                                input = a;
        else { fprintf(stderr, "unexpected argument: %s\n", a); return 2; }
    }

    if (opt.rate  <= 0) opt.rate  = DEFAULT_RATE;
    if (opt.speed <  0) opt.speed = 0;   /* 0 -> replay_init reads it from the song */

    if (scale_mode)
        return play_scale_demo(input);    /* input, if any, is the patch path */

    if (!input) { usage(); return 2; }

    if (path_is_dir(input))
        return play_directory(input, &opt);

    if (opt.wav)
        return render_wav(input, opt.wav, &opt);

    opt.status = 1;                       /* status line on by default for live */
    return play_live(input, &opt);
}

static int play_scale_demo(const char *patch_path)
{
    /* Standalone demo patch (ADLIB/INST0000.ADL, RE-REPORT 5.2), overridable
     * from disk. Local to the demo; the song player uses real song instruments. */
    uint8_t g_patch[16] = {
        0x64, 0x4f, 0xf2, 0x0b, 0x00, 0x71,
        0x3f, 0x52, 0x0b, 0x00, 0x0e, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    /* Optionally load a real ADL instrument from disk. */
    if (patch_path) {
        FILE *f = fopen(patch_path, "rb");
        if (!f) { fprintf(stderr, "cannot open %s\n", patch_path); return 1; }
        if (fread(g_patch, 1, 16, f) != 16)
            fprintf(stderr, "warning: %s is not 16 bytes\n", patch_path);
        fclose(f);
        printf("loaded patch from %s\n", patch_path);
    }

    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq     = DEFAULT_RATE;   /* Nuked resamples from its 49716 native */
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
