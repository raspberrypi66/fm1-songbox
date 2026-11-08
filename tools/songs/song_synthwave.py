"""synthwave - midnight drive : 104 BPM, A minor, driving synth bass, warm pad, 16th arp, saw lead.

Form (bars): intro 8 | verse 8 | chorus 8 | verse 8 | chorus 8 | breakdown 8 | chorus 8 | outro 4 (held)
"""
from smflib import *

BPM = 104

VERSE = "Am | F | C | G | Am | F | C | G"
CHORUS = "F | G | Am | Am | F | G | C | E7"
CHORUS_LAST = "F | G | Am | Am | F | G | C | C"
BREAK = "Am | F | Am | F | Dm | F | E7 | E7"

LEAD_CHORUS = """
C5  -   -   -   A4  -   C5  -  |
D5  -   -   -   B4  -   D5  -  |
E5  -   -   -   -   -   D5  -  |
C5  -   -   -   A4  -   -   -  |
C5  -   -   -   A4  -   C5  -  |
D5  -   -   -   B4  -   D5  -  |
E5  -   -   -   G5  -   -   -  |
G#5 -   -   -   -   -   E5  -  """
LEAD_CHORUS_LAST_END = """
E5  -   -   -   G5  -   -   -  |
A5  -   -   -   -   -   -   -  """
LEAD_VERSE = """
E4  -   -   -   .   .   A4  -  |
C5  -   -   -   -   -   .   .  |
E4  -   -   -   .   .   G4  -  |
B4  -   -   -   -   -   .   .  |
E4  -   -   -   .   .   A4  -  |
C5  -   -   -   D5  -   .   .  |
E5  -   -   -   -   -   D5  -  |
D5  -   -   -   B4  -   -   .  """
LEAD_BREAK = """
A4  -   -   -   -   -   -   -  |
C5  -   -   -   -   -   -   -  |
E5  -   -   -   -   -   -   -  |
F5  -   -   -   E5  -   -   -  |
F5  -   -   -   -   -   -   -  |
A5  -   -   -   -   -   -   -  |
G#5 -   -   -   -   -   -   -  |
B5  -   -   -   -   -   -   .  """


def pad_chords(track, segs, lo=52, hi=67, vel=70, prev=None, gate=1.0):
    for seg in segs:
        v = voicing(seg.chord, 3, lo, hi, prev, omit_root=False)
        prev = v
        for p in v:
            track.note(seg.tick, p, vel, int(seg.ticks * gate) - 5)
    return prev


