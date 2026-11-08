"""house - after hours : deep house, 122 BPM, F minor. Four-on-the-floor, offbeat open hats,
Rhodes stabs on a syncopated 16th pattern, warm pad, rubber synth bass, FM-piano hook, oohs.

Form (bars): intro 8 | A 8 (stabs + pad) | B 8 (+ hook) | A2 8 (+ oohs, rim) | break 8 | drop 16 | outro 8 | hold 2
"""
from smflib import *

BPM = 122
LOOP = "Fm9 | Abmaj9 | Dbmaj9 | Eb9sus4"
BREAK = "Bbm9 | Abmaj9 | Dbmaj9 | Eb9sus4"

STAB = ". . x . . . . x . . x . . . x ."           # 16ths: 1&, 2a, 3&, 4a
BASS_PAT = ". . . 1 . . 8 . . . . 1 . . 1 b7"       # offbeat rubber bass
HOOK = """
.   C5  -   Eb5 -   -   C5  -  |
-   -   -   Bb4 -   -   -   .  |
.   Ab4 -   C5  -   -   Eb5 -  |
-   -   -   F5  -   -   -   .  |
.   C5  -   Eb5 -   -   F5  -  |
-   -   -   Eb5 -   -   -   .  |
.   Ab4 -   C5  -   -   Bb4 -  |
-   -   -   Ab4 -   -   -   .  """
OOHS = """
Ab4+C5  -  -  -  -  -  -  -  |  G4+C5  -  -  -  -  -  -  -  |
F4+Ab4  -  -  -  -  -  -  -  |  G4+Bb4 -  -  -  -  -  -  -  """


