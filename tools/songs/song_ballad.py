"""ballad - paper lanterns : pop ballad, 74 BPM, D major. Piano arpeggios with pedal, oboe verse
melody, French horn chorus melody, strings, finger bass, soft drums, ritardando ending.

Form (bars): intro 4 | verse 8 | chorus 8 | verse 8 | chorus 8 | bridge 4 | chorus 8 | outro 3 + hold
"""
from smflib import *

BPM = 74
INTRO = "D | A/C# | Bm | G"
VERSE = "D | A/C# | Bm | G | D | A/C# | G | A"
CHORUS = "G | A | D | Bm | G | A | Bm | A"
CHORUS_LAST = "G | A | D | Bm | G | A | D | D"
BRIDGE = "Em | Bm | G | A"
OUTRO = "D | A/C# | G"

MEL_VERSE = """
.   .   D5  -   C#5 -   A4  -  |
B4  -   -   -   -   -   .   .  |
.   .   B4  -   A4  -   F#4 -  |
G4  -   -   -   -   -   .   .  |
.   .   D5  -   E5  -   F#5 -  |
E5  -   -   -   -   -   C#5 -  |
D5  -   -   -   B4  -   -   -  |
A4  -   -   -   -   -   .   .  """
MEL_CHORUS = """
B4  -   D5  -   G5  -   -   -  |
F#5 -   -   -   E5  -   -   -  |
D5  -   -   -   -   -   -   .  |
.   .   D5  -   E5  -   F#5 -  |
G5  -   -   -   -   -   E5  -  |
F#5 -   -   -   E5  -   -   -  |
D5  -   -   -   -   -   E5  -  |
C#5 -   -   -   -   -   .   .  """
MEL_CHORUS_LAST_END = """
E5  -   -   -   D5  -   -   -  |
D5  -   -   -   -   -   -   -  """
MEL_BRIDGE = """
G4  -   -   -   B4  -   -   -  |
D5  -   -   -   -   -   -   -  |
B4  -   -   -   D5  -   -   -  |
E5  -   -   -   -   -   -   .  """


def arp_voices(ch):
    """Four voices for a ballad arpeggio: bass note, then chord tones above it."""
    b = ch.bass_in(43)
    tones = sorted((ch.root + iv) % 12 for iv in ch.ivs)
    out = [b]
    p = b + 1
    while len(out) < 4:
        if p % 12 in tones and p % 12 != b % 12 or (p % 12 == b % 12 and len(out) >= 2):
            out.append(p)
        p += 1
    return out


