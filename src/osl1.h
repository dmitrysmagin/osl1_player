/* osl1.h - Ocean OSL1 container parser (Phase 1)
 *
 * Parses the shared OSL1 header used by .LAP/.ALB/.ADL/.SCC/.RLD files into a
 * Song struct: header fields, the instrument table, and the pattern block
 * (track/row counts, order table @block+0x50, position-pointer table
 * @block+0x150). See RE-REPORT.md sections 4/8 and PLAN.md Appendix A.
 *
 * All reads are explicit little-endian; we never rely on struct packing.
 */
#ifndef MEDPLAY_OSL1_H
#define MEDPLAY_OSL1_H

#include <stddef.h>
#include <stdint.h>

#define OSL1_MAX_INSTR  256
#define OSL1_MAX_ORDER  256

/* Device-type byte at header offset 0x07. */
enum {
    OSL1_DEV_GENERIC = 0,   /* Adlib/LAP (Generic) */
    OSL1_DEV_ROLAND  = 2,   /* Roland RLD          */
    OSL1_DEV_MED_LAP = 4,   /* MED LAP             */
    OSL1_DEV_SCC     = 8    /* Sound Blaster SCC   */
};

typedef struct {
    int      valid;         /* passed the table-consistency checks   */
    uint32_t offset;        /* file-absolute offset of this instrument */
    uint16_t len;           /* +0x00 total instrument data length     */
    uint16_t p1;            /* +0x04 param 1 (fine tune?)             */
    uint16_t p2;            /* +0x06 param 2 (volume?)                */
    char     name[21];      /* +0x0A 20-byte null-padded name         */
    uint8_t  adl[16];       /* +0x1E first 16 bytes = OPL2 ADL patch  */
} Instrument;

typedef struct {
    uint32_t block_off;     /* file-absolute offset of the block      */
    char     subtitle[17];  /* +0x00 16-byte null-padded subtitle     */
    uint8_t  restart_idx;   /* +0x10 loop-restart order (driver @0x417)    */
    uint16_t track_count;   /* +0x12 */
    uint16_t row_count;     /* +0x14 */
    uint16_t defaults[8];   /* +0x16 per-track defaults (0x7F7F=rest) */
    uint16_t checksum;      /* +0x26 */
    uint16_t ver_c;         /* +0x28 */
    uint16_t tempo;         /* +0x2A timer Hz, fed to set_tempo @0x422    */
    uint8_t  speed;         /* +0x2C initial ticks/row (driver @0x42C)    */
    uint8_t  order_count;   /* +0x4E */
    uint8_t  order[OSL1_MAX_ORDER];   /* +0x50 pattern order bytes    */
    uint32_t pos_ptr[OSL1_MAX_ORDER]; /* +0x150 file-absolute ptrs    */
} PatternBlock;

typedef struct {
    uint8_t      device;        /* +0x07 device type                  */
    uint8_t      version;       /* +0x04 */
    uint16_t     constant;      /* +0x05 */
    char         title[31];     /* +0x28 30-byte null-padded title    */
    uint32_t     block_off;     /* +0x48 pattern block offset         */
    uint16_t     instr_count;   /* +0x4C instrument count (table len) */
    uint16_t     instr_size;    /* +0x4E instrument data size         */

    uint16_t     instr_total;   /* number of table entries parsed     */
    uint16_t     instr_valid;   /* number that passed validation      */
    Instrument   instr[OSL1_MAX_INSTR];

    PatternBlock blk;

    uint8_t     *raw;           /* owned copy of the whole file       */
    size_t       size;
} Song;

/* Load and parse an OSL1 file. Returns 0 on success, non-zero on error
 * (errbuf, if non-NULL, receives a human-readable message). The Song owns a
 * heap copy of the file; call osl1_free() when done. */
int  osl1_load(const char *path, Song *song, char *errbuf, size_t errlen);
void osl1_free(Song *song);

/* Human-readable name for a device-type byte. */
const char *osl1_device_name(uint8_t device);

#endif /* MEDPLAY_OSL1_H */
