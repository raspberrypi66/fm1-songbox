"""jazz - blue corner : medium swing, AABA in F, vibes head, piano solo, walking bass.

Form (bars): intro 4 | head 32 | piano solo 32 | head out 16 (B A) | tag 4 | final chord 2
"""
from smflib import *

BPM = 148
SW = 1.0 / 3          # triplet swing: offbeat eighth lands on the last triplet

A1 = "Fmaj7 | Em7b5 A7 | Dm7 | G7 | Gm7 | C7 | Am7 D7 | Gm7 C7"
A2 = "Fmaj7 | Em7b5 A7 | Dm7 | G7 | Gm7 | C7 | Fmaj7 | Fmaj7"
B = "Cm7 | F7 | Bbmaj7 | Bbmaj7 | Bbm7 | Eb7 | Abmaj7 | Gm7 C7"
A3 = "Fmaj7 | Em7b5 A7 | Dm7 | G7 | Gm7 | C7 | F6 D7 | Gm7 C7"
INTRO = "Gm7 C7 | Gm7 C7 | Gm7 C7 | Gm7 C7"
TAG = "Gm7 C7 | Am7 D7 | Gm7 C7 | Fmaj9"

# ---- head melody (vibes), eighth-note grid, 8 slots per bar ----------------
HEAD_A = """
C5  -   A4  -   -   G4  F4  -  |
E4  -   -   -   .   G4  Bb4 C#5 |
D5  -   -   -   -   C5  D5  -  |
F5  -   E5  D5  B4  -   -   -  |
Bb4 -   -   D5  -   Bb4 A4  -  |
G4  -   -   -   -   E4  F4  G4 |
A4  -   -   C5  -   -   F#4 -  |
G4  -   -   -   -   -   -   -  """
HEAD_A2_END = """
C5  -   -   -   A4  -   F4  -  |
G4  -   -   -   -   -   -   .  """
HEAD_B = """
G4  -   -   Bb4 -   -   Eb5 -  |
D5  -   -   -   -   -   C5  A4 |
Bb4 -   -   -   -   -   -   -  |
.   -   -   -   -   F4  G4  A4 |
Bb4 -   -   Db5 -   -   F5  -  |
G5  -   -   -   -   -   F5  Db5|
C5  -   -   -   -   -   Bb4 C5 |
D5  -   -   Bb4 -   -   E4  -  """
HEAD_A3_END = """
A4  -   -   C5  -   -   F#4 -  |
G4  -   -   -   -   -   -   .  """
TAG_MEL = """
G4  -   -   Bb4 -   -   E4  -  |
A4  -   -   C5  -   -   F#4 -  |
G4  -   -   Bb4 -   -   E5  -  |
A5  -   -   -   -   -   -   -  """

# ---- piano solo, eighth-note grid -----------------------------------------
SOLO = """
.   .   A4  C5  E5  G5  F5  E5 |
D5  .   Bb4 G4  E4  .   C#5 E5 |
F5  .   D5  .   A4  C5  D5  F5 |
E5  D5  B4  .   .   G4  A4  B4 |
D5  .   .   F5  D5  Bb4 .   A4 |
G4  Bb4 C5  E5  .   .   G5  .  |
A5  G5  E5  C5  .   F#4 A4  C5 |
Bb4 -   -   -   .   .   .   .  |
.   .   C5  .   A4  .   F4  .  |
G4  .   E4  .   .   Bb4 C#5 E5 |
F5  E5  D5  C5  A4  .   .   .  |
.   .   B4  D5  F5  .   E5  D5 |
Bb4 -   .   .   D5  .   F5  .  |
E5  -   .   .   G5  .   Bb5 .  |
A5  -   -   .   .   G5  F5  E5 |
C5  -   -   -   .   .   .   .  |
Eb5 .   .   .   G5  .   Bb5 .  |
A5  .   .   Eb5 .   C5  .   A4 |
Bb4 .   .   D5  .   F5  .   A5 |
Bb5 -   -   .   .   A5  F5  D5 |
Db5 .   .   .   F5  .   Ab5 .  |
G5  .   .   Db5 .   Bb4 .   G4 |
Ab4 .   .   C5  .   Eb5 .   G5 |
F5  D5  Bb4 .   .   E4  G4  Bb4|
C5  .   E5  .   G5  .   A5  .  |
Bb5 .   G5  .   E5  .   C#5 .  |
D5  F5  A5  C6  .   .   A5  F5 |
E5  -   D5  .   B4  -   -   .  |
D5  -   -   -   .   .   Bb4 .  |
C5  -   -   -   .   E5  G5  Bb5|
A5  -   -   -   -   -   F#5 .  |
G5  -   -   -   -   -   .   .  """

