#!/usr/bin/env python3
"""regcmp.py - compare medplay's OPL register trace against a DOSBox DRO capture.

Usage:
    regcmp.py <reference.dro> <candidate.trace> [options]

  <reference.dro>    DOSBox DRO v2.0 capture of the original MED.EXE.
  <candidate.trace>  medplay --trace output (`RRR=VV` lines, plus `; t <ms>`
                     tick markers if the build emits them).

Options:
  --events N     how many note-on events to show side by side (default 24)
  --regs         also print a per-register write-count table
  --state        also print a full end-of-song register-state diff
  --quiet        summary + verdict only

Why not a plain textual diff?  The two streams are not expected to be
byte-identical and a line diff is useless:

  * MED.EXE's driver uploads a patch in *register-major* order (every channel's
    0x20, then every 0x40, ...) at init; medplay uploads *channel-major*
    (0x20/0x40/0x60/0x80/0xE0 for the modulator, then the carrier, then 0xC0).
    Same end state, totally different line order.
  * medplay re-uploads a patch on every note-on; the DOS driver skips registers
    whose value has not changed.
  * DRO records wall-clock delays; medplay's trace is tick-quantised.

So instead we compare *observable behaviour*: the register state each stream
converges on, and the sequence of note-on events (channel, pitch, patch,
volume) that the state machine actually produces.

Note on DRO banks: MED.EXE's ADLIB.DEV drives a single OPL2 at 0x388/0x389 and
never touches a second register bank. Captures made with DOSBox in `sbpro`
(dual-OPL2) mode nonetheless contain a short burst of writes to the second
chip that is plainly not song data. Those are reported and then ignored.
"""
import sys
import struct
from collections import OrderedDict

# ---- OPL2 layout -----------------------------------------------------------

# Operator register offsets for channels 0..8 (modulator; carrier is +3).
OP_OFF = [0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12]

# Adlib F-number table for one octave (ADLIB.DEV @0x3B5), used to turn a
# recorded (block, fnum) pair back into a semitone number.
FNUM = [0x157, 0x16C, 0x181, 0x198, 0x1B1, 0x1CB,
        0x1E6, 0x203, 0x222, 0x243, 0x266, 0x28A]

NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']


def note_of(block, fnum):
    """Nearest semitone for a (block, fnum) pair, plus the cents error."""
    best, bestd = 0, 1e9
    for i, f in enumerate(FNUM):
        d = abs(f - fnum)
        if d < bestd:
            bestd, best = d, i
    return block * 12 + best, fnum - FNUM[best]


