"""bossa - quiet tide : bossa nova in D minor, nylon guitar, flute, vibes, acoustic bass.

Form (bars): intro 4 | A 16 (flute) | B 16 (flute) | A 16 (vibes) | B 16 (flute + vibes) | coda 4 + hold 2
"""
from smflib import *

BPM = 128

A = ("Dm9 | Dm9 | Em7b5 | A7b9 | Dm9 | Dm9 | Gm7 | C7 | "
     "Fmaj7 | Fmaj7 | Em7b5 | A7b9 | Dm9 | Bb7#11 | Em7b5 | A7b9")
B = ("Gm7 | C7 | Fmaj7 | Fmaj7 | Gm7 | C7 | Fmaj7 | A7b9 | "
     "Dm7 | G7 | Cmaj7 | Cmaj7 | Bbmaj7 | Bbmaj7 | Em7b5 | A7b9")
INTRO = "Dm9 | Dm9 | Em7b5 | A7b9"
CODA = "Dm9 | Em7b5 A7b9 | Dm9 | Dm6/9"

# flute, eighth-note grid (straight)
MEL_A = """
.   A4  .   A4  .   .   F5  -  |
-   -   E5  -   -   -   D5  -  |
.   G4  .   G4  .   .   E5  -  |
-   -   D5  -   -   -   C#5 -  |
.   A4  .   A4  .   .   F5  -  |
-   -   E5  -   -   -   D5  -  |
.   Bb4 .   Bb4 .   .   G5  -  |
-   -   E5  -   -   -   .   G5 |
A5  -   -   -   -   -   G5  -  |
F5  -   -   -   E5  -   C5  -  |
D5  -   -   -   -   -   Bb4 -  |
C#5 -   -   -   -   -   E5  -  |
D5  -   -   -   -   -   -   -  |
F5  -   -   -   E5  -   D5  -  |
E5  -   -   -   -   -   D5  -  |
C#5 -   -   -   -   -   .   .  """
MEL_B = """
Bb4 -   -   -   D5  -   -   -  |
E5  -   -   -   -   -   G5  -  |
A5  -   -   -   -   -   -   -  |
-   -   -   G5  F5  -   E5  -  |
D5  -   -   -   F5  -   -   -  |
E5  -   -   -   -   -   D5  -  |
C5  -   -   -   -   -   -   -  |
-   -   -   -   Bb4 -   C#5 -  |
D5  -   -   -   F5  -   -   -  |
B4  -   -   -   -   -   D5  -  |
E5  -   -   -   -   -   -   -  |
-   -   -   D5  C5  -   B4  -  |
A4  -   -   -   C5  -   -   -  |
D5  -   -   -   -   -   F5  -  |
E5  -   -   -   -   -   D5  -  |
C#5 -   -   -   -   -   .   .  """
MEL_CODA = """
.   A4  .   A4  .   .   F5  -  |
-   -   E5  -   -   -   C#5 -  |
D5  -   -   -   -   -   -   -  |
.   .   .   .   .   .   .   .  """

# vibes counter-line for the second B (answers the flute in the gaps)
VIB_B = """
.   .   .   .   .   .   .   .  |
.   .   .   .   .   .   .   .  |
.   .   .   .   C5  -   D5  -  |
F5  -   -   -   .   .   .   .  |
.   .   .   .   .   .   .   .  |
.   .   .   .   .   .   .   .  |
.   .   .   .   E5  -   F5  -  |
G5  -   -   -   .   .   .   .  |
.   .   .   .   .   .   .   .  |
.   .   .   .   .   .   .   .  |
.   .   .   .   G5  -   A5  -  |
B5  -   -   -   .   .   .   .  |
.   .   .   .   .   .   .   .  |
.   .   .   .   .   .   .   .  |
.   .   .   .   Bb4 -   D5  -  |
E5  -   -   -   .   .   .   .  """


def guitar(track, segs, lo=50, hi=66, prev=None, start_bar=0):
    """Bossa comping: thumb bass on 1 and 3 (root / fifth), chord on the bossa clave."""
    chord_pat = ["x . . x . . x .", ". . x . . x . ."]        # 2-bar clave
    bars = chart_bars(segs)
    for b in range(bars):
        pat = chord_pat[(start_bar + b) % 2].split()
        base = segs[0].tick + b * 4 * Q
        for i in range(8):
            beat = segs[0].beat + b * 4 + i / 2.0
            seg = seg_at(segs, beat)
            ch = seg.chord
            if i in (0, 4):                                        # thumb
                root = ch.bass_in(38) if ch.bass != ch.root else ch.root_in(38)
                p = root if (i == 0 or ch.bass != ch.root) else fold(root + ch.tone('5') - 12, 36, 50)
                track.note(base + i * E, p, 84, int(Q * 1.6))
            if pat[i] == 'x':
                v = voicing(ch, 3, lo, hi, prev)
                prev = v
                for k, p in enumerate(v):
                    track.note(base + i * E + k * 12, p, 68 + (6 if i == 0 else 0), int(E * 1.5))
    return prev


