#!/usr/bin/env python3
"""osl1_scan.py - corpus-wide OSL1 scanner.

Walks a directory tree, parses every file that carries the "OSL1" signature,
and dumps:

  * the instrument table with each instrument's synth-type code (record+0x24),
    FM-vs-MIDI classification and GM program;
  * the pattern block's declared track count (== the replay engine's
    voice_count), and the MAX number of voice-cells actually FILLED with data
    across all positions (the compressed-position header's `bp` word, which the
    DOS engine uses as the per-position cell count).

Motivation: test the theory that per-instrument synth codes 0x02 / 0x04 mean
"single OPL2" vs "dual OPL2". A single OPL2 offers 9 melodic channels; if any
song ever fills more than 9 voices, more than one chip's worth of channels is
in play. The tool therefore reports the channel/voice ceiling across the whole
corpus alongside the synth-code mix.

Parsing mirrors src/osl1.c byte-for-byte (little-endian, no struct packing).

Usage:
    python osl1_scan.py [root ...]        # default root = current directory
    python osl1_scan.py --verbose [root]  # per-instrument detail lines
    python osl1_scan.py --csv out.csv [root]
"""
import os
import sys

# Report output must survive whatever the console/redirect codec is (Windows
# often defaults to cp1251), so force UTF-8 with a replacement fallback.
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except (AttributeError, ValueError):
    pass

SYNTH_NAMES = {0x02: "FM-short", 0x04: "FM-ext", 0x08: "MIDI"}


def u16(d, o):
    return d[o] | (d[o + 1] << 8) if o + 2 <= len(d) else 0


def u32(d, o):
    return (d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24)
            if o + 4 <= len(d) else 0)


def rd_str(d, o, n):
    if o >= len(d):
        return ""
    raw = d[o:o + n].split(b"\x00", 1)[0]
    # keep printable ASCII only; makes the report safe under any output codec
    return "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in raw)


class Instr:
    __slots__ = ("idx", "valid", "offset", "length", "synth", "program",
                 "fm", "fm_nz", "name")


def parse(d):
    """Return a dict of parsed fields, or None if not an OSL1 file."""
    if len(d) <= 0x50 or d[:4] != b"OSL1":
        return None
    sz = len(d)
    song = {
        "size": sz,
        "gen": d[0x07],
        "title": rd_str(d, 0x28, 30),
        "block_off": u32(d, 0x48),
        "instr_count": u16(d, 0x4C),
        "instr_size": u16(d, 0x4E),
        "instr": [],
    }

    # ---- instrument pointer table @0x50 (u32 entries; offset is the high word)
    total = min(song["instr_count"], 256)
    last = None
    fm_cnt = midi_cnt = valid = 0
    synth_hist = {}
    for i in range(total):
        te = 0x50 + i * 4
        if te + 4 > sz:
            total = i
            break
        w1 = u16(d, te + 2)                      # file-absolute record offset
        ins = Instr()
        ins.idx = i
        ins.offset = w1
        ok = (w1 > 0x4F) and (w1 + song["instr_size"] <= sz)
        if ok and last is not None and w1 <= last:
            ok = False
        ins.valid = ok
        ins.length = ins.synth = ins.program = ins.fm_nz = 0
        ins.fm = False
        ins.name = "(invalid)"
        if ok:
            ins.length = u16(d, w1)
            ins.synth = d[w1 + 0x24] if w1 + 0x24 < sz else 0
            ins.program = d[w1 + 0x30] if w1 + 0x30 < sz else 0
            adl = d[w1 + 0x2E:w1 + 0x2E + 11]
            ins.fm_nz = sum(1 for b in adl if b)
            ins.fm = ins.fm_nz >= 4
            ins.name = rd_str(d, w1 + 0x0A, 20)
            synth_hist[ins.synth] = synth_hist.get(ins.synth, 0) + 1
            if ins.fm:
                fm_cnt += 1
            else:
                midi_cnt += 1
            valid += 1
            last = w1
        song["instr"].append(ins)
    song["instr_total"] = total
    song["instr_valid"] = valid
    song["fm_instr"] = fm_cnt
    song["midi_instr"] = midi_cnt
    song["synth_hist"] = synth_hist

    if fm_cnt == 0 and midi_cnt == 0:
        song["kind"] = "Unknown"
    elif midi_cnt == 0:
        song["kind"] = "Adlib"
    elif fm_cnt == 0:
        song["kind"] = "MIDI"
    else:
        song["kind"] = "Mixed"

    # ---- pattern block ---------------------------------------------------
    bb = song["block_off"]
    song["track_count"] = song["row_count"] = song["order_count"] = 0
    song["tempo"] = song["speed"] = 0
    song["max_fill"] = 0
    song["fill_uncompressed"] = False
    if bb + 0x50 <= sz:
        song["track_count"] = u16(d, bb + 0x12)
        song["row_count"] = u16(d, bb + 0x14)
        song["tempo"] = u16(d, bb + 0x2A)
        song["speed"] = d[bb + 0x2C] if bb + 0x2C < sz else 0
        oc = d[bb + 0x4E] if bb + 0x4E < sz else 0
        song["order_count"] = oc

        # Max voice-cells actually filled with data across all positions.
        # For a compressed position (header bit 0x8000 set) the engine reads a
        # `bp` word = number of cells decoded that position. For an
        # uncompressed position it copies 8 bytes for every declared track, so
        # the fill equals track_count.
        max_fill = 0
        seen = set()
        for i in range(oc):
            o = bb + 0x50 + i
            pat = d[o] if o < sz else 0
            if pat in seen:
                continue
            seen.add(pat)
            poff = u32(d, bb + 0x150 + pat * 4)
            if poff == 0 or poff + 6 > sz:
                continue
            base = poff + 2
            hdr = u16(d, base)
            if hdr & 0x8000:
                fill = u16(d, base + 2)             # bp = filled cell count
            else:
                fill = song["track_count"]          # raw copy: all tracks
                song["fill_uncompressed"] = True
            if 0 < fill <= 64:                       # ignore obvious garbage
                max_fill = max(max_fill, fill)
        song["max_fill"] = max_fill
    return song


