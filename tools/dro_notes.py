#!/usr/bin/env python3
"""Extract note-on events from a DRO2 capture as (frame, channel, note).

Reverse-maps each key-on (0xB0 write with bit5 going 0->1) to a note number
using the Adlib FNUM table + block, so it can be compared with the replay
engine's decoded note values.
"""
import sys, struct

FNUM = [0x157,0x16C,0x181,0x198,0x1B1,0x1CB,0x1E6,0x203,0x222,0x243,0x266,0x28A]

def nearest_semitone(fnum_lo):
    # match low byte against the table (block handles the octave)
    best, bd = 0, 999
    for i, f in enumerate(FNUM):
        d = abs((f & 0xFF) - fnum_lo)
        if d < bd: bd, best = d, i
    return best

def main():
    data = open(sys.argv[1], 'rb').read()
    assert data[:8] == b'DBRAWOPL'
    length_pairs, length_ms = struct.unpack_from('<II', data, 12)
    short_code, long_code = data[23], data[24]
    codemap_len = data[25]
    codemap = data[26:26+codemap_len]
    off = 26 + codemap_len

    a0 = {}   # channel -> last A0 low byte
    b0 = {}   # channel -> last B0 value
    frame = 0
    n = 0
    events = []
    while off + 1 < len(data) and n < length_pairs:
        idx = data[off]; val = data[off+1]; off += 2; n += 1
        if idx == short_code: frame += val + 1; continue
        if idx == long_code:  frame += (val + 1) << 8; continue
        reg = codemap[idx & 0x7f] | (0x100 if idx & 0x80 else 0)
        base = reg & 0xFF
        if 0xA0 <= base <= 0xA8:
            a0[base - 0xA0] = val
        elif 0xB0 <= base <= 0xB8:
            ch = base - 0xB0
            prev = b0.get(ch, 0)
            b0[ch] = val
            if (val & 0x20) and not (prev & 0x20):  # key-on rising edge
                block = (val >> 2) & 7
                lo = a0.get(ch, 0)
                semi = nearest_semitone(lo)
                note = block * 12 + semi
                events.append((frame, ch, note, lo, val))
    for fr, ch, note, lo, bv in events[:40]:
        print(f"t={fr:6d}ms ch{ch} note={note:3d} (A0={lo:02X} B0={bv:02X})")
    print(f"... {len(events)} note-ons total")

if __name__ == '__main__':
    main()