# comping rhythms (eighth grid), cycled bar by bar
COMP_HEAD = ["x . . x . . . .", ". . . x . . x .", "x . . . . x . .", ". . . x . . . x",
             "x . . x . . x .", ". x . . . x . .", "x . . . . . x .", ". . . x . . . ."]
COMP_SOLO = [". . . x . . . .", "x . . . . x . .", ". . . x . . x .", ". x . . . . . .",
             ". . . . . x . .", "x . . x . . . .", ". . . x . . . x", ". . . . . . x ."]


def walking_bass(segs, lo=29, hi=55):
    """Rule-based walking line: root on the downbeat, then chord tones / passing tones,
    ending on a chromatic (or dominant) approach to the next root. Shapes cycle so the
    line does not repeat the same contour bar after bar."""
    shapes = [['1', '3', '5', 'app'], ['1', '5', '3', 'app'], ['1', '2', '3', 'app'],
              ['1', '3', '5', '6'], ['1', '7-', '5-', 'app'], ['1', '8', '5', 'app'],
              ['1', '3', '4', 'app'], ['1', '5', '8', 'app']]
    out = []
    prev = lo + 7
    for i, s in enumerate(segs):
        ch = s.chord
        nxt = segs[(i + 1) % len(segs)].chord
        root = nearest_octave(ch.bass if ch.bass != ch.root else ch.root, prev, lo, hi - 7)
        beats = int(round(s.beats))
        shape = shapes[i % len(shapes)] if beats == 4 else ['1', 'app' if i % 2 else '5']
        if beats == 8:
            shape = ['1', '3', '5', '8', '7', '5', '3', 'app']
        line = []
        for k, tok in enumerate(shape):
            if tok == 'app':
                last = line[-1] if line else root
                target = nearest_octave(nxt.root, last, lo, hi)
                if nxt.sym == ch.sym:               # same chord next: walk the scale instead
                    p = root + (ch.tone('7') if i % 2 else 12)
                elif i % 3 == 2 and (nxt.root + 7) % 12 != last % 12:   # dominant approach
                    p = nearest_octave(nxt.root + 7, last, lo, hi)
                else:                               # chromatic from above / below
                    p = target + (1 if i % 2 == 0 else -1)
            else:
                p = ch.degree(tok, root)
            line.append(fold(p, lo, hi))
        for b, p in enumerate(line):
            vel = 104 if b == 0 else 92 if b == 2 else 86
            out.append((s.tick + b * Q, p, vel, int(Q * 0.92)))
        prev = line[-1]
    return out


def comping(track, segs, rhythms, lo, hi, size=4, vel=72, gate=0.55, start_bar=0, prev=None):
    """Voice-led chord stabs on the rhythm patterns, chord per chart segment."""
    bars = chart_bars(segs)
    for b in range(bars):
        pat = rhythms[(start_bar + b) % len(rhythms)].split()
        for i, c in enumerate(pat):
            if c != 'x':
                continue
            beat = segs[0].beat + b * 4 + i / 2.0
            seg = seg_at(segs, beat)
            v = voicing(seg.chord, size, lo, hi, prev)
            prev = v
            tick = segs[0].tick + b * 4 * Q + i * E + (int(E * SW) if i % 2 else 0)
            hold = int(E * 1.6) if i % 2 else int(Q * gate * 1.4)
            for p in v:
                track.note(tick, p, vel + (6 if i % 2 == 0 else 0), hold)
    return prev


