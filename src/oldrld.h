/* oldrld.h - pre-OSL1 `.RLD` song loader, generations B4 and B6
 *
 * Many .RLD files under LAPMUSIC/OLDMUSIC are NOT OSL1 containers: they start
 * with a 3-byte magic `Bn 9A 01` where the first byte is a *generation*
 * counter, not a device id (see RE-REPORT.md section 11.1b - the B4/B6 split
 * is chronological, both generations hold FM data, and the A/ vs R/ folder
 * convention is the orthogonal Adlib/Roland distinction).
 *
 *   B4  1991      32 instrument slots.  Played by the standalone INT 60h
 *                 driver LAPMUSIC/OLDMUSIC/BSSJS/ADLIB.DRV (see
 *                 BSSJS_ADLIB.DRV.annotated.asm).  MED.EXE cannot open these
 *                 at all - its magic test is a bare `cmpw $0x9ab6`.
 *   B6  1991-93   64 instrument slots.  Loaded by MED.EXE (med.asm ~0x233b).
 *
 * The two differ only in the slot count and the fields that shift as a result;
 * their pattern encoding is bit-for-bit identical, and identical in turn to
 * OSL1's compressed position bitstream (see decode_cell() in replay.c).
 * RLD.md is the full byte-level specification.
 *
 * The third pre-OSL1 generation, `20 AD 01` (.ALB), shares the first 0x98
 * bytes of this header and the paragraph-addressing scheme but not the cell
 * coding, the instrument region or the row width. It has its own loader -
 * see oldalb.h and ALB.md.
 *
 * Layout, with B4/B6 offsets side by side:
 *
 *   +0x000   magic `Bn 9A 01`
 *   +0x003   song title, 20 bytes, NUL padded
 *   +0x018   pattern order table, 128 bytes of pattern indices
 *   +0x098   instrument slot table, { present:u8, volume:u8 } per slot
 *              B4: 32 slots (ends 0x0D8)        B6: 64 slots (ends 0x118)
 *   B4 0x0D8 / B6 0x118   track count
 *   B4 0x0D9 / B6 0x119   restart order position
 *   B6 0x158              cue-label table, 128 x 16 bytes, fixed size
 *   B4 0x118 / B6 0x958   paragraph offset table
 *   ...      pattern stream; pattern i starts at
 *              para_table_off + para[i] * 16
 *            (the paragraph offsets are relative to the table's own offset -
 *            verified on all 545 old-format files in the corpus)
 *   ...      one 256-byte instrument block per PRESENT slot, sequentially,
 *            starting at para_table_off + para[n_pat] * 16. The 16-byte OPL2
 *            patch is split around the 10-byte name: +0x00..+0x07 then
 *            +0x12..+0x19.
 *   ...      B4 (and optionally B6): a 2048-byte Adlib editor bank of
 *            32 x 64-byte records, running to EOF.
 *
 * Both instrument sources are parsed, and which one voices playback follows
 * what the era's own driver did; it can be overridden - see
 * oldrld_set_fm_source.
 */
#ifndef MEDPLAY_OLDRLD_H
#define MEDPLAY_OLDRLD_H

#include <stddef.h>
#include <stdint.h>
#include "osl1.h"
#include "oldfmt.h"

/* Choose which instrument source supplies the OPL2 patch for a B4/B6 file.
 * The values are OldFmtFmSource (oldfmt.h), because Song.old_fm_source reports
 * them for .ALB files too - but only B4/B6 carry both sources, so this is the
 * only place the choice can be made.
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
 * EDITOR silently falls back to BLOCK when a file has no editor bank.
 *
 * Module-level rather than a parameter so it does not have to thread through
 * osl1_load()'s signature; set it once at start-up. */
void oldrld_set_fm_source(OldFmtFmSource src);

/* Parse an already-loaded B4 or B6 file. `song->raw`/`song->size` must already
 * hold the whole file (as set up by osl1_load() before it inspects the
 * signature); on success this function REPLACES song->raw with a newly
 * allocated synthetic buffer holding decompressed, OSL1-position-shaped
 * pattern data, frees the original raw buffer, and fills in the rest of
 * `song` (title, instruments, blk) the same way osl1_load() would. Returns 0
 * on success, non-zero on error (errbuf, if non-NULL, gets a message). On
 * error the Song is left safely freeable via osl1_free().
 *
 * Callers dispatch on oldfmt_generation(): 0xB4 and 0xB6 come here, and
 * OLDFMT_GEN_ALB goes to oldalb_load() instead. */
int oldrld_load(Song *song, char *errbuf, size_t errlen);

#endif /* MEDPLAY_OLDRLD_H */
