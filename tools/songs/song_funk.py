"""funk - greasy spoon : 104 BPM, E9 vamp, slap bass, clavinet, muted guitar, horn stabs, alto sax.

Form (bars): intro 4 | A vamp 8 | B head 8 | A' 8 | bridge 8 | clav solo 8 | B head 8 | outro 4 + hit
Sixteenth-note grid with a light swing (odd 16ths delayed by 15 ticks).
"""
from smflib import *

BPM = 104
SW = 0.125                      # light 16th swing, 15 ticks

VAMP = "E9 | E9 | E9 | E9 | A9 | A9 | E9 | C9 B7#9"
BRIDGE = "Am7 | D9 | Gmaj7 | Cmaj7 | Am7 | D9 | F#m7b5 | B7#9"

# slap bass riff for the E9 vamp (2 bars, 16th grid), transposed for A9
BASS_E = """
E2 . . E2 . . G2 A2 . . E2 . . . D2 . |
E2 . . . B2 . . A2 . G2 . . E2 . . D2 """
BASS_CB = "C2 . . C2 . . . . B1 . . B1 . . . ."
BASS_BRIDGE_PAT = "1 . . . . . 8 . . . 5 . . . b7 ."      # per chord bar

# clavinet riff (2 bars), staccato
CLAV = """
E3 . . G3 . . A3 . . . Bb3 B3 . . D4 . |
E4 . . . . . B3 D4 . . . A3 . G3 . . """
CLAV_STAB = ". . . . . . G4+B4+D5 . . . . . . . G4+B4+D5 ."

# muted guitar scratch: dyads on a 16th pattern, accents on the "and"s
GTR = "o . x x . o x . o o x . o x x ."

# horns (brass section) answer every second bar; triads written out
HORN_A = """
. . . . . . . . . . . . . . . . |
. . . . . . G#4+D5+G5 . . . . . . . G#4+D5+G5 . |
. . . . . . . . . . . . . . . . |
. . . . . . . . . . . . B4 D5 E5 . |
. . . . . . . . . . . . . . . . |
. . . . . . C#5+G5+B5 . . . . . . . C#5+G5+B5 . |
. . . . . . . . . . . . . . . . |
E4+G4+Bb4 . . . . . . . D#4+F#4+A4 - - - . . . . """

# alto sax head (16th grid)
SAX_HEAD = """
. . . . . . B4 D5 E5 - - - . . G5 . |
E5 - - . D5 . B4 - - - . . . . . . |
. . . . . . B4 D5 E5 - - - . . G5 A5 |
G5 - - . E5 . D5 . B4 - - - . . . . |
A5 - - . G5 . E5 . . . D5 . E5 - - - |
- - - - . . G5 . E5 . D5 - - - . . |
E5 - - . G5 . E5 . D5 . B4 - - - . . |
G5 - - - . . . . F#5 - - - . . . . """
SAX_BRIDGE = """
C5 - - - E5 - - - G5 - - - - - - - |
F#5 - - - - - - - E5 - - - - - - - |
D5 - - - - - - - B4 - - - D5 - - - |
E5 - - - - - - - - - - - . . . . |
C5 - - - E5 - - - G5 - - - A5 - - - |
B5 - - - - - - - A5 - - - F#5 - - - |
E5 - - - - - - - C5 - - - - - - - |
D#5 - - - - - - - - - - - . . . . """
CLAV_SOLO = """
E4 G4 A4 . Bb4 B4 . D5 . . E5 . . D5 B4 . |
. . A4 G4 . E4 . . G4 . E4 . D4 . . . |
E4 . G4 . A4 . . B4 . D5 . E5 . . G5 . |
E5 . D5 . B4 . A4 . G4 . . E4 . . . . |
A4 . C5 . D5 . . E5 . G5 . A5 . . G5 . |
E5 . D5 . C5 . A4 . . . G4 . A4 . . . |
B4 . D5 E5 . . G5 . E5 . D5 . B4 . . . |
G4 . . . Bb4 . C5 . F#4 . . . A4 . D#5 . """
OUTRO_HITS = """
E3+G#3+D4+G4 . . . . . E3+G#3+D4+G4 . . . . . . . . . |
. . . . . . . . . . . . . . . . |
E3+G#3+D4+G4 . . . . . E3+G#3+D4+G4 . . . . . E3+G#3+D4+G4 . . . |
. . . . . . . . . . . . . . . . """


