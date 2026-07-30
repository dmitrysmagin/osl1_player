/* oldrld.h - pre-OSL1 "old format" .RLD loader
 *
 * Some .RLD files under LAPMUSIC/OLDMUSIC are NOT OSL1 containers: they start
 * with the 3-byte magic { 0xB6, 0x9A, 0x01 } instead of "OSL1". MED.EXE loads
 * these via a separate, older code path (med.asm ~0x233b) that:
 *   - stores the song name, a 128-entry pattern order table and a 64-entry
 *     instrument present/volume table directly in the header,
 *   - stores patterns in a compressed per-row form (2 bits/track code word
 *     selecting how many of {note,instrument,effect,param} bytes follow),
 *   - stores one 256-byte instrument record per PRESENT instrument,
 *     sequentially, immediately after all pattern data (plus 16-byte
 *     alignment padding accumulated across the whole pattern stream).
 *
 * The per-track cell coding is bit-for-bit identical to the codes used by
 * OSL1's compressed position bitstream (see decode_cell() in replay.c), so
 * once decompressed into row-major "8 bytes/voice" records this old format
 * plays back on the *existing*, unmodified replay engine: we just need to
 * synthesise an uncompressed-position-shaped buffer and populate a Song the
 * same way osl1_load() does.
 *
 * See RE-REPORT.md / PLAN.md follow-up notes for the disassembly addresses
 * this was reverse-engineered from.
 */
#ifndef MEDPLAY_OLDRLD_H
#define MEDPLAY_OLDRLD_H

#include <stddef.h>
#include <stdint.h>
#include "osl1.h"

/* True if `raw` (of length >= 3) begins with the old-format magic. */
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
