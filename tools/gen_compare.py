"""gen_compare - confirm, field by field, how alike the three pre-OSL1
generations B4 9A 01, B6 9A 01 and 20 AD 01 actually are.

This is the evidence behind the "How alike are they, exactly?" section of
pre-OSL1.md. It reads all three generations with one code path wherever the
format permits, which is the point: the places it needs a branch are exactly
the places the formats differ.

Walks a corpus, deduplicates by SHA-1, and reports per generation:

  * header fields and their observed ranges (name, order, track count, restart)
  * slot-table semantics - the presence byte should only ever be 0 or 1, and an
    absent slot's volume should always be 0, in all three
  * paragraph table[0], which fixes the pattern-stream base
  * decoded cells: note range, instrument selector bound, effect histogram
  * the 0x0F parameter distribution, which is what settles Fxx = speed, not
    tempo (pre-OSL1.md section 9.1)
  * tail slack after the instrument region, which corroborates the block and
    record counts independently of anything parsed

Row decoding is deliberately reimplemented here rather than shelled out to
src/oldrld.c, so that agreement between the two is a real cross-check.

Usage:  python tools/gen_compare.py <root> [<root> ...]
"""
import hashlib
import os
import struct
import sys
from collections import Counter

# magic byte -> (label, instrument slots, fixed paragraph table, track_count off)
GEN = {0xB4: ('B4', 32, 0x118, 0x0D8),
       0xB6: ('B6', 64, 0x958, 0x118),
       0x20: ('ALB', 32, None, 0x118)}   # .ALB's table follows a variable cue table

ALB_RHYTHM_SLOTS = 5


def walk(roots):
    """Every old-format file under `roots`, deduplicated by content."""
    seen = {}
    for root in roots:
        for dirpath, _, names in os.walk(root):
            for n in names:
                p = os.path.join(dirpath, n)
                try:
                    d = open(p, 'rb').read()
                except OSError:
                    continue
                if len(d) < 0x320 or d[0] not in GEN:
                    continue
                want = b'\xad\x01' if d[0] == 0x20 else b'\x9a\x01'
                if d[1:3] != want:
                    continue
                seen.setdefault(hashlib.sha1(d).hexdigest(), (p, d))
    return sorted(seen.values())


def order_of(d):
    """Order length is the last non-zero index + 1, minimum 1 - all three."""
    o = d[0x18:0x98]
    nz = [i for i, v in enumerate(o) if v]
    return o[:(max(nz) + 1) if nz else 1]


def decode_bb(d, tracks, off):
    """B4/B6 row: u16 code word, 2 bits per track MSB-first, payload 0/2/2/7."""
    cells, p = [], off
    for _ in range(64):
        w = struct.unpack_from('<H', d, p)[0]
        p += 2
        for t in range(tracks):
            c = (w >> (14 - 2 * t)) & 3
            if c == 1:                                    # effect only
                cells.append((0, 0, d[p], d[p + 1])); p += 2
            elif c == 2:                                  # note + instrument
                cells.append((d[p], d[p + 1], 0, 0)); p += 2
            elif c == 3:                                  # full cell
                cells.append((d[p], d[p + 4], d[p + 5], d[p + 6])); p += 7
    return cells, p


def decode_alb(d, slots, off):
    """.ALB row: u16 presence mask MSB-first, one fixed 4-byte cell per bit."""
    cells, p = [], off
    for _ in range(64):
        m = struct.unpack_from('<H', d, p)[0]
        p += 2
        for s in range(slots):
            if m & (1 << (15 - s)):
                cells.append(tuple(d[p:p + 4])); p += 4
    return cells, p


def new():
    return dict(files=0, order_len=[], pat=[], named=0, stray17=0,
                tracks=Counter(), restart=[], flag=Counter(),
                vol_present=Counter(), vol_absent=Counter(), gap_dirty=0,
                width=Counter(), tbl0=Counter(), cells=0, fx=Counter(),
                fxF=Counter(), note_lo=999, note_hi=0, sel_hi=0,
                boundary_bad=0, slack=Counter(), n_cue=[], n_instr=[])


