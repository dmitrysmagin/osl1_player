/* fxhist - effect-command histogram, driven through medplay's own loaders and
 * replay engine so the decode is exactly the one the player uses.
 *
 * replay.c is #included rather than linked so this tool can reimplement the
 * tick loop with a sampling point between decode_row() and trigger_row().
 * That matters: dispatch_row_effect() consumes 0x06, 0x0C and 0x0E by zeroing
 * b[6], so sampling after replay_tick() would under-count exactly the three
 * commands the audit cares most about.
 *
 * Prints one line per (command, param) occurrence:  GEN cmd param
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/replay.c"

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <song>...\n", argv[0]); return 2; }

    for (int a = 1; a < argc; a++) {
        Song song; char err[256];
        memset(&song, 0, sizeof(song));
        if (osl1_load(argv[a], &song, err, sizeof(err)) != 0) continue;

        const char *gen = "OSL1";
        if      (song.old_magic == 0xB4) gen = "B4";
        else if (song.old_magic == 0xB6) gen = "B6";
        else if (song.old_magic == 0x20) gen = "ALB";

        Replay r; replay_init(&r, &song, 0);
        long guard = 0;
        while (!r.finished && guard++ < 200000) {
            if (++r.tick < r.speed) { tick_effects(&r, NULL); continue; }
            r.tick = 0;
            decode_row(&r);
            for (int v = 0; v < r.voice_count; v++) {
                const uint8_t *b = r.voice[v].b;
                if (b[6] || b[7])
                    printf("%s %02X %02X %s\n", gen, b[6], b[7], argv[a]);
            }
            trigger_row(&r, NULL);
            advance_position(&r);
        }
        fflush(stdout);
        osl1_free(&song);
    }
    return 0;
}
