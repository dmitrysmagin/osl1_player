/* oldrld.h - pre-OSL1 "old format" song loader (generations B4, B6 and .ALB)
 *
 * Many .RLD files under LAPMUSIC/OLDMUSIC are NOT OSL1 containers: they start
 * with a 3-byte magic `Bn 9A 01` where the first byte is a *generation*
 * counter, not a device id (see RE-REPORT.md section 11.1b - the B4/B6 split
 * is chronological, both generations hold FM data, and the A/ vs R/ folder
 * convention is the orthogonal Adlib/Roland distinction).
 *
 * Three generations are supported:
 *
 *   B4  1991      32 instrument slots.  Played by the standalone INT 60h
 *                 driver LAPMUSIC/OLDMUSIC/BSSJS/ADLIB.DRV (see
 *                 BSSJS_ADLIB.DRV.annotated.asm).  MED.EXE cannot open these
 *                 at all - its magic test is a bare `cmpw $0x9ab6`.
 *   B6  1991-93   64 instrument slots.  Loaded by MED.EXE (med.asm ~0x233b).
 *   ALB 1992      magic `20 AD 01`, always a .ALB file. A *runtime export*
 *                 rather than an editor working file: no 256-byte instrument
 *                 blocks, a variable-length cue table, and a different (much
 *                 simpler) pattern encoding. 45 files in the corpus.
 *
 * B4 and B6 differ only in the slot count and the fields that shift as a
 * result; their pattern encoding is bit-for-bit identical, and identical in
 * turn to OSL1's compressed position bitstream (see decode_cell() in
 * replay.c). .ALB shares the header and the paragraph-addressing scheme but
 * not the cell coding. RLD.md and ALB.md are the full byte-level specifications.
 *
 * Layout, with B4/B6/.ALB offsets side by side:
 *
 *   +0x000   magic `Bn 9A 01`, or `20 AD 01` for .ALB
 *   +0x003   song title, 20 bytes, NUL padded
 *   +0x018   pattern order table, 128 bytes of pattern indices
 *   +0x098   instrument slot table, { present:u8, volume:u8 } per slot
 *              B4/.ALB: 32 slots (ends 0x0D8)   B6: 64 slots (ends 0x118)
 *   B4 0x0D8 / B6+ALB 0x118   track count
 *   B4 0x0D9 / B6+ALB 0x119   restart order position
 *   ALB 0x11A             instrument record count
 *   ALB 0x11B             cue count
 *   B6 0x158              cue-label table, 128 x 16 bytes, fixed size
 *   ALB 0x158             cue-label table, n_cue x 16 bytes, VARIABLE size
 *   B4 0x118 / B6 0x958 / ALB 0x158 + 16*n_cue   paragraph offset table
 *   ...      pattern stream; pattern i starts at
 *              para_table_off + para[i] * 16
 *            (the paragraph offsets are relative to the table's own offset -
 *            verified on all 545 old-format files in the corpus)
 *   ...      B4/B6: one 256-byte instrument block per PRESENT slot,
 *            sequentially, starting at para_table_off + para[n_pat] * 16.
 *            The 16-byte OPL2 patch is split around the 10-byte name:
 *            +0x00..+0x07 then +0x12..+0x19.
 *   ...      B4 (and optionally B6): a 2048-byte Adlib editor bank of
 *            32 x 64-byte records, running to EOF.
 *   ...      .ALB: no blocks at all - just n_instr 64-byte editor records,
 *            one per slot (blank records included), running to EOF.
 *
 * For B4/B6 both instrument sources are parsed, and which one voices playback
 * follows what the era's own driver did; it can be overridden - see
 * oldrld_set_fm_source. .ALB has only the editor records, so the setting has
 * no effect there.
 */
#ifndef MEDPLAY_OLDRLD_H
#define MEDPLAY_OLDRLD_H

#include <stddef.h>
#include <stdint.h>
#include "osl1.h"

/* Value oldrld_generation() returns for the `20 AD 01` .ALB variant. It is the
 * first magic byte, chosen for the same reason 0xB4/0xB6 are: it is what the
 * file actually starts with, so dumps and error messages stay literal. */
#define OLDRLD_GEN_ALB 0x20

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

/* Generation byte (0xB4, 0xB6 or OLDRLD_GEN_ALB) of `raw`, or 0 if it is not
 * an old-format file. */
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
