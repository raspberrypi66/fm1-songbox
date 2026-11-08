"""baroque - passacaglia : G minor, 3/4, 80 BPM. An 8-bar chromatic lament ground
(G F# F E Eb D C D) with seven statements of growing figuration, then a Picardy cadence.

Statements:
 1 ground alone (cello + harpsichord)      5 violin I 16ths, violin II 8ths, viola quarters
 2 violin I theme                           6 climax: theme an octave up, 16ths below, full continuo
 3 + violin II counter-line                 7 theme returns quietly, ritardando, G major chord
 4 violin I 8th figuration, viola enters
"""
from smflib import *

BPM = 80
GROUND = "Gm | D/F# | Gm7/F | C/E | Cm/Eb | Bb/D | Cm | D7"
GROUND_BASS = "G2 - - - - - | F#2 - - - - - | F2 - - - - - | E2 - - - - - | Eb2 - - - - - | D2 - - - - - | C2 - - - - - | D2 - - - - -"
GROUND_BASS_MOVING = "G2 - - - D3 Bb2 | F#2 - - - A2 D3 | F2 - - - D3 Bb2 | E2 - - - G2 C3 | Eb2 - - - G2 C3 | D2 - - - F2 Bb2 | C2 - - - Eb2 G2 | D2 - - - C3 A2"

THEME = """
G4  -   -   -   Bb4 -  |
A4  -   -   -   C5  -  |
Bb4 -   -   -   D5  -  |
C5  -   -   -   E5  -  |
Eb5 -   -   -   C5  -  |
D5  -   -   -   Bb4 -  |
C5  -   -   -   Eb5 -  |
D5  -   -   -   -   -  """
COUNTER = """
Bb3 -   -   -   G4  -  |
F#4 -   -   -   A4  -  |
G4  -   -   -   Bb4 -  |
G4  -   -   -   C5  -  |
C5  -   -   -   G4  -  |
Bb4 -   -   -   F4  -  |
G4  -   -   -   C5  -  |
A4  -   -   -   F#4 -  """
FIG8 = """
G4  Bb4 D5  Bb4 A4  Bb4 |
A4  C5  D5  C5  A4  F#4 |
Bb4 D5  G5  D5  Bb4 D5  |
C5  E5  G5  E5  C5  G4  |
Eb5 C5  G4  C5  Eb5 D5  |
D5  Bb4 F4  Bb4 D5  F5  |
C5  Eb5 G5  Eb5 C5  Eb5 |
D5  F#5 A5  F#5 D5  C5  """
FIG16 = """
G5  F#5 G5  A5  Bb5 A5  G5  F#5 G5  D5  Bb4 D5  |
A5  G5  A5  B5  C6  B5  A5  G5  A5  F#5 D5  F#5 |
Bb5 A5  Bb5 C6  D6  C6  Bb5 A5  Bb5 G5  D5  G5  |
C6  B5  C6  D6  E6  D6  C6  B5  C6  G5  E5  G5  |
Eb6 D6  C6  Bb5 Ab5 G5  Eb5 G5  C6  G5  Eb5 C5  |
D6  C6  Bb5 A5  G5  F5  D5  F5  Bb5 F5  D5  Bb4 |
C5  Eb5 G5  C6  Eb6 C6  G5  Eb5 C5  Eb5 G5  C6  |
D5  F#5 A5  C6  D6  C6  A5  F#5 D5  C5  A4  C5  """
VIOLA_Q = "D4 Bb3 D4 | D4 A3 D4 | D4 Bb3 D4 | E4 C4 E4 | Eb4 C4 Eb4 | F4 D4 F4 | Eb4 C4 G4 | D4 A3 F#4"
VIOLA_LONG = "D4 - - - - - | D4 - - - - - | D4 - - - - - | E4 - - - - - | Eb4 - - - - - | F4 - - - - - | Eb4 - - - - - | D4 - - - - -"


def continuo(track, segs, start, style, prev=None, vel=70):
    """Harpsichord realisation: 'block' = chord on each beat, 'arp' = 8th-note arpeggios,
    'long' = one chord per bar."""
    for k, seg in enumerate(segs):
        v = voicing(seg.chord, 3, 55, 76, prev, omit_root=False)
        prev = v
        base = start + k * 3 * Q
        if style == 'block':
            for b in range(3):
                for p in v:
                    track.note(base + b * Q, p, vel - (0 if b == 0 else 10), int(Q * 0.85))
        elif style == 'long':
            for p in v:
                track.note(base, p, vel, int(3 * Q * 0.95))
        else:
            order = [0, 1, 2, 1, 2, 1] if k % 2 == 0 else [2, 1, 0, 1, 0, 1]
            for i, idx in enumerate(order):
                track.note(base + i * E, v[idx], vel - (0 if i % 2 == 0 else 8), int(E * 0.95))
    return prev


