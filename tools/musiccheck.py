#!/usr/bin/env python3
"""musiccheck.py - Fifth Wheel's music in a recording, checked against the
messages the game sent (dgk's DGK_MIDI_LOG): the notes it played are the notes
in the recording.

  musiccheck.py AUDIO.WAV MIDI.LOG [--points N] [--min-match F]

The log has every message as "ms status d1 d2" (hex bytes): a note sounds from
its note-on to its note-off (channel 10, the drums, left out). The music's first
loud window in the recording is lined up with the log's first note. Then, at N
points through the music, the recording's strongest note (a semitone from E2 to
D7) must be one the log has sounding then (by its pitch class: an instrument's
octave harmonic counts), give or take 60 ms. Exit status 0 when at least
--min-match of the points agree.

86Box does not keep pace with the recorder while the guest is busy: run the
game with -nodraw, so the recording has no gaps to throw the line-up off.
"""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.join(os.environ.get("MGAHAL", ""), "tools", "loopa"))
from wavcheck import read_mono, goertzel    # noqa: E402  (RetroGPU's Loop A tools)


def notes_from_log(path):
    """(us, set of MIDI notes sounding) after each change, and the first note's time."""
    held = set()
    changes, first = [], None
    cur = frozenset()
    for line in open(path):
        w = line.split()
        if len(w) != 4:
            continue
        us, st, d1, d2 = int(w[0]) * 1000, int(w[1], 16), int(w[2], 16), int(w[3], 16)
        ch, kind = st & 15, st & 0xF0
        if ch == 9 or kind not in (0x80, 0x90, 0xB0):
            continue
        if kind == 0x90 and d2 > 0:
            held.add((ch, d1))
        elif kind in (0x80, 0x90):
            held.discard((ch, d1))
        elif d1 in (120, 123):                      # all sound off, all notes off
            held = {h for h in held if h[0] != ch}
        on = frozenset(n for c, n in held)
        if on != cur:
            if on and first is None:
                first = us
            changes.append((us, on))
            cur = on
    return changes, first


def sounding(changes, us):
    s = frozenset()
    for t, on in changes:
        if t > us:
            break
        s = on
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("log")
    ap.add_argument("--points", type=int, default=30)
    ap.add_argument("--min-match", type=float, default=0.75)
    a = ap.parse_args()
    changes, first = notes_from_log(a.log)
    if first is None:
        print("musiccheck: no note played in %s" % a.log)
        sys.exit(1)
    x, rate = read_mono(a.wav)
    win = rate // 100
    rms = [math.sqrt(sum(v * v for v in x[i:i + win]) / win) for i in range(0, len(x) - win, win)]
    # The music: what ends the longest quiet (the boot; the BIOS's beep comes before it), or the start.
    start, quiet, longest = 0, 0, 0
    for i, r in enumerate(rms):
        if r < 0.0005:
            quiet += 1
        else:
            if quiet > longest:
                start, longest = i, quiet
            quiet = 0
    if not any(r >= 0.0005 for r in rms[start:]):
        print("musiccheck: no music in %s" % a.wav)
        sys.exit(1)
    last_us = changes[-1][0]
    span = min((last_us - first) / 1e6, len(x) / rate - start / 100.0) - 0.5
    if span < 5:
        print("musiccheck: only %.1f s of music" % span)
        sys.exit(1)
    names = "c c# d d# e f f# g g# a a# b".split()
    match = 0
    for k in range(a.points):
        t = 0.5 + span * k / a.points
        s0 = int((start / 100.0 + t) * rate)
        seg = x[s0:s0 + int(0.12 * rate)]
        best = max(range(40, 87), key=lambda m: goertzel(seg, rate, 440 * 2 ** ((m - 69) / 12.0)))
        us = first + t * 1e6
        want = set()
        for d in (-60000, 0, 60000, 110000):
            want |= {m % 12 for m in sounding(changes, us + d)}
        ok = best % 12 in want
        match += ok
        print("musiccheck: %5.1f s  heard %-4s  played %-23s %s" % (
            t, names[best % 12] + str(best // 12 - 1),
            " ".join(sorted({names[m % 12] for m in sounding(changes, us)})), "ok" if ok else "MISS"))
    frac = match / float(a.points)
    print("musiccheck: %d of %d points agree (%.0f%%, need %.0f%%), over %.1f s of music"
          % (match, a.points, 100 * frac, 100 * a.min_match, span))
    sys.exit(0 if frac >= a.min_match else 1)


if __name__ == "__main__":
    main()