def piano_arp(track, segs, vel=70):
    for seg in segs:
        v = arp_voices(seg.chord)
        bars = int(seg.ticks // (4 * Q))
        for b in range(bars):
            base = seg.tick + b * 4 * Q
            track.add(arp(v, [0, 1, 2, 3, 2, 1, 2, 3], E, base, 8, vel=vel, gate=1.1,
                          vel_fn=lambda k: 8 if k == 0 else -6 if k % 2 else 0))
            track.cc(base, 64, 127)
            track.cc(base + 4 * Q - 20, 64, 0)


def piano_chorus(track, segs, prev=None, vel=78):
    """Left hand root + fifth, right-hand pulse chords on 1, 2-and, 3, 4-and."""
    for seg in segs:
        v = voicing(seg.chord, 3, 57, 71, prev, omit_root=False)
        prev = v
        ch = seg.chord
        b = ch.bass_in(38)
        track.note(seg.tick, b, vel + 10, seg.ticks - 20)
        track.note(seg.tick + 2 * Q, b + 7 if b + 7 < 50 else b - 5, vel, 2 * Q - 20)
        for slot, vv in ((0, vel), (3, vel - 8), (4, vel - 4), (7, vel - 8)):
            for p in v:
                track.note(seg.tick + slot * E, p, vv, int(E * 1.6) if slot in (0, 4) else int(E * 0.9))
        track.cc(seg.tick, 64, 127)
        track.cc(seg.tick + seg.ticks - 20, 64, 0)
    return prev


def strings(track, segs, prev=None, vel=70, lo=57, hi=74):
    for seg in segs:
        v = voicing(seg.chord, 3, lo, hi, prev, omit_root=False)
        prev = v
        for p in v:
            track.note(seg.tick, p, vel, seg.ticks - 15)
    return prev


def bass(track, segs, pat, vel=96):
    for seg in segs:
        root = seg.chord.bass_in(31)
        track.seq(pat, E, seg.tick, vel=vel, gate=0.95, resolve=seg.chord.resolver(root))


def build(path):
    s = Song('Paper Lanterns', BPM)
    ob = s.track('Oboe', 0, 68, vol=96, pan=56, reverb=62)
    hn = s.track('French Horn', 1, 60, vol=102, pan=70, reverb=64)
    pno = s.track('Piano', 2, 0, vol=100, pan=50, reverb=48)
    strs = s.track('Strings', 3, 48, vol=84, pan=78, reverb=76)
    bs = s.track('Bass', 4, 33, vol=106, pan=64, reverb=10)
    dr = s.drums(vol=92, reverb=36)

    plan = [('intro', INTRO), ('v1', VERSE), ('c1', CHORUS), ('v2', VERSE), ('c2', CHORUS),
            ('bridge', BRIDGE), ('c3', CHORUS_LAST), ('outro', OUTRO)]
    at, segs_of = {}, {}
    beat = 0
    for name, text in plan:
        segs = chart(text, start_beat=beat)
        at[name] = int(beat // 4)
        segs_of[name] = segs
        beat = segs[-1].beat + segs[-1].beats
    end_bar = int(beat // 4)
    final = s.bar(end_bar)

    # ---- piano ----------------------------------------------------------------
    piano_arp(pno, segs_of['intro'], vel=64)
    piano_arp(pno, segs_of['v1'], vel=68)
    piano_arp(pno, segs_of['v2'], vel=72)
    piano_arp(pno, segs_of['bridge'], vel=70)
    piano_arp(pno, segs_of['outro'], vel=64)
    prev = None
    for name in ('c1', 'c2', 'c3'):
        prev = piano_chorus(pno, segs_of[name], prev, vel=80 if name == 'c3' else 76)
    for k, p in enumerate([n('D2'), n('A2'), n('D3'), n('F#3'), n('A3'), n('D4'), n('F#4')]):
        pno.note(final + k * 45, p, 76 - k * 2, s.bar_ticks + 2 * Q - 300 - k * 45)
    pno.cc(final, 64, 127)
    pno.cc(final + s.bar_ticks + 2 * Q - 200, 64, 0)

    # ---- strings: from verse 2 on, with slow swells ------------------------------
    prev = None
    for name in ('v2', 'c2', 'bridge', 'c3', 'outro'):
        prev = strings(strs, segs_of[name], prev, vel=66 if name in ('v2', 'outro') else 74,
                       lo=62 if name == 'c3' else 57, hi=79 if name == 'c3' else 74)
    strs.ramp(11, s.bar(at['v2']), s.bar(at['v2'] + 4), 60, 110)
    strs.ramp(11, s.bar(at['v2'] + 6), s.bar(at['c2']), 110, 127)
    strs.ramp(11, s.bar(at['bridge']), s.bar(at['bridge'] + 3), 90, 127)
    for p in (n('A4'), n('D5'), n('F#5')):
        strs.note(final, p, 72, s.bar_ticks + 2 * Q - 200)
    strs.ramp(11, final + 2 * Q, final + s.bar_ticks + 2 * Q - 240, 127, 50)
    strs.cc(final + s.bar_ticks + 2 * Q, 11, 127)
    # chorus 1: strings only on the second half (first entry), quietly
    strings(strs, segs_of['c1'][4:], None, vel=62)

    # ---- bass ---------------------------------------------------------------------
    bass(bs, segs_of['v1'][4:], "1 - - - - - - .", vel=84)
    for name in ('v2', 'bridge'):
        bass(bs, segs_of[name], "1 - - - - - - .", vel=92)
    for name in ('c1', 'c2', 'c3'):
        bass(bs, segs_of[name], "1 - - . 1 - 5 -", vel=100)
    bass(bs, segs_of['outro'], "1 - - - - - - -", vel=86)
    bs.note(final, n('D2'), 92, s.bar_ticks + 2 * Q - 200)

    # ---- drums ----------------------------------------------------------------------
    light = {K: "x . . . . x . .", RIM: ". . x . . . x .", HH: "o m o m o m o m"}
    full = {K: "x . . . . x . .", SN: ". . x . . . x .", HH: "x o x o x o x o"}
    drum_grid(dr, s.bar(at['v2']), 8, E, light, vel_scale=0.8)
    drum_grid(dr, s.bar(at['bridge']), 4, E, light, vel_scale=0.85)
    drum_grid(dr, s.bar(at['c1']), 8, E, full, vel_scale=0.8)
    for name in ('c2', 'c3'):
        drum_grid(dr, s.bar(at[name]), 8, E, full, vel_scale=0.95)
    drum_grid(dr, s.bar(at['c3']), 8, E, {HHO: ". . . . . . . x"}, vel_scale=0.6)
    for name in ('c1', 'c2', 'c3'):
        dr.note(s.bar(at[name]), CRASH, 90, DRUM_LEN)
        base = s.bar(at[name]) - Q
        for k, v in enumerate((60, 70, 84, 100)):
            dr.note(base + k * S, SN, v, DRUM_LEN)
    dr.note(s.bar(at['outro']), CRASH, 70, DRUM_LEN)
    dr.note(final, CRASH, 76, DRUM_LEN)
    dr.note(final, K, 80, DRUM_LEN)

    # ---- melodies -------------------------------------------------------------------
    ob.seq(MEL_VERSE, E, s.bar(at['v1']), vel=92, gate=0.95)
    ob.seq(MEL_VERSE, E, s.bar(at['v2']), vel=96, gate=0.95)
    ob.seq(MEL_BRIDGE, E, s.bar(at['bridge']), vel=94, gate=0.97)
    for name in ('c1', 'c2'):
        hn.seq(MEL_CHORUS, E, s.bar(at[name]), vel=100 if name == 'c1' else 104, gate=0.96)
    six = '|'.join(MEL_CHORUS.split('|')[:6])
    hn.seq(six, E, s.bar(at['c3']), vel=108, gate=0.96)
    hn.seq(MEL_CHORUS_LAST_END, E, s.bar(at['c3'] + 6), vel=106, gate=0.98)
    ob.seq(six, E, s.bar(at['c3']), vel=84, gate=0.95)                       # oboe doubles the last chorus
    ob.seq(MEL_CHORUS_LAST_END, E, s.bar(at['c3'] + 6), vel=82, gate=0.98)
    ob.seq("F#4 - - - A4 - - - | G4 - - - E4 - - - | D4 - - - - - - .", E, s.bar(at['outro']), vel=80, gate=0.97)
    hn.note(final, n('A4'), 84, s.bar_ticks + 2 * Q - 300)
    ob.note(final, n('F#5'), 78, s.bar_ticks + 2 * Q - 300)
    for tr in (hn, ob):
        tr.ramp(11, final + 2 * Q, final + s.bar_ticks + 2 * Q - 320, 127, 60)
        tr.cc(final + s.bar_ticks + 2 * Q, 11, 127)

    # ritardando through the outro into the final chord
    s.rit(s.bar(at['outro'] + 1), final, BPM, 52, steps=10)
    s.tempo(final + 1, 50)
    s.finish(final + s.bar_ticks + 2 * Q + Q)
    return s.write(path)


if __name__ == '__main__':
    build(r'D:\ESP32S3-MIDI\songs\ballad - paper lanterns.mid')
