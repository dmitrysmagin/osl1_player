"""alb_cells - validate the .ALB pattern cell encoding across the corpus.

The `20 AD 01` variant does NOT use the B4/B6 2-bit-per-track code word. Each
row is a u16 *presence mask*, one bit per channel slot scanned MSB-first
(bit 15 = slot 0), followed by one fixed 4-byte cell per set bit, in ascending
slot order:

    +0  note        0 = none, else 17..96
    +1  instrument  1-based selector, 0 = no change
    +2  effect cmd  0..15, same set as B4/B6
    +3  effect par

so a row is always 2 + 4 * popcount(mask) bytes. See OLD_RLD.md section 11.3
for how this was distinguished from the 2-bit model (length validation alone
cannot: both give 4 * popcount).

This script decodes every pattern in every .ALB file and checks:

  * each pattern ends exactly where the paragraph table says the next begins,
    after padding to a 16-byte boundary;
  * no mask bit at or above track_count + 5 is ever set (section 11.4 - the
    five extra slots are the OPL2 percussion channels);
  * every instrument selector is within n_instr;
  * notes are 0 or in 17..96, and no cell is entirely empty.

Usage:  python tools/alb_cells.py <root-dir>
"""
import collections
import hashlib
import os
import struct
import sys

CUE = 0x158
CELL = 4
RHYTHM_SLOTS = 5


def find(root):
    """Distinct .ALB files by content (the corpus has nested duplicate trees)."""
    seen, out = set(), []
    for dirpath, _, names in os.walk(root):
        for n in names:
            p = os.path.join(dirpath, n)
            try:
                with open(p, 'rb') as f:
                    d = f.read()
            except OSError:
                continue
            if d[:3] != b'\x20\xad\x01':
                continue
            k = hashlib.sha1(d).hexdigest()
            if k in seen:
                continue
            seen.add(k)
            out.append((p, d))
    return sorted(out)


def decode_pattern(d, off, stats):
    """Return the byte length of one 64-row pattern starting at off."""
    n = 0
    for _ in range(64):
        mask = struct.unpack_from('<H', d, off + n)[0]
        n += 2
        for slot in range(16):
            if not (mask >> (15 - slot)) & 1:
                continue
            note, ins, cmd, par = d[off + n: off + n + CELL]
            n += CELL
            stats['cells'] += 1
            stats['max_slot'] = max(stats['max_slot'], slot + 1)
            stats['max_instr'] = max(stats['max_instr'], ins)
            stats['effects'][cmd] += 1
            if note:
                stats['note_lo'] = min(stats['note_lo'], note)
                stats['note_hi'] = max(stats['note_hi'], note)
            if not (note or ins or cmd or par):
                stats['empty'] += 1
    return n


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else 'MEDIT'
    files = find(root)
    ok = bad = 0
    total = collections.Counter()
    effects = collections.Counter()
    note_lo, note_hi = 255, 0

    for p, d in files:
        order = d[0x18:0x98]
        nz = [i for i, v in enumerate(order) if v]
        pc = max(order[:(max(nz) + 1) if nz else 1]) + 1
        tracks, n_instr = d[0x118], d[0x11A]
        para = CUE + 16 * d[0x11B]
        tbl = struct.unpack_from('<%dH' % (pc + 1), d, para)

        stats = dict(cells=0, max_slot=0, max_instr=0, empty=0,
                     note_lo=255, note_hi=0, effects=collections.Counter())
        good, why = True, ''
        for i in range(pc):
            start = para + 16 * tbl[i]
            end = para + 16 * tbl[i + 1]
            try:
                n = decode_pattern(d, start, stats)
            except (struct.error, ValueError, IndexError):
                good, why = False, 'pat %d: ran off the end of the file' % i
                break
            pad = (16 - (n & 15)) & 15
            if start + n + pad != end:
                good = False
                why = ('pat %2d: decoded %d(+%d pad) -> 0x%X, table says 0x%X'
                       % (i, n, pad, start + n + pad, end))
                break

        if good and stats['max_slot'] > tracks + RHYTHM_SLOTS:
            good, why = False, ('slot %d used, but track_count+%d = %d'
                                % (stats['max_slot'], RHYTHM_SLOTS,
                                   tracks + RHYTHM_SLOTS))
        if good and stats['max_instr'] > n_instr:
            good, why = False, ('instrument selector %d > n_instr %d'
                                % (stats['max_instr'], n_instr))
        if good and stats['empty']:
            good, why = False, '%d wholly empty cells' % stats['empty']

        ok += good
        bad += not good
        if not good:
            print('%-14s %s' % (os.path.basename(p), why))
        total['cells'] += stats['cells']
        effects += stats['effects']
        if stats['note_lo'] <= stats['note_hi']:
            note_lo = min(note_lo, stats['note_lo'])
            note_hi = max(note_hi, stats['note_hi'])

    print('\n%d/%d files decode cleanly (%d bad)' % (ok, len(files), bad))
    print('cells decoded: %d   note range: %d..%d'
          % (total['cells'], note_lo, note_hi))
    print('effects: %s'
          % ', '.join('%02X:%d' % kv for kv in sorted(effects.items())))


if __name__ == '__main__':
    main()