def drums(dr, bar0, bars, s, busy=False):
    kick = ["x . . x . . . . . . x . . . . .", "x . . x . . . . . . x . x . . ."]
    snare = ". . . . X . . g . g . . X . . g"
    hat = "X x x x X x x x X x x x X x x ."
    ohat = ". . . . . . . . . . . . . . x ."
    drum_grid(dr, s.bar(bar0), bars, S, {K: kick, SN: snare, HH: hat, HHO: ohat}, swing=SW,
              vel_scale=1.0 if busy else 0.9)


def fill(dr, bar, s):
    """One-bar snare/tom fill replacing the regular pattern's last half bar."""
    base = s.bar(bar) + 2 * Q
    pat = [(0, SN, 90), (1, SN, 70), (2, SN, 100), (3, TOM_HM, 96), (4, TOM_HM, 84), (5, TOM_M, 96),
           (6, TOM_LM, 100), (7, TOM_L, 110)]
    for slot, key, v in pat:
        dr.note(base + slot * S + (15 if slot % 2 else 0), key, v, DRUM_LEN)


def vamp_bass(track, s, bar0):
    for b2 in range(2):                      # E9 x4
        track.seq(BASS_E, S, s.bar(bar0 + b2 * 2), swing=SW, vel=108, gate=0.75)
    track.seq(BASS_E, S, s.bar(bar0 + 4), swing=SW, vel=108, gate=0.75, transpose=5)   # A9 x2
    track.seq(BASS_E.split('|')[0], S, s.bar(bar0 + 6), swing=SW, vel=108, gate=0.75)
    track.seq(BASS_CB, S, s.bar(bar0 + 7), swing=SW, vel=110, gate=0.8)


def vamp_gtr(track, s, bar0, bars, segs):
    """Chicken-scratch: 3rd + 7th dyad of the current chord on the GTR 16th pattern."""
    for b in range(bars):
        ch = seg_at(segs, segs[0].beat + b * 4).chord
        root = ch.root_in(52)
        dyad = (root + ch.tone('7'), root + 12 + ch.tone('3'))
        for i, c in enumerate(GTR.split()):
            if c == '.':
                continue
            tick = s.bar(bar0 + b) + i * S + (int(S * SW) if i % 2 else 0)
            for p in dyad:
                track.note(tick, p, 78 if c == 'x' else 52, int(S * 0.45))