def bass(track, segs):
    """Bossa bass: root on 1 (dotted), pickup on 2-and, fifth on 3, pickup on 4-and."""
    for i, s in enumerate(segs):
        ch = s.chord
        root = ch.bass_in(33) if ch.bass != ch.root else ch.root_in(33)
        fifth = fold(root + ch.tone('5') - 12, 31, 45)
        if abs(fifth - root) > 7:
            fifth = fold(root + ch.tone('5'), 31, 45)
        if s.beats >= 4:
            pat = [(0, root, 100, E * 3), (3, root, 72, E), (4, fifth, 92, E * 3), (7, fifth, 70, E)]
        else:
            pat = [(0, root, 100, E * 3), (3, fifth, 76, E)]
        for slot, p, v, d in pat:
            track.note(s.tick + slot * E, p, v, int(d * 0.9))


def build(path):
    s = Song('Quiet Tide', BPM)
    fl = s.track('Flute', 0, 73, vol=100, pan=58, reverb=60)
    vib = s.track('Vibes', 1, 11, vol=90, pan=76, reverb=55)
    gtr = s.track('Nylon Guitar', 2, 24, vol=104, pan=48, reverb=40)
    bs = s.track('Bass', 3, 32, vol=112, pan=64, reverb=15)
    dr = s.drums(vol=92, reverb=28)

    form = [(INTRO, 'intro'), (A, 'a1'), (B, 'b1'), (A, 'a2'), (B, 'b2'), (CODA, 'coda')]
    beat = 0
    sections = {}
    all_segs = []
    for text, label in form:
        segs = chart(text, start_beat=beat)
        sections[label] = (int(beat // 4), segs)
        all_segs += segs
        beat = segs[-1].beat + segs[-1].beats
    total = int(beat // 4)                      # bars before the held chord

    # guitar and bass through everything
    prev = None
    for label in ('intro', 'a1', 'b1', 'a2', 'b2', 'coda'):
        bar0, segs = sections[label]
        prev = guitar(gtr, segs if label != 'coda' else segs[:-1], prev=prev, start_bar=bar0)
    bass(bs, all_segs[:-1])                      # last chord handled below
    last = all_segs[-1]
    bs.note(last.tick, n('D2'), 96, s.bar_ticks * 2 - 60)
    # final Dm6/9 strum on the guitar, spread
    for k, p in enumerate([n('D3'), n('A3'), n('E4'), n('F4'), n('B4')]):
        gtr.note(last.tick + k * 30, p, 78, s.bar_ticks * 2 - 120 - k * 30)

    # melodies
    fl.seq(MEL_A, E, s.bar(sections['a1'][0]), vel=92, gate=0.92, vel_fn=lambda i: 6 if i % 8 == 6 else 0)
    fl.seq(MEL_B, E, s.bar(sections['b1'][0]), vel=96, gate=0.92)
    vib.seq(MEL_A, E, s.bar(sections['a2'][0]), vel=88, gate=1.0)
    fl.seq(MEL_B, E, s.bar(sections['b2'][0]), vel=100, gate=0.92)
    vib.seq(VIB_B, E, s.bar(sections['b2'][0]), vel=76, gate=1.0)
    fl.seq(MEL_CODA, E, s.bar(sections['coda'][0]), vel=90, gate=0.95)
    fl.note(last.tick, n('E5'), 84, s.bar_ticks * 2 - 200)
    fl.ramp(11, last.tick + s.bar_ticks, last.tick + s.bar_ticks * 2 - 240, 127, 60)
    fl.cc(last.tick + s.bar_ticks * 2, 11, 127)

    # drums: brushes-light bossa. Hat 8ths, rim clave, soft kick on 1, 2-and, 3, 4-and.
    hat = "x o x o x o x o"
    rim = ["x . . x . . x .", ". . x . . x . ."]
    kick = "o . . o o . . o"
    drum_grid(dr, s.bar(2), total - 3, E, {HH: hat}, vel_scale=0.8)
    drum_grid(dr, s.bar(2), total - 3, E, {RIM: rim, K: kick}, vel_scale=0.85)
    drum_grid(dr, s.bar(4), total - 5, S, {SHAKER: ". o . m . o . m . o . m . o . m"}, vel_scale=0.9)
    for label in ('a1', 'b1', 'a2', 'b2'):
        dr.note(s.bar(sections[label][0]), CRASH, 72, DRUM_LEN)
    dr.note(last.tick, RIDE, 70, DRUM_LEN)
    dr.note(last.tick, K, 80, DRUM_LEN)

    s.finish(last.tick + s.bar_ticks * 2 + Q)
    return s.write(path)


if __name__ == '__main__':
    build(r'D:\ESP32S3-MIDI\songs\bossa - quiet tide.mid')
