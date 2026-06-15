/* osl1_dump - parity check for src/osl1.c against the reference Python dumper.
 *
 * Prints the parsed OSL1 fields for one or more files. Build via the Makefile
 * target `osl1_dump.exe`; compare its output to `python osl1_dump.py <file>`.
 */
#include <stdio.h>
#include "../src/osl1.h"

static void dump(const char *path)
{
    Song s;
    char err[128];
    if (osl1_load(path, &s, err, sizeof err) != 0) {
        printf("%s: ERROR: %s\n", path, err);
        return;
    }

    printf("========================================================================\n");
    printf("  OSL1 Dump: %s  (%zu bytes)\n", path, s.size);
    printf("========================================================================\n\n");

    printf("  Header:\n");
    printf("    version       %u\n", s.version);
    printf("    constant      0x%04X\n", s.constant);
    printf("    device        %u (%s)\n", s.device, osl1_device_name(s.device));
    printf("    title         \"%s\"\n", s.title);
    printf("    block_off     0x%08X\n", s.block_off);
    printf("    instr_count   %u (%u valid)\n", s.instr_count, s.instr_valid);
    printf("    instr_size    %u (0x%04X)\n\n", s.instr_size, s.instr_size);

    printf("  Instruments (%u table entries):\n", s.instr_total);
    printf("    #   off     len   p1     p2     name                  adl[0..15]\n");
    for (uint16_t i = 0; i < s.instr_total; i++) {
        const Instrument *in = &s.instr[i];
        printf("   %s%2u  0x%04X  %5u  0x%04X 0x%04X %-20s ",
               in->valid ? " " : "*", i, in->offset, in->len, in->p1, in->p2,
               in->name);
        if (in->valid) {
            for (int b = 0; b < 16; b++) printf("%02x", in->adl[b]);
        }
        printf("\n");
    }
    printf("\n");

    const PatternBlock *b = &s.blk;
    printf("  Pattern block @0x%08X:\n", b->block_off);
    printf("    subtitle      \"%s\"\n", b->subtitle);
    printf("    tracks        %u\n", b->track_count);
    printf("    rows          %u\n", b->row_count);
    printf("    defaults      ");
    for (int i = 0; i < 8; i++) printf("0x%04X%s", b->defaults[i], i < 7 ? " " : "\n");
    printf("    checksum      0x%04X\n", b->checksum);
    printf("    ver_c         0x%04X\n", b->ver_c);
    printf("    format_id     0x%04X\n", b->format_id);
    printf("    track_rel     0x%04X\n", b->track_rel);
    printf("    order_count   %u\n", b->order_count);

    printf("    order        ");
    for (uint16_t i = 0; i < b->order_count; i++) printf(" %02X", b->order[i]);
    printf("\n");

    printf("    pos_ptr (abs):\n");
    for (uint16_t i = 0; i < b->order_count; i++) {
        uint32_t cur = b->pos_ptr[i];
        if (i + 1 < b->order_count) {
            uint32_t nxt = b->pos_ptr[i + 1];
            long psz = (long)nxt - (long)cur;
            printf("      %3u  0x%06X -> 0x%06X  (%ld bytes)\n", i, cur, nxt, psz);
        } else {
            printf("      %3u  0x%06X  (last)\n", i, cur);
        }
    }
    printf("\n");

    osl1_free(&s);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: osl1_dump <file.osl1> [...]\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) dump(argv[i]);
    return 0;
}
