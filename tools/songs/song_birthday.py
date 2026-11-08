"""Happy Birthday : traditional melody (public domain), original arrangement. F major, 3/4, 104 BPM.

Form (bars): intro 4 (piano waltz + celesta, pickup in bar 4) | verse 1 8 (piano melody, bass)
| interlude 4 (celesta figure, strings and drums enter) | verse 2 8 (celesta + piano melody an
octave up, strings, light drums, last line lifts to G major) | tag 2 (last line again, ritardando)
| final G chord 2 bars.
"""
from smflib import *

BPM = 104
INTRO = "F | Dm7 | Gm7 | C7"
VERSE1 = "F | C7 | C7 | F | F:2 F7:1 | Bb | F:2 C7:1 | F"
INTERLUDE = "F | Bb | Gm7 | C7"
VERSE2 = "F | C7 | C7 | F | F:2 F7:1 | Bb:2 D7:1 | G:2 D7:1 | G"
TAG = "G:2 D7:1 | G"

# 16th grid, 12 slots per 3/4 bar. The pickup ("hap-py") is a dotted eighth + sixteenth on beat 3.
PICKUP = ". . . . . . . . C4 - - C4"
MELODY = """
D4 - - - C4 - - - F4 - - -  |
E4 - - - - - - - C4 - - C4  |
D4 - - - C4 - - - G4 - - -  |
F4 - - - - - - - C4 - - C4  |
C5 - - - A4 - - - F4 - - -  |
E4 - - - D4 - - - Bb4 - - Bb4 |
A4 - - - F4 - - - G4 - - -  |
F4 - - - - - - - . . . .    """
MELODY_LIFT = '|'.join(MELODY.split('|')[:5]) + """|
E4 - - - D4 - - - C5 - - C5 |
B4 - - - G4 - - - A4 - - -  |
G4 - - - - - - - C5 - - C5  """
TAG_MEL = """
B4 - - - G4 - - - A4 - - -  |
G4 - - - - - - - - - - -    """
CELESTA_INTERLUDE = """
A4 . C5 . F5 . C5 . A4 . C5 . |
Bb4 . D5 . F5 . D5 . Bb4 . D5 . |
G4 . Bb4 . D5 . Bb4 . G4 . Bb4 . |
E4 . G4 . Bb4 . C5 . E5 - - -  """


def waltz(track, segs, prev=None, vel=70, lo=45, hi=59):
    """Piano: bass note on 1, chord on 2 and 3."""
    for seg in segs:
        v = voicing(seg.chord, 3, lo, hi, prev, omit_root=False)
        prev = v
        b = seg.chord.bass_in(36)
        track.note(seg.tick, b, vel + 8, int(seg.ticks * 0.95))
        beats = int(round(seg.beats))
        for k in range(1, beats):
            for p in v:
                track.note(seg.tick + k * Q, p, vel - (4 if k == 2 else 0), int(Q * 0.7))
    return prev


def strings(track, segs, prev=None, vel=64):
    for seg in segs:
        v = voicing(seg.chord, 3, 57, 72, prev, omit_root=False)
        prev = v
        for p in v:
            track.note(seg.tick, p, vel, seg.ticks - 12)
    return prev


def bass(track, segs, vel=92):
    for seg in segs:
        root = seg.chord.bass_in(31)
        beats = int(round(seg.beats))
        track.note(seg.tick, root, vel, int(Q * (2 if beats == 3 else beats) * 0.9))
        if beats == 3:
            fifth = fold(root + seg.chord.tone('5') - 12, 29, 43)
            track.note(seg.tick + 2 * Q, fifth, vel - 12, int(Q * 0.85))