def stabs(track, segs, prev=None, vel=84):
    for seg in segs:
        v = voicing(seg.chord, 3, 60, 76, prev)
        prev = v
        bars = int(seg.ticks // (4 * Q))
        for b in range(bars):
            for i, c in enumerate(STAB.split()):
                if c == 'x':
                    for p in v:
                        track.note(seg.tick + b * 4 * Q + i * S, p, vel - (6 if i in (7, 14) else 0), int(S * 1.6))
    return prev


def pad(track, segs, prev=None, vel=66):
    for seg in segs:
        v = voicing(seg.chord, 3, 53, 67, prev, omit_root=False)
        prev = v
        for p in v:
            track.note(seg.tick, p, vel, seg.ticks - 10)
    return prev


def bass(track, segs, vel=108):
    for seg in segs:
        root = seg.chord.root_in(36)
        bars = int(seg.ticks // (4 * Q))
        for b in range(bars):
            track.seq(BASS_PAT, S, seg.tick + b * 4 * Q, vel=vel, gate=0.7, resolve=seg.chord.resolver(root))


def build(path):
    s = Song('After Hours', BPM)
    ep = s.track('Rhodes', 0, 4, vol=96, pan=58, reverb=50)
    pd = s.track('Pad', 1, 89, vol=88, pan=64, reverb=72)
    bs = s.track('Bass', 2, 39, vol=114, pan=64, reverb=6)
    hook = s.track('Hook', 3, 5, vol=92, pan=74, reverb=62)
    ooh = s.track('Oohs', 4, 53, vol=82, pan=44, reverb=78)
    dr = s.drums(vol=116, reverb=18)

    plan = [('intro', LOOP, 2), ('A', LOOP, 2), ('B', LOOP, 2), ('A2', LOOP, 2), ('break', BREAK, 2),
            ('drop', LOOP, 4), ('outro', LOOP, 2)]
    at, segs_of = {}, {}
    beat = 0
    for name, text, reps in plan:
        segs = []
        for r in range(reps):
            segs += chart(text, start_beat=beat)
            beat = segs[-1].beat + segs[-1].beats
        at[name] = int(segs[0].beat // 4)
        segs_of[name] = segs
    end_bar = int(beat // 4)

    # ---- drums --------------------------------------------------------------
    kick = "x . . . x . . . x . . . x . . ."
    clap = ". . . . x . . . . . . . x . . ."
    hat = "o . m . o . m . o . m . o . m ."
    ohat = ". . o . . . o . . . o . . . o ."
    shk = "m o m o m o m o m o m o m o m o"
    rim = ". . . x . . x . . . . x . . . ."
    drum_grid(dr, s.bar(0), 4, S, {K: kick, HH: hat})
    drum_grid(dr, s.bar(4), 4, S, {K: kick, HH: hat, CLAP: clap, SHAKER: shk})
    for name in ('A', 'B', 'A2'):
        drum_grid(dr, s.bar(at[name]), 8, S, {K: kick, HH: hat, HHO: ohat, CLAP: clap, SHAKER: shk})
    drum_grid(dr, s.bar(at['A2']), 8, S, {RIM: rim}, vel_scale=0.8)
    drum_grid(dr, s.bar(at['break']), 6, S, {HH: hat, SHAKER: shk}, vel_scale=0.8)
    base = s.bar(at['break'] + 6)
    for k in range(32):                                       # two-bar clap/snare build
        dr.note(base + k * S, CLAP if k % 2 == 0 else SN, 48 + k * 2, DRUM_LEN)
    drum_grid(dr, s.bar(at['drop']), 16, S, {K: kick, HH: hat, HHO: ohat, CLAP: clap, SHAKER: shk, RIM: rim})
    drum_grid(dr, s.bar(at['outro']), 4, S, {K: kick, HH: hat, HHO: ohat, CLAP: clap, SHAKER: shk})
    drum_grid(dr, s.bar(at['outro'] + 4), 4, S, {K: kick, HH: hat, SHAKER: shk})
    for name in ('A', 'B', 'A2', 'drop'):
        dr.note(s.bar(at[name]), CRASH, 104, DRUM_LEN)
    dr.note(s.bar(end_bar), CRASH, 96, DRUM_LEN)
    dr.note(s.bar(end_bar), K, 110, DRUM_LEN)

    # ---- bass ---------------------------------------------------------------
    bass(bs, segs_of['intro'][4:], vel=100)
    for name in ('A', 'B', 'A2', 'drop', 'outro'):
        bass(bs, segs_of[name])
    bs.note(s.bar(end_bar), n('F2'), 104, s.bar_ticks * 2 - 60)

    # ---- Rhodes stabs -------------------------------------------------------
    prev = None
    for name in ('A', 'B', 'A2', 'drop', 'outro'):
        prev = stabs(ep, segs_of[name], prev, vel=84 if name != 'drop' else 90)
    prev = stabs(ep, segs_of['break'], prev, vel=70)
    for k, p in enumerate([n('F3'), n('C4'), n('Eb4'), n('Ab4'), n('G4')]):             # final Fm9
        ep.note(s.bar(end_bar) + k * 20, p, 86, s.bar_ticks * 2 - 150 - k * 20)

    # ---- pad: in from A, swells, drops out in the outro's last half ------------
    prev = None
    for name in ('A', 'B', 'A2', 'break', 'drop'):
        prev = pad(pd, segs_of[name], prev, vel=70 if name != 'break' else 76)
    prev = pad(pd, segs_of['outro'][:4], prev, vel=64)
    pd.ramp(11, s.bar(at['A']), s.bar(at['A'] + 4), 60, 127)
    pd.ramp(11, s.bar(at['break']), s.bar(at['break'] + 7), 100, 127)
    for p in (n('F3'), n('Ab3'), n('C4'), n('Eb4')):
        pd.note(s.bar(end_bar), p, 72, s.bar_ticks * 2 - 60)

    # ---- hook (FM piano) ----------------------------------------------------
    for name in ('B', 'drop'):
        reps = 1 if name == 'B' else 2
        for r in range(reps):
            hook.seq(HOOK, E, s.bar(at[name] + r * 8), vel=88, gate=0.9)
    hook.seq(HOOK, E, s.bar(at['break']), vel=72, gate=1.0)
    hook.note(s.bar(end_bar) + Q, n('C6'), 70, s.bar_ticks - 100)

    # ---- oohs: break and drop, long two-note voicings with swells --------------
    for name in ('A2', 'break', 'drop'):
        reps = 2 if name == 'A2' else 2 if name == 'break' else 4
        for r in range(reps):
            ooh.seq(OOHS, E, s.bar(at[name] + r * 4), vel=76, gate=1.0)
        ooh.ramp(11, s.bar(at[name]), s.bar(at[name] + 2), 70, 120)
    ooh.cc(s.bar(at['drop'] + 16), 11, 127)

    s.finish(s.bar(end_bar + 2) + Q)
    return s.write(path)


if __name__ == '__main__':
    build(r'D:\ESP32S3-MIDI\songs\house - after hours.mid')