def note_name(n):
    return '%s%d' % (NOTE_NAMES[n % 12], n // 12)


# ---- trace loading ---------------------------------------------------------

def load_dro(path):
    """-> (writes, meta). writes = [(t_ms, reg, val)] for bank 0 only."""
    d = open(path, 'rb').read()
    if d[:8] != b'DBRAWOPL':
        sys.exit('%s: not a DRO file' % path)
    vmaj, vmin = struct.unpack_from('<HH', d, 8)
    if (vmaj, vmin) != (2, 0):
        sys.exit('%s: unsupported DRO version %d.%d' % (path, vmaj, vmin))
    pairs, ms = struct.unpack_from('<II', d, 12)
    hw, fmt, comp = d[20], d[21], d[22]
    short_code, long_code = d[23], d[24]
    cmlen = d[25]
    cm = d[26:26 + cmlen]
    off = 26 + cmlen

    writes, bank1 = [], 0
    t = 0
    n = 0
    while off + 1 < len(d) and n < pairs:
        idx, val = d[off], d[off + 1]
        off += 2
        n += 1
        if idx == short_code:
            t += val + 1
            continue
        if idx == long_code:
            t += (val + 1) << 8
            continue
        if idx & 0x80:
            bank1 += 1          # second OPL2 chip: capture artefact, see docstring
            continue
        writes.append((float(t), cm[idx & 0x7f], val))
        off_reg = None
    meta = dict(kind='DRO2', pairs=pairs, ms=ms, hw=hw, fmt=fmt, comp=comp,
                bank1=bank1)
    return writes, meta


def load_trace(path):
    """-> (writes, meta) for medplay's `RRR=VV` / `; t <ms>` format."""
    writes = []
    t = 0.0
    ticks = 0
    bank1 = 0
    have_time = False
    for line in open(path):
        line = line.strip()
        if not line:
            continue
        if line.startswith(';'):
            parts = line.split()
            if len(parts) >= 3 and parts[1] == 't':
                t = float(parts[2])
                ticks += 1
                have_time = True
            continue
        if '=' not in line:
            continue
        r, v = line.split('=', 1)
        try:
            reg, val = int(r, 16), int(v, 16)
        except ValueError:
            continue        # stray header/diagnostic line, e.g. "DRO2: pairs=..."
        if reg & 0x100:
            bank1 += 1
            continue
        writes.append((t, reg & 0xFF, val))
    meta = dict(kind='trace', ticks=ticks, ms=t, bank1=bank1,
                have_time=have_time)
    return writes, meta


def load_any(path):
    if path.lower().endswith('.dro'):
        return load_dro(path)
    return load_trace(path)


# ---- behavioural extraction ------------------------------------------------

SETTLE_MS = 8.0     # a key-on's register burst is considered complete after this


def analyse(writes):
    """Replay the write stream through a register file and pull out note-ons.

    Returns (events, final_state, reg_counts).
    An event is a dict describing everything audible about one key-on.

    The patch attached to an event is snapshotted only once the burst of writes
    around the key-on has settled (SETTLE_MS), not at the instant of the 0xB0
    write. This matters: MED.EXE's driver writes the key-on *before* it writes
    the carrier's 0x40 volume, so sampling at the key-on instant catches the
    previous note's (or the initial 0x3F "silent") TL and makes every patch look
    different. medplay writes volume first, then keys on. Both converge on the
    same state a few hundred microseconds later, which is what is audible.
    """
    reg = [0] * 0x100
    counts = OrderedDict()
    b0 = {}
    events = []
    pending = []            # events whose patch has not been snapshotted yet

    def snapshot(e):
        ch = e['ch']
        m = OP_OFF[ch]
        c = m + 3
        e['patch'] = (reg[0x20 + m], reg[0x40 + m], reg[0x60 + m],
                      reg[0x80 + m], reg[0xE0 + m],
                      reg[0x20 + c], reg[0x40 + c], reg[0x60 + c],
                      reg[0x80 + c], reg[0xE0 + c],
                      # 0xC0 bits 4-5 are the OPL3-only left/right enables.
                      # Nuked-OPL3 needs them set; a real OPL2 has no such bits
                      # and MED.EXE never writes them. Mask them out so the
                      # feedback/connection nibble is what actually gets compared.
                      reg[0xC0 + ch] & 0x0F)
        e['vol'] = reg[0x40 + c] & 0x3F
        e['ksl'] = reg[0x40 + c] >> 6

    for t, r, v in writes:
        while pending and t - pending[0]['t'] > SETTLE_MS:
            snapshot(pending.pop(0))
        counts[r] = counts.get(r, 0) + 1
        reg[r] = v
        if 0xB0 <= r <= 0xB8:
            ch = r - 0xB0
            prev = b0.get(ch, 0)
            b0[ch] = v
            if (v & 0x20) and not (prev & 0x20):        # key-on rising edge
                fnum = ((v & 3) << 8) | reg[0xA0 + ch]
                block = (v >> 2) & 7
                note, cents = note_of(block, fnum)
                e = dict(t=t, ch=ch, note=note, block=block,
                         fnum=fnum, err=cents)
                events.append(e)
                pending.append(e)
    for e in pending:
        snapshot(e)
    return events, reg, counts


def patch_str(p):
    return ' '.join('%02X' % x for x in p)


# ---- report ----------------------------------------------------------------

def hdr(title):
    print()
    print('=' * 78)
    print('  ' + title)
    print('=' * 78)


def main():
    args, opts, nev = [], set(), 24
    argv = sys.argv[1:]
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '--events' and i + 1 < len(argv):
            nev = int(argv[i + 1]); i += 2; continue
        if a.startswith('--events='):
            nev = int(a.split('=', 1)[1]); i += 1; continue
        if a.startswith('--'):
            opts.add(a); i += 1; continue
        args.append(a); i += 1
    if len(args) != 2:
        sys.exit(__doc__)

    ref_path, cand_path = args
    ref_w, ref_m = load_any(ref_path)
    cnd_w, cnd_m = load_any(cand_path)

    ref_e, ref_s, ref_c = analyse(ref_w)
    cnd_e, cnd_s, cnd_c = analyse(cnd_w)

    # ---- 1. stream summary -------------------------------------------------
    hdr('1. Stream summary')
    print('  %-28s %-22s %-22s' % ('', 'REFERENCE (DOS)', 'CANDIDATE (medplay)'))
    print('  %-28s %-22s %-22s' % ('file', ref_path.split('/')[-1],
                                   cand_path.split('/')[-1]))
    print('  %-28s %-22s %-22s' % ('format', ref_m['kind'], cnd_m['kind']))
    print('  %-28s %-22s %-22s' % ('register writes (bank 0)',
                                   len(ref_w), len(cnd_w)))
    print('  %-28s %-22s %-22s' % ('bank-1 writes (ignored)',
                                   ref_m['bank1'], cnd_m['bank1']))
    print('  %-28s %-22s %-22s' % ('distinct registers',
                                   len(ref_c), len(cnd_c)))
    print('  %-28s %-22s %-22s' % ('duration (ms)',
                                   ref_m.get('ms', '?'),
                                   '%.0f' % cnd_m.get('ms', 0)))
    print('  %-28s %-22s %-22s' % ('note-on events',
                                   len(ref_e), len(cnd_e)))
    if ref_m.get('bank1'):
        print()
        print('  note: the reference capture contains %d writes to a second OPL2'
              % ref_m['bank1'])
        print('        chip (DRO hardware type %s). ADLIB.DEV is single-chip, so'
              % ref_m.get('hw', '?'))
        print('        these are a DOSBox capture artefact and are excluded.')

    # ---- 2. instrument patches --------------------------------------------
    def patchset(events):
        seen = OrderedDict()
        for e in events:
            seen.setdefault(e['patch'], []).append(e)
        return seen

    rp, cp = patchset(ref_e), patchset(cnd_e)
    hdr('2. Distinct operator patches programmed at note-on')
    print('  reference: %d distinct   candidate: %d distinct' % (len(rp), len(cp)))
    print()
    print('  %-4s %-33s %-6s %s' % ('', 'mod 20 40 60 80 E0 / car ... / C0',
                                    'uses', 'in candidate?'))
    for i, (p, evs) in enumerate(rp.items()):
        mark = 'yes' if p in cp else 'NO'
        print('  R%-3d [%s] %5d  %s' % (i, patch_str(p), len(evs), mark))
    extra = [p for p in cp if p not in rp]
    if extra:
        print()
        print('  patches in candidate but NOT in reference:')
        for i, p in enumerate(extra):
            print('  C%-3d [%s] %5d' % (i, patch_str(p), len(cp[p])))

    # Volume-insensitive comparison: 0x40 holds KSL|TL, and TL is the
    # per-note volume the tracker scales, so patches can legitimately differ
    # there while the timbre is identical.
    def strip_tl(p):
        q = list(p)
        q[1] = q[1] & 0xC0      # modulator 0x40: keep KSL, drop TL
        q[6] = q[6] & 0xC0      # carrier   0x40: keep KSL, drop TL
        return tuple(q)

    rt = set(strip_tl(p) for p in rp)
    ct = set(strip_tl(p) for p in cp)
    print()
    print('  ignoring the TL (volume) field of 0x40:')
    print('    reference timbres: %d   candidate timbres: %d   shared: %d'
          % (len(rt), len(ct), len(rt & ct)))
    if rt - ct:
        print('    only in reference: %d' % len(rt - ct))
        for p in sorted(rt - ct):
            print('      [%s]' % patch_str(p))
    if ct - rt:
        print('    only in candidate: %d' % len(ct - rt))
        for p in sorted(ct - rt):
            print('      [%s]' % patch_str(p))

    # ---- 3. note-on timeline ----------------------------------------------
    hdr('3. Note-on timeline (first %d events)' % nev)
    print('  %-30s | %-30s' % ('REFERENCE (DOS)', 'CANDIDATE (medplay)'))
    print('  %-30s | %-30s' % ('t(ms)  ch note      fnum/blk',
                               't(ms)  ch note      fnum/blk'))
    print('  ' + '-' * 30 + '-+-' + '-' * 30)

    def fmt(e):
        if e is None:
            return ' ' * 30
        return '%6.0f %2d %-4s %3d  %04X/%d' % (
            e['t'], e['ch'], note_name(e['note']), e['note'],
            e['fnum'], e['block'])

    for i in range(min(nev, max(len(ref_e), len(cnd_e)))):
        a = ref_e[i] if i < len(ref_e) else None
        b = cnd_e[i] if i < len(cnd_e) else None
        same = (a and b and a['ch'] == b['ch'] and a['note'] == b['note'])
        print('  %-30s | %-30s %s' % (fmt(a), fmt(b), '' if same else '<-- differs'))

    # ---- 4. pitch agreement ------------------------------------------------
    # Positional (i-th vs i-th) alignment is useless here: a single spurious or
    # missing event shifts every later comparison. Instead match each reference
    # note-on to the nearest unclaimed candidate note-on within a time window
    # (well under one row), which is what "the same note happened at the same
    # musical moment" actually means.
    WIN = 40.0      # ms; rows here are 120 ms apart, ticks 20 ms

    def match(require_ch):
        used = [False] * len(cnd_e)
        pairs, unmatched = [], []
        j0 = 0
        for a in ref_e:
            best, bestd = -1, WIN + 1
            j = j0
            while j < len(cnd_e) and cnd_e[j]['t'] <= a['t'] + WIN:
                b = cnd_e[j]
                if not used[j] and b['note'] == a['note'] and \
                        (not require_ch or b['ch'] == a['ch']):
                    d = abs(b['t'] - a['t'])
                    if d < bestd:
                        bestd, best = d, j
                j += 1
            while j0 < len(cnd_e) and cnd_e[j0]['t'] < a['t'] - WIN:
                j0 += 1
            if best >= 0:
                used[best] = True
                pairs.append((a, cnd_e[best]))
            else:
                unmatched.append(a)
        spurious = [cnd_e[j] for j in range(len(cnd_e)) if not used[j]]
        return pairs, unmatched, spurious

    hdr('4. Note-event agreement (time-window matched, +/-%dms)' % WIN)
    p_np, miss_np, spur_np = match(False)
    p_ch, miss_ch, spur_ch = match(True)
    nref, ncnd = len(ref_e), len(cnd_e)
    if nref == 0:
        print('  no reference note-on events to compare')
    else:
        print('  matching on (time, note):')
        print('    matched            : %d/%d reference events (%.1f%%)'
              % (len(p_np), nref, 100.0 * len(p_np) / nref))
        print('    missing in medplay : %d' % len(miss_np))
        print('    extra in medplay   : %d' % len(spur_np))
        print('  matching on (time, note, channel):')
        print('    matched            : %d/%d reference events (%.1f%%)'
              % (len(p_ch), nref, 100.0 * len(p_ch) / nref))
        if p_np:
            dts = sorted(b['t'] - a['t'] for a, b in p_np)
            print('  timing offset of matched events (candidate - reference):')
            print('    median %+.1f ms, min %+.1f ms, max %+.1f ms'
                  % (dts[len(dts) // 2], dts[0], dts[-1]))

        # Per-channel breakdown: isolates "one voice is wrong" from
        # "everything is wrong", and exposes constant octave offsets.
        print()
        print('  per-channel reference note-ons and how they fared:')
        print('    %-4s %6s %8s %9s  %s'
              % ('ch', 'ref', 'matched', 'missing', 'note range (ref -> cand)'))
        for ch in range(9):
            rl = [e for e in ref_e if e['ch'] == ch]
            if not rl:
                continue
            mm = [a for a, b in p_np if a['ch'] == ch]
            ms_ = [a for a in miss_np if a['ch'] == ch]
            cl = [e for e in cnd_e if e['ch'] == ch]
            rr = '%d..%d' % (min(e['note'] for e in rl),
                             max(e['note'] for e in rl))
            cr = ('%d..%d' % (min(e['note'] for e in cl),
                              max(e['note'] for e in cl))) if cl else '-'
            # constant semitone offset, if any, between the two channels' notes
            hint = ''
            if cl and len(set(e['note'] for e in rl)) and ms_:
                d = min(e['note'] for e in cl) - min(e['note'] for e in rl)
                if d:
                    hint = '   (offset %+d semitones)' % d
            print('    %-4d %6d %8d %9d  %s -> %s%s'
                  % (ch, len(rl), len(mm), len(ms_), rr, cr, hint))

    # ---- 5. per-register write counts -------------------------------------
    if '--regs' in opts:
        hdr('5. Per-register write counts')
        allr = sorted(set(ref_c) | set(cnd_c))
        print('  %-6s %8s %8s   %s' % ('reg', 'ref', 'cand', 'delta'))
        for r in allr:
            a, b = ref_c.get(r, 0), cnd_c.get(r, 0)
            flag = '' if a == b else ('  <--' if (a == 0 or b == 0) else '')
            print('  0x%02X   %8d %8d   %+d%s' % (r, a, b, b - a, flag))

    # ---- 6. final register state ------------------------------------------
    if '--state' in opts:
        hdr('6. Final register state differences')
        touched = sorted(set(ref_c) | set(cnd_c))
        diffs = [r for r in touched if ref_s[r] != cnd_s[r]]
        print('  %d of %d touched registers differ at end of song'
              % (len(diffs), len(touched)))
        for r in diffs:
            print('    0x%02X  ref=%02X  cand=%02X' % (r, ref_s[r], cnd_s[r]))

    # ---- verdict -----------------------------------------------------------
    hdr('Verdict')
    if nref:
        print('  note-on count      : ref %d vs cand %d  (%+d)'
              % (nref, ncnd, ncnd - nref))
        print('  note+time match    : %.1f%%  (%d missing, %d extra)'
              % (100.0 * len(p_np) / nref, len(miss_np), len(spur_np)))
        print('  note+time+channel  : %.1f%%' % (100.0 * len(p_ch) / nref))
        print('  timbre coverage    : %d/%d reference timbres reproduced'
              % (len(rt & ct), len(rt)))
    print()


if __name__ == '__main__':
    main()
