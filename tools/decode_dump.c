/* decode_dump - headless validation of the Phase 3 replay decoder.
 *
 * Loads an OSL1 song, drives the replay engine row-by-row (dev = NULL, so no
 * audio is produced), and prints the decoded cell for every voice on every
 * row. This lets us eyeball that decode_row consumes the position stream
 * sanely (note values in range, effects plausible, the row count matching the
 * position header) and that the order/loop logic terminates.
 *
 * Build via the Makefile target `decode_dump.exe`.
 *
 * Usage: decode_dump <songfile> [max_rows]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/osl1.h"
#include "../src/replay.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <songfile> [max_rows]\n", argv[0]);
        return 2;
    }
    int max_rows = argc > 2 ? atoi(argv[2]) : 64;

    Song song;
    char err[256];
    if (osl1_load(argv[1], &song, err, sizeof(err)) != 0) {
        fprintf(stderr, "load failed: %s\n", err);
        return 1;
    }

    printf("device=%s title=\"%s\" tracks=%u rows=%u orders=%u\n",
           osl1_device_name(song.device), song.title,
           song.blk.track_count, song.blk.row_count, song.blk.order_count);

    Replay r;
    replay_init(&r, &song, 6);
    printf("voices=%u order_len=%u\n\n", r.voice_count, r.order_len);

    int rows = 0;
    int last_order = -1;
    /* speed=6 means a row is decoded once every 6 ticks. */
    while (!r.finished && rows < max_rows) {
        if ((int)r.order_idx != last_order) {
            uint8_t pat = song.blk.order[r.order_idx];
            printf("--- order %u (pattern %u, pos_ptr=0x%lx) ---\n",
                   r.order_idx, pat, (unsigned long)song.blk.pos_ptr[pat]);
            last_order = (int)r.order_idx;
        }

        /* advance exactly one decoded row (6 ticks at the default speed) */
        for (int t = 0; t < r.speed; t++)
            replay_tick(&r, NULL);

        printf("row %3u:", r.pos);
        for (int v = 0; v < r.voice_count; v++) {
            const uint8_t *b = r.voice[v].b;
            uint8_t note = b[5];
            uint16_t period = (uint16_t)((b[1] | (b[2] << 8)) | (b[3] | (b[4] << 8)));
            if (note == 0 && period == 0 && b[6] == 0)
                printf("  ....    ");
            else
                printf("  n%02u e%02X%02X", note, b[6], b[7]);
        }
        printf("\n");
        rows++;
    }

    printf("\nstopped after %d rows (finished=%d, orders_played=%d)\n",
           rows, r.finished, r.orders_played);
    osl1_free(&song);
    return 0;
}
