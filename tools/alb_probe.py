"""alb_probe - structural analysis of the 20 AD 01 ".ALB" old-format variant.

Scratch tool used to reverse the *container*; kept because the correlations it
prints are the evidence behind OLD_RLD.md section 11.2. For the cell encoding
see alb_cells.py and OLD_RLD.md section 11.3.

Two caveats, both documented in OLD_RLD.md section 11.2 and both deliberately
left in so the raw evidence is still visible here:

  * this reads the paragraph table as a fixed 128 entries; it is really
    pattern_count + 1 entries padded to a paragraph, so the tail printed by
    --detail past that point is pattern data, not table;
  * the 32-entry slot table is only meaningful for slots below n_instr - the
    export does not clear what it inherits, so present_count and n_instr
    disagree in 11 of the 14 distinct files.

Layout under test:
    +0x000  magic 20 AD 01
    +0x003  title, 20 bytes
    +0x018  order table, 128 x u8
    +0x098  instrument slot table, 32 x [present][volume]
    +0x118  track count
    +0x119  restart position
    +0x11A  instrument record count
    +0x11B  cue count
    +0x158  cue table, n_cue x 16 bytes          (VARIABLE - unlike B6)
    para =  0x158 + 16 * n_cue                   paragraph table
    ...     patterns, pattern i at para + 16 * tbl[i]
    ...     n_instr x 64-byte editor records, to EOF

Usage:  python tools/alb_probe.py <root-dir> [--detail NAME]
"""
import os
import struct
import sys

CUE = 0x158


def find(root):
    out = []
    for dirpath, _, names in os.walk(root):
        for n in names:
            p = os.path.join(dirpath, n)
            try:
                with open(p, 'rb') as f:
                    if f.read(3) == b'\x20\xad\x01':
                        out.append(p)
            except OSError:
                pass
    return out


def order_of(d):
    order = d[0x18:0x98]
    nz = [i for i, v in enumerate(order) if v]
    n = (max(nz) + 1) if nz else 1
    return order[:n]


def check(d):
    order = order_of(d)
    pc = max(order) + 1
    n_instr, n_cue = d[0x11A], d[0x11B]
    para = CUE + 16 * n_cue
    tbl = struct.unpack_from('<128H', d, para)
    blocks = para + 16 * tbl[pc]
    rem = len(d) - blocks
    mono = all(0 < tbl[i] < tbl[i + 1] for i in range(pc))
    ok = rem >= 0 and rem == 64 * n_instr and mono
    return dict(order=order, pc=pc, n_instr=n_instr, n_cue=n_cue, para=para,
                tbl=tbl, blocks=blocks, ok=ok, mono=mono, rem=rem)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else 'MEDIT'
    detail = None
    if '--detail' in sys.argv:
        detail = sys.argv[sys.argv.index('--detail') + 1]

    files = sorted(find(root))
    good = 0
    gapdirty = maxvol = maxslot = 0
    for p in files:
        name = os.path.basename(p)
        if detail and detail.upper() not in name.upper():
            continue
        d = open(p, 'rb').read()
        r = check(d)
        good += r['ok']
        if any(d[0xD8:0x118]):
            gapdirty += 1
        for i in range(64):
            if d[0x98 + 2 * i]:
                maxslot = max(maxslot, i + 1)
                maxvol = max(maxvol, d[0x99 + 2 * i])
        if not r['ok'] or detail:
            print('%-14s size=%-6d pat=%-3d instr=%-3d cue=%-3d para=0x%04X rem=%d %s'
                  % (name, len(d), r['pc'], r['n_instr'], r['n_cue'],
                     r['para'], r['rem'], 'OK' if r['ok'] else 'FAIL'))
        if detail:
            print('   order:', list(r['order']))
            print('   tbl:', [hex(x) for x in r['tbl'][:r['pc'] + 2]])
            for i in range(r['n_cue']):
                e = d[CUE + 16 * i: CUE + 16 * i + 16]
                print('   cue %2d %-14r %s' % (i, e[:10].rstrip(b'\x00 '), e[10:].hex()))

    print('\n%d/%d files fit the layout' % (good, len(files)))
    print('highest slot index used: %d   highest slot volume: %d (0x%02X)'
          % (maxslot, maxvol, maxvol))
    print('files with non-zero bytes in the 0xD8..0x117 gap: %d' % gapdirty)


if __name__ == '__main__':
    main()