def build(path):
    s = Song('Blue Corner', BPM)
    vib = s.track('Vibes', 0, 11, vol=104, pan=54, reverb=58)
    pno = s.track('Piano', 1, 0, vol=96, pan=70, reverb=36)
    bass = s.track('Bass', 2, 32, vol=112, pan=64, reverb=12)
    dr = s.drums(vol=96, reverb=30)

    # chord chart for the whole piece
    form = [(INTRO, 'intro'), (A1, 'head'), (A2, 'head'), (B, 'head'), (A3, 'head'),
            (A1, 'solo'), (A2, 'solo'), (B, 'solo'), (A3, 'solo'),
            (B, 'out'), (A3, 'out'), (TAG, 'tag')]
    segs_all, labels = [], []
    beat = 0
    for text, label in form:
        segs = chart(text, start_beat=beat)
        segs_all += segs
        labels.append((int(beat // 4), label, segs))
        beat = segs[-1].beat + segs[-1].beats
    total_bars = int(beat // 4)
    final_bar = total_bars                     # bar index of the final chord

    # ---- bass: walking all the way, last bar is a held F ----------------
    bass.add(walking_bass(segs_all))
    bass.note(s.bar(final_bar), n('F2'), 100, s.bar_ticks * 2 - 40)

    # ---- piano comping -----------------------------------------------
    prev = None
    for bar0, label, segs in labels:
        if label == 'solo':
            prev = comping(pno, segs, COMP_SOLO, 48, 64, size=3, vel=62, start_bar=bar0, prev=prev)
        else:
            prev = comping(pno, segs, COMP_HEAD, 55, 74, size=4, vel=70, start_bar=bar0, prev=prev)
    # final chord: Fmaj9 spread, with sustain pedal
    ft = s.bar(final_bar)
    for k, p in enumerate([n('F2'), n('C3'), n('A3'), n('E4'), n('G4'), n('C5')]):
        pno.note(ft + k * 25, p, 84, s.bar_ticks * 2 - 200 - k * 25)
    pno.cc(ft, 64, 127)
    pno.cc(ft + s.bar_ticks * 2 - 120, 64, 0)

    # ---- piano solo lines ------------------------------------------
    solo_start = s.bar(36)
    pno.seq(SOLO, E, solo_start, swing=SW, vel=92, gate=0.82,
            vel_fn=lambda i: (4 if i % 2 else 0))

    # ---- vibes head / head out / tag --------------------------------
    hv = dict(swing=SW, vel=98, gate=0.9, vel_fn=lambda i: (0 if i % 8 else 6))
    A6 = '|'.join(HEAD_A.split('|')[:6])          # bars 1-6 of the A melody
    vib.seq(HEAD_A, E, s.bar(4), **hv)
    vib.seq(A6, E, s.bar(12), **hv)
    vib.seq(HEAD_A2_END, E, s.bar(18), **hv)
    vib.seq(HEAD_B, E, s.bar(20), **hv)
    vib.seq(A6, E, s.bar(28), **hv)
    vib.seq(HEAD_A3_END, E, s.bar(34), **hv)
    # head out: B then A
    vib.seq(HEAD_B, E, s.bar(68), **hv)
    vib.seq(A6, E, s.bar(76), **hv)
    vib.seq(HEAD_A3_END, E, s.bar(82), **hv)
    vib.seq(TAG_MEL, E, s.bar(84), **hv)
    vib.note(s.bar(final_bar), n('A5'), 100, s.bar_ticks * 2 - 60)
    vib.note(s.bar(final_bar) + 40, n('E5'), 80, s.bar_ticks * 2 - 100)

    # ---- drums ------------------------------------------------------
    ride = "x . x x x . x x"
    hat = ". . x . . . x ."
    drum_grid(dr, s.bar(0), 4, E, {HHP: hat, RIDE: [". . . . . . . .", ". . . . . . . .", ride, ride]}, swing=SW)
    drum_grid(dr, s.bar(0), 4, E, {K: "o . . . o . . ."}, swing=SW, vel_scale=0.6)
    snare_head = [". . . . . . . .", ". . . x . . . .", ". . . . . . . .", ". . . . . x . .",
                  ". x . . . . . .", ". . . . . . . x", ". . . x . . . .", ". . . . . . x ."]
    snare_solo = [". . . x . . . .", ". x . . . x . .", ". . . . . . . x", "x . . x . . . .",
                  ". . . x . x . .", ". x . . . . . x", ". . . . . x . .", ". . x . . . . x"]
    for bar0, label, segs in labels:
        if label == 'intro':
            continue
        bars = chart_bars(segs)
        drum_grid(dr, s.bar(bar0), bars, E, {RIDE: ride, HHP: hat, K: "o . . . o . . ."}, swing=SW,
                  vel_scale=1.0 if label != 'solo' else 1.05)
        drum_grid(dr, s.bar(bar0), bars, E, {SN: snare_solo if label == 'solo' else snare_head}, swing=SW,
                  vel_scale=0.55 if label != 'solo' else 0.7)
    for b in (4, 20, 36, 52, 68, 84):
        dr.note(s.bar(b), CRASH, 96, DRUM_LEN)
    # tag: kicks with the hits, final crash
    dr.note(s.bar(final_bar), CRASH, 110, DRUM_LEN)
    dr.note(s.bar(final_bar), K, 100, DRUM_LEN)
    dr.note(s.bar(final_bar + 1), RIDE, 70, DRUM_LEN)

    s.finish(s.bar(final_bar + 2) + Q)
    return s.write(path)


if __name__ == '__main__':
    build(r'D:\ESP32S3-MIDI\songs\jazz - blue corner.mid')
