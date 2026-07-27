#!/usr/bin/env python3
"""Compare A0/B0 pitch-register distributions between a medplay --trace and a
DRO dump (via dro_dump.py). Both use `RRR=VV` text lines; DRO may have a
banner and `; delay` comments. We collect, per channel 0..8, the set of
distinct fnum (A0 low | (B0 fnum-hi)) and block values seen, ignoring the
KEYON bit so a note-on/off pair does not look like two pitches."""
import sys

def load(path):
    a0 = {}   # reg 0xA0..0xA8 last value per channel
    fnums = [set() for _ in range(9)]
    blocks = [set() for _ in range(9)]
    with open(path) as f:
        for ln in f:
            ln = ln.strip()
            if not ln or ln.startswith(';') or '=' not in ln:
                continue
            reg, _, val = ln.partition('=')
            reg = reg.strip()
            val = val.strip()
            if len(val.split()) != 1:
                continue
            try:
                r = int(reg, 16)
                v = int(val, 16)
            except ValueError:
                continue
            lo = r & 0xff
            if 0xa0 <= lo <= 0xa8:
                ch = lo - 0xa0
                a0[ch] = v
            elif 0xb0 <= lo <= 0xb8:
                ch = lo - 0xb0
                low = a0.get(ch, 0)
                fnum = ((v & 0x03) << 8) | low
                block = (v >> 2) & 0x07
                fnums[ch].add(fnum)
                blocks[ch].add(block)
    return fnums, blocks

fa, ba = load(sys.argv[1])
fb, bb = load(sys.argv[2])
print(f"A={sys.argv[1]}")
print(f"B={sys.argv[2]}")
for ch in range(9):
    inter = fa[ch] & fb[ch]
    onlyA = fa[ch] - fb[ch]
    onlyB = fb[ch] - fa[ch]
    if not (fa[ch] or fb[ch]):
        continue
    print(f"ch{ch}: fnums A={len(fa[ch])} B={len(fb[ch])} "
          f"shared={len(inter)} onlyA={len(onlyA)} onlyB={len(onlyB)} "
          f"blocksA={sorted(ba[ch])} blocksB={sorted(bb[ch])}")
