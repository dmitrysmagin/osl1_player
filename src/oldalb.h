/* oldalb.h - pre-OSL1 `.ALB` song loader, magic `20 AD 01`
 *
 * The third and last pre-OSL1 generation, and the odd one out. Where B4 and B6
 * are editor working files (see oldrld.h), an .ALB is a *runtime export*: what
 * the "ADLIB DRIVER (Version 3.00)" of September 1991 was handed to play, with
 * everything the editor needed and the driver did not already stripped out.
 * 22 paths / 16 distinct files in the corpus. ALB.md is the full byte-level
 * specification.
 *
 * It shares the first 0x98 bytes of the B4/B6 header and the paragraph
 * addressing scheme, and nothing else that matters:
 *
 *   - the cell encoding is a presence mask with fixed 4-byte cells, not a
 *     2-bit-per-track code word with variable payloads;
 *   - a row carries track_count + 5 slots, the extra five being the OPL2's
 *     rhythm-mode percussion channels;
 *   - there are no 256-byte instrument blocks at all, only 64-byte Adlib
 *     editor records - so --fm-source has nothing to choose between;
 *   - the cue table is variable length, which makes the paragraph table's
 *     offset variable too;
 *   - the paragraph table holds exactly pattern_count + 1 entries, with no
 *     stale tail.
 *
 * Layout:
 *
 *   +0x000   magic `20 AD 01`
 *   +0x003   song title, 20 bytes, NUL padded
 *   +0x018   pattern order table, 128 bytes of pattern indices
 *   +0x098   instrument slot table, 32 x { present:u8, volume:u8 }, ends 0x0D8
 *   +0x0D8   64 zero bytes (B6's extra slots, unused here)
 *   +0x118   track count
 *   +0x119   restart order position
 *   +0x11A   instrument record count
 *   +0x11B   cue count
 *   +0x158   cue-label table, n_cue x 16 bytes, VARIABLE size
 *   +0x158 + 16*n_cue   paragraph offset table, pattern_count + 1 entries
 *   ...      pattern stream; pattern i starts at para_table_off + para[i] * 16
 *   ...      n_instr 64-byte editor records, one per slot (blank records
 *            included), running to EOF
 */
#ifndef MEDPLAY_OLDALB_H
#define MEDPLAY_OLDALB_H

#include <stddef.h>
#include <stdint.h>
#include "osl1.h"

/* Parse an already-loaded `20 AD 01` file. Contract is identical to
 * oldrld_load(): `song->raw`/`song->size` must hold the whole file, and on
 * success song->raw is REPLACED with a newly allocated synthetic buffer
 * holding decompressed, OSL1-position-shaped pattern data. Returns 0 on
 * success, non-zero on error (errbuf, if non-NULL, gets a message). On error
 * the Song is left safely freeable via osl1_free().
 *
 * Callers dispatch on oldfmt_generation(): OLDFMT_GEN_ALB comes here, 0xB4 and
 * 0xB6 go to oldrld_load() instead. */
int oldalb_load(Song *song, char *errbuf, size_t errlen);

#endif /* MEDPLAY_OLDALB_H */
