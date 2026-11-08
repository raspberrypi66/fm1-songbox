"""reggae - sunday lane : roots reggae, 78 BPM, G major. One-drop drums, clean-guitar skank on
the offbeats, bubbling organ, heavy finger bass, harmonica (melodica-style) melody.

Form (bars): intro 4 | verse 8 | chorus 8 | verse 8 | chorus 8 | bridge 4 | chorus 8 | outro 4 + hold 2
"""
from smflib import *

BPM = 78
INTRO = "G | G | C | D"
VERSE = "G | G | C | D | G | Em | C | D"
CHORUS = "C | D | G | Em | C | D | G | G"
BRIDGE = "Em | C | Em | D"
OUTRO = "G | C | D | G"

BASS_A = "1 - - . 5- - . 1"       # verse: dotted root, fifth below, pickup
BASS_B = "1 - . 1 . 3 5 -"        # chorus: a little more movement
BASS_C = "1 - - . . 5- - 1"       # bridge

MEL_VERSE = """
.   .   B4  -   D5  -   -   -  |
-   -   .   .   D5  E5  D5  B4 |
C5  -   -   -   -   -   E5  -  |
D5  -   -   -   -   -   .   .  |
.   .   B4  -   D5  -   -   -  |
-   -   .   .   E5  -   D5  B4 |
C5  -   -   -   E5  -   -   -  |
F#5 -   -   -   D5  -   -   .  """
MEL_CHORUS = """
E5  -   -   -   G5  -   -   -  |
F#5 -   -   -   A5  -   -   -  |
G5  -   -   -   -   -   D5  -  |
E5  -   -   -   -   -   -   .  |
E5  -   -   -   G5  -   -   -  |
F#5 -   -   -   A5  -   -   -  |
G5  -   -   -   -   -   D5  -  |
B4  -   -   -   -   -   .   .  """
MEL_BRIDGE = """
E5  -   -   -   G5  -   -   -  |
E5  -   -   -   -   -   D5  -  |
B4  -   -   -   D5  -   -   -  |
D5  -   -   -   -   -   -   .  """
MEL_OUTRO = """
.   .   B4  -   D5  -   -   -  |
E5  -   -   -   -   -   D5  -  |
F#5 -   -   -   A5  -   -   -  |
G5  -   -   -   -   -   -   -  """


def skank(track, segs, prev=None, vel=84):
    """Guitar chop on every offbeat eighth, three-note voicings, very short."""
    for seg in segs:
        v = voicing(seg.chord, 3, 62, 76, prev, omit_root=False)
        prev = v
        for b in range(int(seg.beats)):
            for p in v:
                track.note(seg.tick + b * Q + E, p, vel, int(E * 0.35))
    return prev


def bubble(track, segs, prev=None, vel=58):
    """Organ bubble: two chord tones on the 'e' and 'a' of each beat."""
    for seg in segs:
        v = voicing(seg.chord, 2, 55, 67, prev, omit_root=False)
        prev = v
        for b in range(int(seg.beats)):
            for slot in (1, 3):
                for p in v:
                    track.note(seg.tick + b * Q + slot * S, p, vel + (6 if slot == 3 else 0), int(S * 0.6))
    return prev


def bass(track, segs, pat, vel=110):
    for seg in segs:
        root = seg.chord.root_in(31)
        track.seq(pat, E, seg.tick, vel=vel, gate=0.9, resolve=seg.chord.resolver(root))