def arp_track(track, segs, vel=62):
    """Four 16ths per beat running up the triad (root position, octave on top)."""
    for seg in segs:
        ch = seg.chord
        root = ch.root_in(57)
        voices = [root, root + ch.tone('3'), root + ch.tone('5'), root + 12]
        pattern = [0, 1, 2, 3]
        track.add(arp(voices, pattern, S, seg.tick, int(seg.ticks // S), vel=vel, gate=0.8,
                      vel_fn=lambda k: 10 if k % 4 == 0 else 0))


def bass_line(track, segs, pat, vel=104):
    for seg in segs:
        root = seg.chord.root_in(33)
        bars = int(seg.ticks // (4 * Q))
        for b in range(bars):
            track.seq(pat, E, seg.tick + b * 4 * Q, vel=vel, gate=0.8, resolve=seg.chord.resolver(root),
                      vel_fn=lambda i: 8 if i % 4 == 0 else 0)


def build(path):
    s = Song('Midnight Drive', BPM)
    lead = s.track('Saw Lead', 0, 81, vol=98, pan=64, reverb=55, bends=True)
    pad = s.track('Warm Pad', 1, 89, vol=92, pan=64, reverb=70)
    arpt = s.track('Square Arp', 2, 80, vol=72, pan=92, reverb=45)
    brass = s.track('Synth Brass', 3, 62, vol=88, pan=36, reverb=40)
    bass = s.track('Synth Bass', 4, 38, vol=112, pan=64, reverb=10)
    dr = s.drums(vol=112, reverb=35)

    plan = [('intro', VERSE), ('v1', VERSE), ('c1', CHORUS), ('v2', VERSE), ('c2', CHORUS),
            ('break', BREAK), ('c3', CHORUS_LAST)]
    at, segs_of = {}, {}
    beat = 0
    for name, text in plan:
        segs = chart(text, start_beat=beat)
        at[name] = int(beat // 4)
        segs_of[name] = segs
        beat = segs[-1].beat + segs[-1].beats
    end_bar = int(beat // 4)                      # outro starts here, 4 bars held

    # ---- pad: everywhere, swells in the intro -----------------------------
    prev = None
    for name, _ in plan:
        prev = pad_chords(pad, segs_of[name], prev=prev, vel=72 if name != 'intro' else 64)
    pad.ramp(11, s.bar(0), s.bar(4), 40, 127)
    for p in (n('A3'), n('E4'), n('A4'), n('C5')):            # final chord
        pad.note(s.bar(end_bar), p, 80, s.bar_ticks * 4 - 100)
    pad.ramp(11, s.bar(end_bar + 1), s.bar(end_bar + 4) - 200, 127, 30)
    pad.cc(s.bar(end_bar + 4), 11, 127)

    # ---- arp: intro, verses, choruses, break, outro -------------------------
    for name, _ in plan:
        arp_track(arpt, segs_of[name], vel=58 if name in ('intro', 'break') else 64)
    arp_track(arpt, [Seg(end_bar * 4, 16, Chord('Am'))], vel=58)
    arpt.ramp(11, s.bar(end_bar + 1), s.bar(end_bar + 4) - 200, 127, 20)
    arpt.cc(s.bar(end_bar + 4), 11, 127)

    # ---- bass ---------------------------------------------------------------
    bass.cc(0, 11, 0)
    bass.cc(s.bar(4) - 10, 11, 127)
    bass_line(bass, segs_of['intro'][4:], "1 1 1 1 1 1 8 1", vel=92)
    for name in ('v1', 'v2'):
        bass_line(bass, segs_of[name], "1 1 1 1 1 1 8 1")
    for name in ('c1', 'c2', 'c3'):
        bass_line(bass, segs_of[name], "1 8 1 8 1 8 1 8", vel=108)
    bass_line(bass, segs_of['break'][:6], "1 - - - - - - -", vel=96)
    bass_line(bass, segs_of['break'][6:], "1 . 1 . 1 . 1 1", vel=104)
    bass.note(s.bar(end_bar), n('A1'), 104, s.bar_ticks * 4 - 100)
    bass.ramp(11, s.bar(end_bar + 1), s.bar(end_bar + 4) - 200, 127, 30)
    bass.cc(s.bar(end_bar + 4), 11, 127)

    # ---- synth brass stabs in the choruses -----------------------------------
    prev = None
    for name in ('c1', 'c2', 'c3'):
        for seg in segs_of[name]:
            v = voicing(seg.chord, 3, 60, 74, prev, omit_root=False)
            prev = v
            for p in v:
                brass.note(seg.tick, p, 92, int(Q * 0.7))
                brass.note(seg.tick + int(2.5 * Q), p, 86, int(Q * 0.5))
                if name == 'c3':
                    brass.note(seg.tick + 3 * Q + E, p, 80, int(E * 0.8))

    # ---- lead ---------------------------------------------------------------
    lead.seq(LEAD_VERSE, E, s.bar(at['v2']), vel=84, gate=0.95)
    for name in ('c1', 'c2'):
        lead.seq(LEAD_CHORUS, E, s.bar(at[name]), vel=104, gate=0.96)
        lead.scoop(s.bar(at[name] + 2), -2, 110)
        lead.scoop(s.bar(at[name] + 6) + 2 * Q, -2, 110)
    six = '|'.join(LEAD_CHORUS.split('|')[:6])
    lead.seq(six, E, s.bar(at['c3']), vel=110, gate=0.96)
    lead.seq(LEAD_CHORUS_LAST_END, E, s.bar(at['c3'] + 6), vel=112, gate=0.98)
    lead.scoop(s.bar(at['c3'] + 2), -2, 110)
    lead.scoop(s.bar(at['c3'] + 7), -2, 140)
    lead.seq(LEAD_BREAK, E, s.bar(at['break']), vel=88, gate=0.98)
    lead.ramp(11, s.bar(at['break']), s.bar(at['break'] + 8) - 240, 70, 127)
    lead.note(s.bar(end_bar), n('A5'), 108, s.bar_ticks * 2 - 30)      # held over the final chord
    lead.ramp(11, s.bar(end_bar) + s.bar_ticks, s.bar(end_bar) + s.bar_ticks * 2 - 60, 127, 40)
    lead.cc(s.bar(end_bar + 2), 11, 127)

    # ---- drums --------------------------------------------------------------
    verse = {K: "x . . . x . . .", SN: ". . x . . . x .", CLAP: ". . o . . . o .", HH: "x o x o x o x o"}
    chorus = {K: "x . . x x . . .", SN: ". . X . . . X .", CLAP: ". . x . . . x .", HH: "x o x o x o x x",
              HHO: ". . . . . . . x"}
    drum_grid(dr, s.bar(4), 4, E, {HH: "x o x o x o x o"}, vel_scale=0.8)
    drum_grid(dr, s.bar(6), 2, E, {K: "x . . . x . . ."}, vel_scale=0.9)
    for name in ('v1', 'v2'):
        drum_grid(dr, s.bar(at[name]), 8, E, verse)
    for name in ('c1', 'c2', 'c3'):
        drum_grid(dr, s.bar(at[name]), 8, E, chorus, vel_scale=1.05)
        dr.note(s.bar(at[name]), CRASH, 112, DRUM_LEN)
    # tom fills at the end of v1, v2, c2 and the break (gated-reverb style, straight 16ths)
    for b in (at['v1'] + 7, at['v2'] + 7, at['c2'] + 7, at['c1'] + 7):
        base = s.bar(b) + 2 * Q
        for k, key in enumerate([SN, SN, TOM_HM, TOM_HM, TOM_M, TOM_M, TOM_L, TOM_L]):
            dr.note(base + k * S, key, 96 + (10 if k % 2 == 0 else 0), DRUM_LEN)
    drum_grid(dr, s.bar(at['break']), 6, E, {K: "x . . . . . . ."}, vel_scale=0.8)
    base = s.bar(at['break'] + 6)
    for k in range(32):                                    # two-bar snare build
        dr.note(base + k * S, SN, 50 + k * 2, DRUM_LEN)
        if k % 4 == 0:
            dr.note(base + k * S, K, 90, DRUM_LEN)
    dr.note(s.bar(end_bar), CRASH, 118, DRUM_LEN)
    dr.note(s.bar(end_bar), K, 110, DRUM_LEN)
    dr.note(s.bar(end_bar), SN, 100, DRUM_LEN)

    s.finish(s.bar(end_bar + 4) + Q)
    return s.write(path)


if __name__ == '__main__':
    build(r'D:\ESP32S3-MIDI\songs\synthwave - midnight drive.mid')