def synth_summary(hist):
    parts = []
    for code in sorted(hist):
        parts.append("%s=%d" % (SYNTH_NAMES.get(code, "0x%02X" % code),
                                 hist[code]))
    return ",".join(parts) if parts else "-"


def main():
    args = sys.argv[1:]
    verbose = False
    csv_path = None
    roots = []
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("-v", "--verbose"):
            verbose = True
        elif a == "--csv":
            i += 1
            csv_path = args[i]
        else:
            roots.append(a)
        i += 1
    if not roots:
        roots = ["."]

    songs = []
    for root in roots:
        if os.path.isfile(root):
            files = [root]
        else:
            files = []
            for dp, _, fns in os.walk(root):
                for fn in fns:
                    files.append(os.path.join(dp, fn))
        for path in files:
            try:
                with open(path, "rb") as fh:
                    d = fh.read()
            except OSError:
                continue
            s = parse(d)
            if s is None:
                continue
            s["path"] = path
            songs.append(s)

    # ---- per-file report -------------------------------------------------
    hdr = ("%-46s %5s g %-6s v/tot  syn-mix                    "
           "trk fill row  tempo/spd" %
           ("file", "size", "kind"))
    print(hdr)
    print("-" * len(hdr))
    for s in sorted(songs, key=lambda x: x["path"]):
        print("%-46s %5d %d %-6s %2d/%-2d  %-25s %3d %4d %4d  %4d/%-2d" % (
            s["path"][-46:], s["size"], s["gen"], s["kind"],
            s["instr_valid"], s["instr_total"], synth_summary(s["synth_hist"]),
            s["track_count"], s["max_fill"], s["row_count"],
            s["tempo"], s["speed"]))
        if verbose:
            for ins in s["instr"]:
                if not ins.valid:
                    continue
                print("      #%-2d off=0x%04X len=%-4d syn=0x%02X %-8s "
                      "prog=%-3d fm_nz=%-2d %s" % (
                          ins.idx, ins.offset, ins.length, ins.synth,
                          "FM" if ins.fm else "MIDI", ins.program,
                          ins.fm_nz, ins.name))

    # ---- corpus summary --------------------------------------------------
    print()
    print("=" * 72)
    print("CORPUS SUMMARY  (%d OSL1 files)" % len(songs))
    print("=" * 72)

    def dist(key):
        h = {}
        for s in songs:
            h[s[key]] = h.get(s[key], 0) + 1
        return h

    trk = dist("track_count")
    fill = dist("max_fill")
    print("\ntrack_count (declared voices == engine voice_count):")
    for k in sorted(trk):
        print("   %2d tracks : %3d files %s" %
              (k, trk[k], "  <-- >9!" if k > 9 else ""))
    print("   max track_count = %d" % (max(trk) if trk else 0))

    print("\nmax voice-cells actually FILLED per song:")
    for k in sorted(fill):
        print("   %2d filled : %3d files %s" %
              (k, fill[k], "  <-- >9!" if k > 9 else ""))
    print("   max fill = %d" % (max(fill) if fill else 0))

    over9_trk = [s for s in songs if s["track_count"] > 9]
    over9_fill = [s for s in songs if s["max_fill"] > 9]
    print("\nfiles with >9 declared tracks : %d" % len(over9_trk))
    print("files with >9 filled voices   : %d" % len(over9_fill))
    for s in over9_fill:
        print("   %-44s trk=%d fill=%d" %
              (s["path"][-44:], s["track_count"], s["max_fill"]))

    # synth-code global histogram + co-occurrence
    gh = {}
    both24 = 0
    for s in songs:
        for c, n in s["synth_hist"].items():
            gh[c] = gh.get(c, 0) + n
        if 0x02 in s["synth_hist"] and 0x04 in s["synth_hist"]:
            both24 += 1
    print("\nglobal synth-code histogram (all valid instruments):")
    for c in sorted(gh):
        print("   0x%02X %-8s : %d" % (c, SYNTH_NAMES.get(c, "?"), gh[c]))
    print("\nfiles containing BOTH 0x02 and 0x04 instruments: %d" % both24)

    if csv_path:
        with open(csv_path, "w") as fh:
            fh.write("path,size,gen,kind,instr_valid,instr_total,"
                     "fm,midi,syn02,syn04,syn08,track_count,max_fill,"
                     "row_count,tempo,speed\n")
            for s in songs:
                h = s["synth_hist"]
                fh.write("%s,%d,%d,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n" % (
                    s["path"], s["size"], s["gen"], s["kind"],
                    s["instr_valid"], s["instr_total"], s["fm_instr"],
                    s["midi_instr"], h.get(0x02, 0), h.get(0x04, 0),
                    h.get(0x08, 0), s["track_count"], s["max_fill"],
                    s["row_count"], s["tempo"], s["speed"]))
        print("\nwrote CSV -> %s" % csv_path)


if __name__ == "__main__":
    main()