def build(path):
    s = Song('Happy Birthday', BPM, 3, 4)
    pno = s.track('Piano', 0, 0, vol=104, pan=54, reverb=46)
    cel = s.track('Celesta', 1, 8, vol=92, pan=76, reverb=62)
    strs = s.track('Strings', 2, 48, vol=82, pan=40, reverb=74)
    bs = s.track('Bass', 3, 32, vol=104, pan=64, reverb=12)
    dr = s.drums(vol=84, reverb=36)

    plan = [('intro', INTRO), ('v1', VERSE1), ('inter', INTERLUDE), ('v2', VERSE2), ('tag', TAG)]
    at, segs_of = {}, {}
    beat = 0
    for name, text in plan:
        segs = chart(text, bar_beats=3, start_beat=beat)
        at[name] = int(beat // 3)
        segs_of[name] = segs
        beat = segs[-1].beat + segs[-1].beats
    end_bar = int(beat // 3)                       # final chord bar
    final = s.bar(end_bar)
    B = s.bar_ticks

    # ---- piano: waltz accompaniment everywhere, melody in verse 1 (and an octave up in verse 2)
    prev = None
    for name, _ in plan:
        prev = waltz(pno, segs_of[name], prev, vel=66 if name in ('intro', 'v1') else 72)
    pno.seq(PICKUP, S, s.bar(3), vel=92, gate=0.9)
    pno.seq(MELODY, S, s.bar(at['v1']), vel=94, gate=0.92)
    pno.seq(PICKUP, S, s.bar(at['inter'] + 3), vel=92, gate=0.9, transpose=12)
    pno.seq(MELODY_LIFT, S, s.bar(at['v2']), vel=90, gate=0.92, transpose=12)
    pno.seq(TAG_MEL, S, s.bar(at['tag']), vel=92, gate=0.92, transpose=12)
    for k, p in enumerate([n('G2'), n('D3'), n('G3'), n('B3'), n('D4'), n('G4')]):
        pno.note(final + k * 40, p, 78 - k, 2 * B - 200 - k * 40)
    pno.cc(final, 64, 127)
    pno.cc(final + 2 * B - 150, 64, 0)

    # ---- celesta: intro sparkle, interlude figure, melody in verse 2, final flourish -------
    cel.seq("F5 . A5 . C6 . A5 . F5 . A5 . | D5 . F5 . A5 . F5 . D5 . F5 . | "
            "D5 . G5 . Bb5 . G5 . D5 . G5 . | E5 . G5 . Bb5 . C6 - - - . .", S, s.bar(0), vel=64, gate=0.9)
    cel.seq(CELESTA_INTERLUDE, S, s.bar(at['inter']), vel=72, gate=0.9)
    cel.seq(PICKUP, S, s.bar(at['inter'] + 3), vel=98, gate=0.9, transpose=12)
    cel.seq(MELODY_LIFT, S, s.bar(at['v2']), vel=100, gate=0.92, transpose=12)
    cel.seq(TAG_MEL, S, s.bar(at['tag']), vel=100, gate=0.92, transpose=12)
    flourish = [n('G5'), n('B5'), n('D6'), n('G6'), n('B6'), n('D7'), n('G7')]
    for k, p in enumerate(flourish):                 # short run up, only the top note rings
        cel.note(final + Q + k * S, p, 76, S - 10 if k < len(flourish) - 1 else 2 * B - Q - k * S - 100)

    # ---- strings: from the interlude's third bar, swell into verse 2 --------------------------
    prev = strings(strs, segs_of['inter'][2:], None, vel=58)
    prev = strings(strs, segs_of['v2'], prev, vel=68)
    prev = strings(strs, segs_of['tag'], prev, vel=72)
    strs.ramp(11, s.bar(at['inter'] + 2), s.bar(at['v2']), 50, 120)
    strs.cc(s.bar(at['v2'] + 4), 11, 127)
    for p in (n('G4'), n('B4'), n('D5')):
        strs.note(final, p, 74, 2 * B - 120)
    strs.ramp(11, final + B, final + 2 * B - 160, 127, 60)
    strs.cc(final + 2 * B, 11, 127)

    # ---- bass: verse 1 on, waltz root / fifth ---------------------------------------------
    for name in ('v1', 'inter', 'v2', 'tag'):
        bass(bs, segs_of[name], vel=88 if name == 'v1' else 94)
    bs.note(final, n('G2'), 92, 2 * B - 120)

    # ---- light drums: brushes feel, kick on 1, hats on 2 and 3, rim on 3 ------------------------
    pat = {K: "o . . . . .", HH: ". . o . o .", RIM: ". . . . m ."}
    drum_grid(dr, s.bar(at['inter'] + 2), 2, E, pat, vel_scale=0.7)
    drum_grid(dr, s.bar(at['v2']), 8, E, pat, vel_scale=0.85)
    drum_grid(dr, s.bar(at['tag']), 2, E, {K: "o . . . . .", HH: ". . o . o ."}, vel_scale=0.8)
    dr.note(s.bar(at['v2']), CRASH, 62, DRUM_LEN)
    dr.note(s.bar(at['v2'] + 6), TAMB, 70, DRUM_LEN)
    dr.note(final, CRASH, 72, DRUM_LEN)
    dr.note(final, K, 84, DRUM_LEN)

    # ritardando over the tag into the final chord
    s.rit(s.bar(at['tag']), final, BPM, 66, steps=8)
    s.tempo(final + 1, 60)
    s.finish(final + 2 * B + Q)
    return s.write(path)


if __name__ == '__main__':
    build(r'D:\ESP32S3-MIDI\songs\Happy Birthday.mid')
