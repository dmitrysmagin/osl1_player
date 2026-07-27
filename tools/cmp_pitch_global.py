#!/usr/bin/env python3
"""Allocation-independent pitch comparison: aggregate the distinct (block,fnum)
pairs across ALL 9 channels, since DOSBox and medplay may map a logical voice
onto different physical OPL channels. Reports how closely the two pitch-value
clouds overlap."""
import sys

def load(path):
    a0 = {}   # keyed by (bank, ch) so dual-OPL2 banks don't clobber each other
    pairs = set()
    fnums = set()
    with open(path) as f:
        for ln in f:
            ln = ln.strip()
            if not ln or ln.startswith(';') or '=' not in ln:
                continue
            reg, _, val = ln.partition('=')
            reg, val = reg.strip(), val.strip()
            if len(val.split()) != 1:
                continue
            try:
                r = int(reg, 16); v = int(val, 16)
            except ValueError:
                continue
            bank = 1 if (r & 0x100) else 0
            lo = r & 0xff
            if 0xa0 <= lo <= 0xa8:
                a0[(bank, lo - 0xa0)] = v
            elif 0xb0 <= lo <= 0xb8:
                ch = lo - 0xb0
                fnum = ((v & 0x03) << 8) | a0.get((bank, ch), 0)
                block = (v >> 2) & 0x07
                pairs.add((block, fnum))
                fnums.add(fnum)
    return pairs, fnums

pa, na = load(sys.argv[1])
pb, nb = load(sys.argv[2])
print(f"A={sys.argv[1]}  pairs={len(pa)} fnums={len(na)}")
print(f"B={sys.argv[2]}  pairs={len(pb)} fnums={len(nb)}")
print(f"(block,fnum) shared={len(pa & pb)} onlyA={len(pa - pb)} onlyB={len(pb - pa)}")
print(f"fnum        shared={len(na & nb)} onlyA={len(na - nb)} onlyB={len(nb - na)}")
# proximity: for each A fnum, nearest B fnum distance
def near(xs, ys):
    ys = sorted(ys)
    import bisect
    out = []
    for x in xs:
        i = bisect.bisect_left(ys, x)
        best = min((abs(x - ys[j]) for j in (i-1, i, i+1) if 0 <= j < len(ys)), default=99999)
        out.append(best)
    return out
d = near(na, nb)
d.sort()
if d:
    print(f"fnum nearest-neighbour dist: max={d[-1]} median={d[len(d)//2]} "
          f"<=8:{sum(1 for x in d if x<=8)}/{len(d)}")
