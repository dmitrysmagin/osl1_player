/* opl_scale - headless verification of the opl_dev backend (Phase 2).
 *
 * Programs a real ADL patch, then for each note of a two-octave chromatic
 * scale keys it on, renders a short buffer through Nuked-OPL3, and reports the
 * peak amplitude. Non-zero peaks across all blocks confirm the patch upload and
 * F-number/block mapping actually drive the emulator (no audio device needed).
 *
 * Build via the Makefile target `opl_scale.exe`.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../src/opl3.h"
#include "../src/opl_dev.h"

/* INST0000.ADL (RE-REPORT 5.2). */
static const uint8_t PATCH[16] = {
    0x64, 0x4f, 0xf2, 0x0b, 0x00, 0x71,
    0x3f, 0x52, 0x0b, 0x00, 0x0e, 0x00,
    0x00, 0x00, 0x00, 0x00
};

int main(void)
{
    const uint32_t rate   = 49716;
    const int      frames = rate / 10;   /* 0.1 s per note */

    opl3_chip chip;
    OPL3_Reset(&chip, rate);

    opl_dev d;
    opl_dev_init(&d, &chip, 1);
    opl_dev_program(&d, 0, PATCH);

    int16_t *buf = malloc((size_t)frames * 2 * sizeof(int16_t));
    if (!buf) return 1;

    int silent = 0;
    printf("note  block  peak\n");
    for (int note = 36; note <= 60; note++) {
        opl_dev_note_on(&d, 0, note);
        OPL3_GenerateStream(&chip, buf, (uint32_t)frames);
        opl_dev_note_off(&d, 0);

        int peak = 0;
        for (int i = 0; i < frames * 2; i++) {
            int a = buf[i] < 0 ? -buf[i] : buf[i];
            if (a > peak) peak = a;
        }
        printf(" %3d   %d      %6d%s\n", note, note / 12, peak,
               peak == 0 ? "  <-- SILENT" : "");
        if (peak == 0) silent++;
    }

    free(buf);
    if (silent) {
        printf("FAIL: %d note(s) produced silence\n", silent);
        return 1;
    }
    printf("OK: all notes audible across blocks 3..5\n");
    return 0;
}