def build(path):
    s = Song('Sunday Lane', BPM)
    harm = s.track('Harmonica', 0, 22, vol=100, pan=58, reverb=58)
    org = s.track('Organ', 1, 16, vol=82, pan=80, reverb=38)
    gtr = s.track('Skank Guitar', 2, 27, vol=92, pan=40, reverb=42)
    bs = s.track('Bass', 3, 33, vol=118, pan=64, reverb=8)
    dr = s.drums(vol=104, reverb=30)

    plan = [('intro', INTRO), ('v1', VERSE), ('c1', CHORUS), ('v2', VERSE), ('c2', CHORUS),
            ('bridge', BRIDGE), ('c3', CHORUS), ('outro', OUTRO)]
    at, segs_of = {}, {}
    beat = 0
    for name, text in plan:
        segs = chart(text, start_beat=beat)
        at[name] = int(beat // 4)
        segs_of[name] = segs
        beat = segs[-1].beat + segs[-1].beats
    end_bar = int(beat // 4) - 1                    # the outro's last bar holds the final chord
    final = s.bar(end_bar)

    # ---- drums: one drop (kick + rim on 3), hats with offbeat accents ---------
    one_drop = {K: ". . . . x . . .", RIM: ". . . . x . . .", HH: "o x o x o x o x"}
    rockers = {K: "x . . . x . . .", SN: ". . . . x . . .", HH: "o x o x o x o X", HHO: ". . . . . . . x"}
    for name in ('intro', 'v1', 'v2', 'bridge', 'outro'):
        bars = chart_bars(segs_of[name]) - (1 if name == 'outro' else 0)
        drum_grid(dr, s.bar(at[name]), bars, E, one_drop)
    for name in ('c1', 'c2', 'c3'):
        drum_grid(dr, s.bar(at[name]), 8, E, rockers)
        dr.note(s.bar(at[name]), CRASH, 96, DRUM_LEN)
    # drum pickups into each chorus: snare flam-ish roll on 4 and 4-and
    for name in ('c1', 'c2', 'c3'):
        base = s.bar(at[name]) - Q
        for k, v in enumerate((70, 80, 96, 110)):
            dr.note(base + k * S, SN, v, DRUM_LEN)
    for b in range(3):
        base = s.bar(at['bridge'] + b) + 3 * Q
        dr.note(base, TOM_M, 84, DRUM_LEN)
        dr.note(base + E, TOM_LM, 90, DRUM_LEN)
    dr.note(final, CRASH, 104, DRUM_LEN)
    dr.note(final, K, 108, DRUM_LEN)
    dr.note(final, RIM, 100, DRUM_LEN)

    # ---- bass ---------------------------------------------------------------
    bass(bs, segs_of['intro'], BASS_A, vel=104)
    for name in ('v1', 'v2'):
        bass(bs, segs_of[name], BASS_A)
    for name in ('c1', 'c2', 'c3'):
        bass(bs, segs_of[name], BASS_B, vel=114)
    bass(bs, segs_of['bridge'], BASS_C)
    bass(bs, segs_of['outro'][:3], BASS_A)
    bs.note(final, n('G1'), 112, s.bar_ticks * 2 - 60)

    # ---- skank + bubble -----------------------------------------------------
    prev = None
    for name, _ in plan:
        segs = segs_of[name][:-1] if name == 'outro' else segs_of[name]
        prev = skank(gtr, segs, prev, vel=84 if name not in ('c1', 'c2', 'c3') else 92)
    for k, p in enumerate([n('G3'), n('D4'), n('G4'), n('B4')]):
        gtr.note(final + k * 30, p, 90, s.bar_ticks * 2 - 160 - k * 30)
    prev = None
    for name in ('v1', 'c1', 'v2', 'c2', 'bridge', 'c3', 'outro'):
        segs = segs_of[name][:-1] if name == 'outro' else segs_of[name]
        prev = bubble(org, segs, prev, vel=58 if name not in ('c1', 'c2', 'c3') else 64)
    for p in (n('B3'), n('D4'), n('G4')):
        org.note(final, p, 74, s.bar_ticks * 2 - 60)
    org.ramp(11, final + s.bar_ticks, final + s.bar_ticks * 2 - 120, 127, 50)
    org.cc(final + s.bar_ticks * 2, 11, 127)

    # ---- harmonica melody ---------------------------------------------------
    harm.seq(MEL_VERSE, E, s.bar(at['v1']), vel=88, gate=0.92)
    harm.seq(MEL_CHORUS, E, s.bar(at['c1']), vel=98, gate=0.95)
    harm.seq(MEL_VERSE, E, s.bar(at['v2']), vel=90, gate=0.92)
    harm.seq(MEL_CHORUS, E, s.bar(at['c2']), vel=100, gate=0.95)
    harm.seq(MEL_BRIDGE, E, s.bar(at['bridge']), vel=86, gate=0.97)
    harm.seq(MEL_CHORUS, E, s.bar(at['c3']), vel=102, gate=0.95)
    harm.seq('|'.join(MEL_OUTRO.split('|')[:3]), E, s.bar(at['outro']), vel=92, gate=0.95)
    harm.note(final, n('G5'), 96, s.bar_ticks * 2 - 120)
    harm.note(final, n('B4'), 80, s.bar_ticks * 2 - 120)
    harm.ramp(11, final + s.bar_ticks, final + s.bar_ticks * 2 - 160, 127, 56)
    harm.cc(final + s.bar_ticks * 2, 11, 127)

    s.finish(final + s.bar_ticks * 2 + Q)
    return s.write(path)


if __name__ == '__main__':
    build(r'D:\ESP32S3-MIDI\songs\reggae - sunday lane.mid')