def build(path):
    s = Song('Greasy Spoon', BPM)
    sax = s.track('Alto Sax', 0, 65, vol=108, pan=60, reverb=42, bends=True)
    clav = s.track('Clavinet', 1, 7, vol=98, pan=46, reverb=20)
    gtr = s.track('Muted Guitar', 2, 28, vol=84, pan=84, reverb=18)
    horns = s.track('Horns', 3, 61, vol=100, pan=70, reverb=38)
    bass = s.track('Slap Bass', 4, 36, vol=116, pan=64, reverb=8)
    dr = s.drums(vol=112, reverb=22)

    # section layout
    secs = [('intro', 4), ('A', 8), ('B', 8), ('A2', 8), ('bridge', 8), ('solo', 8), ('B2', 8), ('outro', 4)]
    bar = 0
    at = {}
    for name, length in secs:
        at[name] = bar
        bar += length
    end_bar = bar
    vamp = chart(VAMP)
    bridge = chart(BRIDGE)

    # ---- drums --------------------------------------------------------
    drums(dr, 0, 4, s)
    for name in ('A', 'B', 'A2', 'bridge', 'solo', 'B2'):
        drums(dr, at[name], 8, s, busy=name in ('B', 'B2', 'solo'))
        fill(dr, at[name] + 7, s)
        dr.note(s.bar(at[name]), CRASH, 100, DRUM_LEN)
    fill(dr, 3, s)
    # outro: hits with the band, then a fill and the final hit
    drum_grid(dr, s.bar(at['outro']), 1, S, {K: "x . . . . . x . . . . . . . . .", HH: "X x x x X x x x X x x x X x x x"}, swing=SW)
    fill(dr, at['outro'] + 1, s)
    drum_grid(dr, s.bar(at['outro'] + 1), 1, S, {HH: "X x x x X x x x . . . . . . . ."}, swing=SW)
    drum_grid(dr, s.bar(at['outro'] + 2), 1, S, {K: "x . . . . . x . . . . . x . . .",
                                                   CRASH: "x . . . . . . . . . . . . . . .",
                                                   SN: ". . . . . . x . . . . . x . . ."}, swing=SW)
    fill(dr, at['outro'] + 2, s)
    dr.note(s.bar(end_bar), CRASH, 118, DRUM_LEN)
    dr.note(s.bar(end_bar), K, 118, DRUM_LEN)
    dr.note(s.bar(end_bar) + Q * 2, HHO, 90, DRUM_LEN)

    # ---- bass ---------------------------------------------------------
    bass.seq(BASS_E, S, s.bar(0), swing=SW, vel=100, gate=0.75)
    bass.seq(BASS_E, S, s.bar(2), swing=SW, vel=104, gate=0.75)
    for name in ('A', 'B', 'A2', 'solo', 'B2'):
        vamp_bass(bass, s, at[name])
    for b, seg in enumerate(bridge):
        root = seg.chord.root_in(36)
        bass.seq(BASS_BRIDGE_PAT, S, s.bar(at['bridge'] + b), swing=SW, vel=104, gate=0.8,
                 resolve=seg.chord.resolver(root))
    bass.seq("E2 . . . . . E2 . . . . . . . . . | . . . . . . . . . . . . . . . . | "
             "E2 . . . . . E2 . . . . . E2 . . . | . . . . . . . . . . . . D2 . E2 .", S, s.bar(at['outro']),
             swing=SW, vel=110, gate=0.8)
    bass.note(s.bar(end_bar), n('E2'), 118, Q * 2 - 30)

    # ---- clavinet -----------------------------------------------------
    for name in ('A', 'A2', 'B2'):
        for k in range(2):
            clav.seq(CLAV, S, s.bar(at[name] + k * 2), swing=SW, vel=92, gate=0.5)
        clav.seq(CLAV, S, s.bar(at[name] + 4), swing=SW, vel=92, gate=0.5, transpose=5)
        clav.seq(CLAV.split('|')[0], S, s.bar(at[name] + 6), swing=SW, vel=92, gate=0.5)
        clav.seq("C4+E4+Bb4 . . . . . . . B3+D#4+A4 - - - . . . .", S, s.bar(at[name] + 7), swing=SW, vel=90, gate=0.6)
    for k in range(4):                                         # stabs on top during B
        clav.seq(CLAV_STAB, S, s.bar(at['B'] + k * 2), swing=SW, vel=84, gate=0.5)
        clav.seq(CLAV_STAB, S, s.bar(at['B2'] + k * 2), swing=SW, vel=84, gate=0.5)
    prev = None
    for b, seg in enumerate(bridge):                           # bridge: sustained comping
        v = voicing(seg.chord, 3, 55, 71, prev)
        prev = v
        for p in v:
            clav.note(s.bar(at['bridge'] + b), p, 80, int(Q * 1.4))
            clav.note(s.bar(at['bridge'] + b) + int(2.5 * Q) + 15, p, 74, int(Q * 0.6))
    clav.seq(CLAV_SOLO, S, s.bar(at['solo']), swing=SW, vel=100, gate=0.6, vel_fn=lambda i: -8 if i % 2 else 0)
    clav.seq(OUTRO_HITS, S, s.bar(at['outro']), swing=SW, vel=104, gate=0.5)
    for p in (n('E3'), n('G#3'), n('D4'), n('G4')):
        clav.note(s.bar(end_bar), p, 108, Q * 2 - 40)

    # ---- guitar scratch (A sections, head, solo) ----------------------
    for name in ('A', 'B', 'A2', 'solo', 'B2'):
        vamp_gtr(gtr, s, at[name], 8, vamp)
    vamp_gtr(gtr, s, 2, 2, chart("E9 | E9"))
    for b, seg in enumerate(bridge):
        vamp_gtr(gtr, s, at['bridge'] + b, 1, [Seg(0, 4, seg.chord)])

    # ---- horns --------------------------------------------------------
    horns.seq("E4+G#4+D5 . . . . . . . . . . . . . . .", S, s.bar(3), swing=SW, vel=104, gate=0.4)
    for name in ('A', 'A2'):
        horns.seq(HORN_A, S, s.bar(at[name]), swing=SW, vel=100, gate=0.45)
    horns.seq("E4+G#4+D5 . . . . . E4+G#4+D5 . . . . . . . . . | . . . . . . . . . . . . . . . . |"
              "E4+G#4+D5 . . . . . E4+G#4+D5 . . . . . E4+G#4+D5 . . . | . . . . . . . . . . . . . . . .",
              S, s.bar(at['outro']), swing=SW, vel=108, gate=0.45)
    for p in (n('G#4'), n('D5'), n('G5')):
        horns.note(s.bar(end_bar), p, 112, Q * 2 - 40)
    # B2: horns echo the sax phrase ends
    horns.seq(". . . . . . . . . . . . . . . . | . . . . . . . . . . G4+B4+E5 . . . . . |"
              ". . . . . . . . . . . . . . . . | . . . . . . . . . . . . G4+B4+E5 . . . |"
              ". . . . . . . . . . . . . . . . | . . . . . . . . . . . . A4+C#5+G5 . . . |"
              ". . . . . . . . . . . . . . . . | E4+G4+Bb4 . . . . . . . D#4+F#4+A4 - - - . . . .",
              S, s.bar(at['B2']), swing=SW, vel=96, gate=0.45)

    # ---- sax ----------------------------------------------------------
    for name in ('B', 'B2'):
        sax.seq(SAX_HEAD, S, s.bar(at[name]), swing=SW, vel=104 if name == 'B' else 110, gate=0.9)
        base = s.bar(at[name])
        sax.scoop(base + 8 * S, -2, 90)                  # scoop into the first long E5
        sax.scoop(base + s.bar_ticks * 2 + 8 * S, -2, 90)
        sax.scoop(base + s.bar_ticks * 4, -1, 80)
    sax.seq(SAX_BRIDGE, S, s.bar(at['bridge']), swing=SW, vel=96, gate=0.95)
    sax.ramp(11, s.bar(at['bridge'] + 4), s.bar(at['bridge'] + 6), 100, 127)
    sax.cc(s.bar(at['bridge'] + 7), 11, 127)
    # outro: short answers, then the last note with a fall-off
    sax.seq("G5 - - . E5 . . . . . . . . . . . | . . . . . . . . . . . . . . . . |"
            "G5 - - . E5 . . . B4 - - . . . . . | . . . . . . . . . . . . B4 D5 . .", S, s.bar(at['outro']),
            swing=SW, vel=104, gate=0.85)
    sax.note(s.bar(end_bar), n('E5'), 112, Q * 2 - 60)
    sax.scoop(s.bar(end_bar), -2, 90)
    fall0 = s.bar(end_bar) + Q + E
    for k in range(9):
        sax.bend_semis(fall0 + k * 30, -2 * k / 8.0)
    sax.bend(s.bar(end_bar) + Q * 2, 8192)

    s.finish(s.bar(end_bar) + Q * 3)
    return s.write(path)


if __name__ == '__main__':
    build(r'D:\ESP32S3-MIDI\songs\funk - greasy spoon.mid')
