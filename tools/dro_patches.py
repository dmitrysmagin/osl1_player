#!/usr/bin/env python3
"""Track full OPL register state through a DRO2 capture and, at each note-on,
print the operator patch active on that channel (mod/car 0x20/40/60/80/E0 +
0xC0). Distinct patches == the instruments MED.EXE actually programmed, so we
can reverse-engineer the song's instrument byte layout."""
import sys, struct

OP_OFF = [0x00,0x01,0x02,0x08,0x09,0x0A,0x10,0x11,0x12]

def main():
    d = open(sys.argv[1], 'rb').read()
    lp, = struct.unpack_from('<I', d, 12)
    sc, lc = d[23], d[24]
    cl = d[25]; cm = d[26:26+cl]; off = 26+cl
    reg = [0]*0x200          # full register file
    b0 = {}
    n = 0
    seen = {}
    order = []
    while off+1 < len(d) and n < lp:
        i, v = d[off], d[off+1]; off += 2; n += 1
        if i in (sc, lc): continue
        r = cm[i & 0x7f] | (0x100 if i & 0x80 else 0)
        reg[r] = v
        base = r & 0xFF
        if 0xB0 <= base <= 0xB8:
            ch = base - 0xB0
            prev = b0.get(ch, 0); b0[ch] = v
            if (v & 0x20) and not (prev & 0x20):
                m = OP_OFF[ch]; c = m + 3
                patch = (
                    reg[0x20+m], reg[0x40+m], reg[0x60+m], reg[0x80+m], reg[0xE0+m],
                    reg[0x20+c], reg[0x40+c], reg[0x60+c], reg[0x80+c], reg[0xE0+c],
                    reg[0xC0+ch],
                )
                key = patch
                if key not in seen:
                    seen[key] = len(seen)
                    order.append((ch, patch))
    print("distinct operator patches seen at note-ons (mod0-4, car0-4, C0):")
    for idx, (ch, p) in enumerate(order):
        s = ' '.join('%02X' % x for x in p)
        print(f"  P{idx}: [{s}]  (first on ch{ch})")

if __name__ == '__main__':
    main()
