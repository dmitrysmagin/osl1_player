#!/usr/bin/env python3
"""Probe OSL1 instrument records to find a reliable per-instrument device/synth
discriminator. For each valid instrument prints: len, byte@+0x24, and whether
the 16-byte OPL2 patch region (+0x2E) carries real FM data vs is empty."""
import sys, struct

def u16(b, o): return b[o] | (b[o+1] << 8)
def u32(b, o): return b[o] | (b[o+1]<<8) | (b[o+2]<<16) | (b[o+3]<<24)

def analyse(path):
    b = open(path, 'rb').read()
    dev07 = b[0x07]
    n = u16(b, 0x4C)
    print(f"\n=== {path}  @0x07={dev07:#04x} instr_count={n} ===")
    print("  #  off     len  +24 +25 fm?  fmbytes(11)                  prog")
    for i in range(min(n, 60)):
        te = 0x50 + i*4
        off = u16(b, te+2)
        if off <= 0x4f or off+0x30 > len(b):
            continue
        L = u16(b, off)
        if L == 0:
            continue
        b24 = b[off+0x24]
        b25 = b[off+0x25]
        fm = b[off+0x2E:off+0x2E+11]
        fmnz = sum(1 for x in fm if x)          # nonzero FM bytes
        prog = b[off+0x30]
        isfm = "FM " if fmnz >= 4 else "   "
        print(f" {i:2}  0x{off:04X} {L:4} {b24:3} {b25:3}  {isfm} "
              f"{fm.hex():24}  {prog:3}")

for p in sys.argv[1:]:
    analyse(p)
