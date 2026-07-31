/* oldrld.h - pre-OSL1 "old format" .RLD loader (generations B4 and B6)
 *
 * Many .RLD files under LAPMUSIC/OLDMUSIC are NOT OSL1 containers: they start
 * with a 3-byte magic `Bn 9A 01` where the first byte is a *generation*
 * counter, not a device id (see RE-REPORT.md section 11.1b - the B4/B6 split
 * is chronological, both generations hold FM data, and the A/ vs R/ folder
 * convention is the orthogonal Adlib/Roland distinction).
 *
 * Two generations are supported:
 *
 *   B4  1991      32 instrument slots.  Played by the standalone INT 60h
 *                 driver LAPMUSIC/OLDMUSIC/BSSJS/ADLIB.DRV (see
 *                 BSSJS_ADLIB.DRV.annotated.asm).  MED.EXE cannot open these
 *                 at all - its magic test is a bare `cmpw $0x9ab6`.
 *   B6  1991-93   64 instrument slots.  Loaded by MED.EXE (med.asm ~0x233b).
 *
 * The two differ only in the slot count and the fields that shift as a
 * result; the pattern encoding is bit-for-bit identical, and identical in
 * turn to OSL1's compressed position bitstream (see decode_cell() in
 * replay.c). OLD_RLD.md is the full byte-level specification of both.
 *
 * Layout, with B4/B6 offsets side by side:
 *
 *   +0x000   magic `Bn 9A 01`
 *   +0x003   song title, 20 bytes, NUL padded
 *   +0x018   pattern order table, 128 bytes of pattern indices
 *   +0x098   instrument slot table, { present:u8, volume:u8 } per slot
 *              B4: 32 slots (ends 0x0D8)      B6: 64 slots (ends 0x118)
 *   B4 0x0D8 / B6 0x118   track count
 *   B4 0x0D9 / B6 0x119   restart order position
 *   B6 0x158              cue-label table, 128 x 16 bytes (B6 only)
 *   B4 0x118 / B6 0x958   paragraph offset table, 256 x u16
 *   ...      pattern stream; pattern i starts at
 *              para_table_off + para[i] * 16
 *            (the paragraph offsets are relative to the table's own offset -
 *            verified on all 500 old-format files in the corpus)
 *   ...      one 256-byte instrument block per PRESENT slot, sequentially,
 *            starting at para_table_off + para[max_pattern + 1] * 16.
 *            The 16-byte OPL2 patch is split around the 10-byte name:
 *            +0x00..+0x07 then +0x12..+0x19.
 *   ...      B4 only (and optionally B6): a 2048-byte Adlib editor bank of
 *            32 x 64-byte records, running to EOF.
 *
 * Both instrument sources are parsed. Which one voices playback follows what
 * the era's own driver did, and can be overridden - see oldrld_set_fm_source.
 */
#ifndef MEDPLAY_OLDRLD_H
#define MEDPLAY_OLDRLD_H

#include <stddef.h>
#include <stdint.h>
#include "osl1.h"

/* Which instrument source supplies the OPL2 patch for an old-format file.
 *
 * AUTO follows the driver of the era, which is the historically faithful
 * choice and the default:
 *   B4 -> EDITOR.  ADLIB.DRV reads only the 64-byte editor bank (its
 *         inst_fetch @0x07B8 does `slot * 64 + inst_base`); it never looks at
 *         the 256-byte blocks, which in a B4 file typically hold the Roland /
 *         MT-32 instrument set instead.
 *   B6 -> BLOCK.   MED.EXE reads only the 256-byte blocks (med.asm 0x25DC),
 *         and that path is verified byte-for-byte against the OSL1 corpus
 *         (RE-REPORT.md section 11.5). Only 17 B6 files carry a non-blank
 *         editor bank at all.
 * EDITOR silently falls back to BLOCK when a file has no editor bank. */
typedef enum {
    OLDRLD_FM_AUTO = 0,
    OLDRLD_FM_BLOCK,
    OLDRLD_FM_EDITOR
} OldrldFmSource;

/* Select the patch source for subsequent oldrld_load() calls. Module-level
 * rather than a parameter so it does not have to thread through
 * osl1_load()'s signature; set it once at start-up. */
void oldrld_set_fm_source(OldrldFmSource src);

/* Generation byte (0xB4 or 0xB6) of `raw`, or 0 if it is not an old-format
 * file. */
uint8_t oldrld_generation(const uint8_t *raw, size_t size);

/* True if `raw` (of length >= 3) begins with an old-format magic. */
int oldrld_is_old_format(const uint8_t *raw, size_t size);

/* Parse an already-loaded old-format file. `song->raw`/`song->size` must
 * already hold the whole file (as set up by osl1_load() before it inspects
 * the signature); on success this function REPLACES song->raw with a newly
 * allocated synthetic buffer holding decompressed, OSL1-position-shaped
 * pattern data, frees the original raw buffer, and fills in the rest of
 * `song` (title, instruments, blk) the same way osl1_load() would. Returns 0
 * on success, non-zero on error (errbuf, if non-NULL, gets a message). On
 * error the Song is left safely freeable via osl1_free(). */
int oldrld_load(Song *song, char *errbuf, size_t errlen);

#endif /* MEDPLAY_OLDRLD_H */