def analyse(d, s, g, slots, para_fixed, tc_off):
    s['files'] += 1
    order = order_of(d)
    pc = max(order) + 1
    s['order_len'].append(len(order))
    s['pat'].append(pc)
    s['named'] += 1 if d[3:0x17].rstrip(b'\x00 ') else 0
    s['stray17'] += 1 if d[0x17] else 0
    tracks = d[tc_off]
    s['tracks'][tracks] += 1
    s['restart'].append(d[tc_off + 1])

    present = 0
    for i in range(slots):
        f, v = d[0x98 + 2 * i], d[0x99 + 2 * i]
        s['flag'][f] += 1
        if f:
            present += 1
            s['vol_present'][v] += 1
        else:
            s['vol_absent'][v] += 1
    s['gap_dirty'] += 1 if any(d[0x98 + 2 * slots:tc_off]) else 0

    if g == 'ALB':
        n_instr, n_cue = d[0x11A], d[0x11B]
        s['n_instr'].append(n_instr)
        s['n_cue'].append(n_cue)
        para = 0x158 + 16 * n_cue
        width = tracks + ALB_RHYTHM_SLOTS
    else:
        para = para_fixed
        width = tracks
    s['width'][width] += 1

    tbl = struct.unpack_from('<%dH' % (pc + 1), d, para)
    s['tbl0'][tbl[0]] += 1

    for i in range(pc):
        off = para + 16 * tbl[i]
        cells, end = (decode_alb(d, width, off) if g == 'ALB'
                      else decode_bb(d, width, off))
        # the decode must land inside the paragraph the table gives to the next
        if not (off < end <= para + 16 * tbl[i + 1]):
            s['boundary_bad'] += 1
        for note, sel, cmd, prm in cells:
            s['cells'] += 1
            if note:
                s['note_lo'] = min(s['note_lo'], note)
                s['note_hi'] = max(s['note_hi'], note)
            s['sel_hi'] = max(s['sel_hi'], sel)
            if cmd or prm:
                s['fx'][cmd] += 1
                if cmd == 0x0F:
                    s['fxF'][prm] += 1

    tail = len(d) - (para + 16 * tbl[pc])
    s['slack'][tail - (64 * d[0x11A] if g == 'ALB' else 256 * present)] += 1


def rng(v):
    return '%d..%d' % (min(v), max(v)) if v else '-'


def main():
    roots = sys.argv[1:] or ['.']
    st = {k: new() for k in ('B4', 'B6', 'ALB')}
    for _, d in walk(roots):
        g, slots, para_fixed, tc_off = GEN[d[0]]
        analyse(d, st[g], g, slots, para_fixed, tc_off)

    rows = [
        ('distinct files',     lambda s: s['files']),
        ('track_count',        lambda s: dict(sorted(s['tracks'].items()))),
        ('row width (slots)',  lambda s: dict(sorted(s['width'].items()))),
        ('order length',       lambda s: rng(s['order_len'])),
        ('pattern count',      lambda s: rng(s['pat'])),
        ('named songs',        lambda s: s['named']),
        ('stray byte @0x17',   lambda s: s['stray17']),
        ('restart_idx',        lambda s: rng(s['restart'])),
        ('slot flag values',   lambda s: dict(sorted(s['flag'].items()))),
        ('present slot vol',   lambda s: 'max=%d %s' % (max(s['vol_present']),
                                                        s['vol_present'].most_common(2))),
        ('absent  slot vol',   lambda s: 'max=%d' % max(s['vol_absent'])),
        ('dirty gap b4 count', lambda s: s['gap_dirty']),
        ('para table[0]',      lambda s: dict(sorted(s['tbl0'].items()))),
        ('cells decoded',      lambda s: s['cells']),
        ('boundary misses',    lambda s: s['boundary_bad']),
        ('note range',         lambda s: '%d..%d' % (s['note_lo'], s['note_hi'])),
        ('max instr selector', lambda s: s['sel_hi']),
        ('tail slack',         lambda s: dict(sorted(s['slack'].items()))),
    ]
    w = 26
    print('%-19s | %-*s | %-*s | %s' % ('', w, 'B4 9A 01', w, 'B6 9A 01', '20 AD 01'))
    print('-' * 100)
    for label, fn in rows:
        print('%-19s | %-*s | %-*s | %s'
              % (label, w, fn(st['B4']), w, fn(st['B6']), fn(st['ALB'])))

    print('\neffect command histogram (B4 / B6 / ALB); >0x0F is out of range')
    for k in sorted(set(st['B4']['fx']) | set(st['B6']['fx']) | set(st['ALB']['fx'])):
        print('  0x%02X : %6d / %6d / %6d'
              % (k, st['B4']['fx'][k], st['B6']['fx'][k], st['ALB']['fx'][k]))

    print('\n0x0F parameter - tick counts, not Hz or BPM (section 9.1)')
    for g in ('B4', 'B6', 'ALB'):
        c = st[g]['fxF']
        print('  %-4s n=%-5d top %s   outside 1..0x20: %d'
              % (g, sum(c.values()), c.most_common(5),
                 sum(v for k, v in c.items() if not 1 <= k <= 0x20)))

    if st['ALB']['n_instr']:
        print('\n.ALB only: n_instr %s   n_cue %s'
              % (rng(st['ALB']['n_instr']), rng(st['ALB']['n_cue'])))


if __name__ == '__main__':
    main()
