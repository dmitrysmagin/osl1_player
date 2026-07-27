#!/usr/bin/env python3
"""Decode a DOSBox DRO (v2.0) OPL capture into a register-write trace.

Output format matches medplay's --trace: one `RRR=VV` line per register write
(RRR = 3-hex-digit register incl. 0x100 bank, VV = 2-hex-digit value).
Delays are emitted as comment lines `; delay <ms>` so the two streams can be
segmented into frames when diffing.

Usage: dro_dump.py <file.dro> [--with-delays]
"""
import sys, struct

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    with_delays = '--with-delays' in sys.argv
    data = open(args[0], 'rb').read()

    assert data[:8] == b'DBRAWOPL', 'not a DRO file'
    vmaj, vmin = struct.unpack_from('<HH', data, 8)
    assert (vmaj, vmin) == (2, 0), f'unsupported DRO version {vmaj}.{vmin}'
    length_pairs, length_ms = struct.unpack_from('<II', data, 12)
    hw_type   = data[20]
    fmt       = data[21]
    comp      = data[22]
    short_code= data[23]
    long_code = data[24]
    codemap_len = data[25]
    codemap = data[26:26+codemap_len]
    off = 26 + codemap_len

    sys.stderr.write(
        f'DRO2: pairs={length_pairs} ms={length_ms} hw={hw_type} fmt={fmt} '
        f'comp={comp} short={short_code:#x} long={long_code:#x} '
        f'codemap_len={codemap_len}\n')

    out = []
    n = 0
    while off + 1 < len(data) and n < length_pairs:
        idx = data[off]; val = data[off+1]; off += 2; n += 1
        if idx == short_code:
            if with_delays: out.append(f'; delay {val+1}')
            continue
        if idx == long_code:
            if with_delays: out.append(f'; delay {(val+1)<<8}')
            continue
        high = idx & 0x80
        reg = codemap[idx & 0x7f]
        full = reg | (0x100 if high else 0)
        out.append(f'{full:03X}={val:02X}')
    print('\n'.join(out))

if __name__ == '__main__':
    main()