def build(path):
    s = Song('Passacaglia', BPM, 3, 4)
    v1 = s.track('Violin I', 0, 40, vol=100, pan=48, reverb=72)
    v2 = s.track('Violin II', 1, 40, vol=90, pan=76, reverb=72)
    va = s.track('Viola', 2, 41, vol=88, pan=88, reverb=70)
    vc = s.track('Cello', 3, 42, vol=104, pan=56, reverb=66)
    hp = s.track('Harpsichord', 4, 6, vol=72, pan=36, reverb=60)
    segs = chart(GROUND, bar_beats=3)
    L = 8 * 3 * Q                                        # one statement in ticks
    st = [k * L for k in range(7)]                       # statement start ticks

    # cello ground: plain in 1-4 and 7, moving in 5-6
    for k in range(7):
        pat = GROUND_BASS_MOVING if k in (4, 5) else GROUND_BASS
        vc.seq(pat, E, st[k], vel=92 if k in (4, 5) else 84, gate=0.95 if k in (4, 5) else 0.92)

    # harpsichord
    prev = None
    styles = ['block', 'long', 'long', 'block', 'arp', 'arp', 'long']
    for k in range(7):
        prev = continuo(hp, segs, st[k], styles[k], prev, vel=74 if k == 5 else 66)

    # strings
    v1.seq(THEME, E, st[1], vel=86, gate=0.97)
    v1.seq(THEME, E, st[2], vel=90, gate=0.97)
    v2.seq(COUNTER, E, st[2], vel=78, gate=0.97)
    v1.seq(FIG8, E, st[3], vel=92, gate=0.9)
    v2.seq(COUNTER, E, st[3], vel=78, gate=0.97)
    va.seq(VIOLA_LONG, E, st[3], vel=72, gate=0.97)
    v1.seq(FIG16, S, st[4], vel=96, gate=0.85, vel_fn=lambda i: 6 if i % 4 == 0 else 0)
    v2.seq(FIG8, E, st[4], vel=80, gate=0.9, transpose=-12)
    va.seq(VIOLA_Q, Q, st[4], vel=74, gate=0.9)
    v1.seq(THEME, E, st[5], vel=104, gate=0.98, transpose=12)
    v2.seq(FIG16, S, st[5], vel=84, gate=0.85, transpose=-12, vel_fn=lambda i: 6 if i % 4 == 0 else 0)
    va.seq(COUNTER, E, st[5], vel=78, gate=0.97, transpose=-12)
    v1.seq(THEME, E, st[6], vel=82, gate=0.97)
    v2.seq(COUNTER, E, st[6], vel=72, gate=0.97)
    va.seq(VIOLA_LONG, E, st[6], vel=66, gate=0.97)
    # dynamics: swell into statement 6, fade through 7
    v1.ramp(11, st[4], st[5], 108, 127)
    v1.ramp(11, st[6], st[6] + L - 3 * Q, 127, 96)
    v2.ramp(11, st[6], st[6] + L - 3 * Q, 127, 96)

    # final cadence: two bars of G major (Picardy third), ritardando over the last 4 bars
    fin = st[6] + L
    hold = 2 * 3 * Q
    for tr, p, v in ((v1, n('G5'), 92), (v2, n('D5'), 84), (va, n('B4'), 80), (vc, n('G2'), 92)):
        tr.note(fin, p, v, hold - 60)
    for k, p in enumerate([n('G3'), n('D4'), n('G4'), n('B4'), n('D5')]):
        hp.note(fin + k * 40, p, 72, hold - 200 - k * 40)
    hp.note(fin + Q, n('G5'), 64, hold - Q - 200)
    s.rit(fin - 4 * 3 * Q, fin, BPM, 58, steps=12)
    s.tempo(fin + 1, 56)
    for tr in (v1, v2, va):
        tr.ramp(11, fin, fin + hold - 200, 110, 70)
        tr.cc(fin + hold, 11, 127)
    v1.cc(fin + hold, 11, 127)
    s.finish(fin + hold + Q)
    return s.write(path)


if __name__ == '__main__':
    build(r'D:\ESP32S3-MIDI\songs\baroque - passacaglia.mid')
