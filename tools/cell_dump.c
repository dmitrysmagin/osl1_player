/* cell_dump - print full 8-byte decoded cell per voice per row (debug aid). */
#include <stdio.h>
#include <stdlib.h>
#include "../src/osl1.h"
#include "../src/replay.h"

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <song> [rows]\n", argv[0]); return 2; }
    int max_rows = argc > 2 ? atoi(argv[2]) : 8;

    Song song; char err[256];
    if (osl1_load(argv[1], &song, err, sizeof(err)) != 0) {
        fprintf(stderr, "load: %s\n", err); return 1;
    }
    Replay r; replay_init(&r, &song, 6);
    printf("voices=%u flags=%04X\n", r.voice_count, r.flags);

    int rows = 0;
    while (!r.finished && rows < max_rows) {
        for (int t = 0; t < r.speed; t++) replay_tick(&r, NULL);
        printf("row %2u:", r.pos);
        for (int v = 0; v < r.voice_count; v++) {
            const uint8_t *b = r.voice[v].b;
            printf("  [%02X %02X %02X %02X %02X %02X %02X %02X]",
                   b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8]);
        }
        printf("\n");
        rows++;
    }
    osl1_free(&song);
    return 0;
}
