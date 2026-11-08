"""lofi - late bus : lo-fi hip hop reference track, Eb major (final hook lifts to F), 84 BPM.

Same world as the firmware generator: Rhodes (EP1) with rootless 9th voicings and a laid-back
lag, finger bass, boom-bap kit with 58 % swing on the 16ths and a laid-back snare, flute hook,
vibes in the break, string pad. One motif (the "call") is developed through the whole piece:
hinted low on EP2 in the grooves, stated / varied / answered / resolved in the hooks,
augmented (double length) in the break, lifted a whole step and doubled in the final hook.

Form (bars): intro 4 | groove 8 | hook 8 | groove 4 | hook 8 | break 8 | final hook 8 (in F)
| outro 4 (ritardando, final Fmaj9)
"""
from smflib import *

BPM = 84
SW = 0.16                       # 58 % swing: odd 16ths late by 19 ticks
RHODES_LAG = 12                 # Rhodes sits a touch behind the beat (fixed, not random)
SNARE_LAG = 15                  # snare and side stick likewise

GROOVE = "Fm9 | Bb13 | Ebmaj9 | Cm9"
HOOK = "Abmaj9 | Gm7 | Fm9 | Bb13sus Bb13 | Abmaj9 | Gm7 | Fm9 Bb13 | Ebmaj9"
BREAK = "Cm9 | Abmaj9 | Ebmaj9 | Bb13sus | Cm9 | Abmaj9 | Ebmaj9 | Bb13sus"
HOOK_F = "Bbmaj9 | Am7 | Gm9 | C13sus C13 | Bbmaj9 | Am7 | Gm9 C13 | Fmaj9"
OUTRO_F = "Fmaj9 | Dm9 | Gm9 C13sus | Fmaj9"

# 16th grid, 16 slots per bar. The motif: dotted-eighth rise Bb C Eb, hold, a pickup.
HOOK_MEL = """
Bb4 - - C5  - - Eb5 - - - - - .  .  C5  -  |
D5  - - -   - - Bb4 - - - - - .  .  .   .  |
Bb4 - - C5  - - Eb5 - - - - - .  .  F5  -  |
C5  - - -   - - Ab4 - - - - - .  .  .   .  |
Eb5 - - G5  - - Bb5 - - - - - .  .  G5  -  |
F5  - - -   - - D5  - - - - - .  .  Bb4 C5 |
Eb5 - - C5  - - Bb4 - - - - - Ab4 - G4  -  |
G4  - - -   - - -   - - - - - .  .  .   .  """
GROOVE_HINT = """
Bb3 - - C4 - - Eb4 - - - - - . . . . |
D4  - - -  - - Bb3 - - - - - . . . . """
BREAK_MEL = """
Bb4 - - - - - - - C5 - - - - - - - |
Eb5 - - - - - - - -  - - - - - - - |
D5  - - - - - - - -  - - - Bb4 - - - |
C5  - - - - - - - -  - - - .  . . . |
Eb5 - - - - - - - G5 - - - - - - - |
Bb5 - - - - - - - -  - - - - - - - |
F5  - - - - - - - -  - - - D5 - - - |
C5  - - - - - - - -  - - - .  . . . """


def rhodes(track, segs, prev=None, vel=72, lag=RHODES_LAG, hold=False):
    """Rootless 4-note voicings. Pattern per bar: beat 1 (long), 2-and (short), and a push on
    4-and that already plays the next chord. Chords are rolled from the bottom."""
    pats = [[(0, 9, 0), (6, 3, -8), (14, 4, -6)], [(0, 6, 0), (10, 4, -8)]]
    bars = chart_bars(segs)
    for b in range(bars):
        pat = [(0, 16, 0)] if hold else pats[b % 2]
        for slot, length, dv in pat:
            beat = segs[0].beat + b * 4 + slot / 4.0
            seg = seg_at(segs, beat)
            if slot == 14:                                       # push: next bar's first chord
                nxt = seg_at(segs, beat + 0.5)
                if nxt is seg and b == bars - 1:
                    continue
                seg = nxt
            v = voicing(seg.chord, 4, 55, 72, prev)
            prev = v
            t = segs[0].tick + b * 4 * Q + slot * S + (int(S * SW) if slot % 2 else 0) + lag
            for k, p in enumerate(v):
                track.note(t + k * 10, p, vel + dv - (3 if k == 0 else 0), length * S - 20 - k * 10)
    return prev


def pad(track, segs, prev=None, vel=58):
    for seg in segs:
        v = voicing(seg.chord, 3, 60, 72, prev)
        prev = v
        for p in v:
            track.note(seg.tick, p, vel, seg.ticks - 12)
    return prev


def bass(track, segs, vel=100, held=False):
    for seg in segs:
        root = seg.chord.root_in(29)
        if held or seg.beats < 4:
            track.note(seg.tick, root, vel, seg.ticks - 30)
            continue
        track.note(seg.tick, root, vel, int(Q * 1.6))
        track.note(seg.tick + 2 * Q + E, root, vel - 14, int(Q * 0.9))
        track.note(seg.tick + 3 * Q + E + int(S * SW) + S, fold(root + seg.chord.tone('5') - 12, 28, 43),
                   vel - 22, int(S * 1.5))


def drums(dr, s, bar0, bars, busy=False, vel=1.0):
    kick = ["x . . . . . . x . . x . . . . .", "x . . . . . . x . . x . . . x ."]
    hat = "x . o . x . o . x . o . x . o ." if not busy else "x g o g x g o g x g o g x g o g"
    drum_grid(dr, s.bar(bar0), bars, S, {K: kick, HH: hat}, swing=SW, vel_scale=vel)
    drum_grid(dr, s.bar(bar0) + SNARE_LAG, bars, S, {SN: ". . . . x . . . . . . . x . . ."}, swing=SW, vel_scale=vel)
    for b in range(bars):
        if b % 4 == 3:
            dr.note(s.bar(bar0 + b) + 14 * S + SNARE_LAG, HHO, int(70 * vel), DRUM_LEN)


def snare_fill(dr, s, bar):
    base = s.bar(bar) + 3 * Q + SNARE_LAG
    for k, v in enumerate((58, 70, 84, 100)):
        dr.note(base + k * S + (int(S * SW) if k % 2 else 0), SN, v, DRUM_LEN)


def build(path):
    s = Song('Late Bus', BPM)
    fl = s.track('Flute', 0, 73, vol=96, pan=58, reverb=66)
    rh = s.track('Rhodes', 1, 4, vol=100, pan=52, reverb=48)
    ep2 = s.track('EP2 hint', 2, 5, vol=78, pan=76, reverb=56)
    vib = s.track('Vibes', 3, 11, vol=86, pan=40, reverb=64)
    pd = s.track('Strings', 4, 49, vol=74, pan=64, reverb=80)
    bs = s.track('Bass', 5, 33, vol=112, pan=64, reverb=8)
    dr = s.drums(vol=108, reverb=26)

    plan = [('intro', GROOVE), ('g1', GROOVE + ' | ' + GROOVE), ('h1', HOOK), ('g2', GROOVE), ('h2', HOOK),
            ('break', BREAK), ('h3', HOOK_F), ('outro', OUTRO_F)]
    at, segs_of = {}, {}
    beat = 0
    for name, text in plan:
        segs = chart(text, start_beat=beat)
        at[name] = int(beat // 4)
        segs_of[name] = segs
        beat = segs[-1].beat + segs[-1].beats
    final_bar = int(beat // 4) - 1                 # outro bar 4 = the final chord
    final = s.bar(final_bar)
    B = s.bar_ticks

    # ---- Rhodes --------------------------------------------------------------
    prev = None
    levels = {'intro': 60, 'g1': 68, 'h1': 74, 'g2': 68, 'h2': 76, 'break': 64, 'h3': 80, 'outro': 62}
    for name, _ in plan:
        segs = segs_of[name][:-1] if name == 'outro' else segs_of[name]
        prev = rhodes(rh, segs, prev, vel=levels[name], hold=name in ('break', 'outro'))
    for k, p in enumerate([n('F2'), n('C3'), n('A3'), n('E4'), n('G4'), n('C5')]):      # final Fmaj9 roll
        rh.note(final + RHODES_LAG + k * 30, p, 76 - k * 2, 2 * B - 260 - k * 30)
    rh.cc(final, 64, 127)
    rh.cc(final + 2 * B - 200, 64, 0)
    for name in ('h1', 'h2', 'h3'):                                                   # swell into hooks
        rh.ramp(11, s.bar(at[name] - 2), s.bar(at[name]), 104, 127)
    rh.ramp(11, s.bar(at['break']), s.bar(at['break'] + 1), 127, 112)
    rh.cc(s.bar(at['h3'] - 2), 11, 112)

    # ---- pad: hooks, break, outro ---------------------------------------------
    prev = None
    for name in ('h1', 'h2', 'break', 'h3'):
        prev = pad(pd, segs_of[name], prev, vel=56 if name != 'h3' else 62)
    prev = pad(pd, segs_of['outro'][:-1], prev, vel=54)
    for p in (n('A3'), n('E4'), n('G4')):
        pd.note(final, p, 60, 2 * B - 120)
    for name in ('h1', 'h2', 'h3'):
        pd.ramp(11, s.bar(at[name]), s.bar(at[name] + 2), 80, 127)
    pd.ramp(11, final + B, final + 2 * B - 160, 127, 56)
    pd.cc(final + 2 * B, 11, 127)

    # ---- bass -----------------------------------------------------------------
    bass(bs, segs_of['intro'][2:], vel=90)
    for name in ('g1', 'h1', 'g2', 'h2', 'h3'):
        bass(bs, segs_of[name], vel=100 if name != 'h3' else 106)
    bass(bs, segs_of['break'], vel=92, held=True)
    bass(bs, segs_of['outro'][:-1], vel=92, held=True)
    bs.note(final, n('F1'), 96, 2 * B - 120)

    # ---- drums ----------------------------------------------------------------
    drum_grid(dr, s.bar(3), 1, S, {HH: ". . . . . . . . o . o . x . o ."}, swing=SW, vel_scale=0.8)
    drums(dr, s, at['g1'], 8, vel=0.92)
    drums(dr, s, at['h1'], 8, busy=True)
    drums(dr, s, at['g2'], 4, vel=0.92)
    drums(dr, s, at['h2'], 8, busy=True)
    drums(dr, s, at['h3'], 8, busy=True, vel=1.06)
    for name in ('h1', 'h2', 'h3'):
        snare_fill(dr, s, at[name] - 1)
        dr.note(s.bar(at[name]), CRASH, 84, DRUM_LEN)
    drum_grid(dr, s.bar(at['break']) + SNARE_LAG, 7, S, {RIM: ". . . . m . . . . . . . m . . ."}, swing=SW)
    drum_grid(dr, s.bar(at['break']), 7, S, {K: "o . . . . . . . . . . . . . . ."}, swing=SW, vel_scale=0.7)
    base = s.bar(at['break'] + 7)
    for k in range(16):                                              # one-bar roll into the lift
        dr.note(base + k * S + (int(S * SW) if k % 2 else 0), SN, 40 + k * 4, DRUM_LEN)
        if k % 4 == 0:
            dr.note(base + k * S, K, 80, DRUM_LEN)
    drum_grid(dr, s.bar(at['outro']), 2, S, {K: "x . . . . . . . . . x . . . . .",
                                               HH: "x . o . x . o . x . o . x . o ."}, swing=SW, vel_scale=0.8)
    drum_grid(dr, s.bar(at['outro']) + SNARE_LAG, 2, S, {SN: ". . . . x . . . . . . . x . . ."}, swing=SW, vel_scale=0.8)
    dr.note(s.bar(at['outro'] + 2), K, 84, DRUM_LEN)
    dr.note(s.bar(at['outro'] + 2) + 2 * Q + SNARE_LAG, RIM, 70, DRUM_LEN)
    dr.note(final, CRASH, 72, DRUM_LEN)
    dr.note(final, K, 90, DRUM_LEN)

    # ---- melody: motif hinted, stated, augmented, lifted ---------------------------
    for name in ('g1', 'g2'):
        ep2.seq(GROOVE_HINT, S, s.bar(at[name]), swing=SW, vel=62, gate=0.9)
        if name == 'g1':
            ep2.seq(GROOVE_HINT, S, s.bar(at[name] + 4), swing=SW, vel=66, gate=0.9)
    ep2.seq(GROOVE_HINT.split('|')[0], S, s.bar(at['intro'] + 2), swing=SW, vel=54, gate=0.9)
    fl.seq(HOOK_MEL, S, s.bar(at['h1']), swing=SW, vel=92, gate=0.92)
    fl.seq(HOOK_MEL, S, s.bar(at['h2']), swing=SW, vel=96, gate=0.92)
    vib.seq(BREAK_MEL, S, s.bar(at['break']), swing=SW, vel=78, gate=1.0)
    fl.seq(HOOK_MEL, S, s.bar(at['h3']), swing=SW, vel=104, gate=0.92, transpose=2)
    vib.seq(HOOK_MEL, S, s.bar(at['h3']), swing=SW, vel=72, gate=1.0, transpose=2)
    fl.ramp(11, s.bar(at['h3'] + 7), s.bar(at['h3'] + 8), 127, 110)
    fl.cc(s.bar(at['outro']), 11, 127)
    # outro: the motif's head once more, slow, resting on the 3rd of Fmaj9
    fl.seq("C5 - - - - - D5 - - - - - F5 - - - | - - - - - - - - E5 - - - - - - - | D5 - - - - - - - C5 - - - - - - -",
           S, s.bar(at['outro']), swing=SW, vel=84, gate=0.97)
    fl.note(final, n('A4'), 80, 2 * B - 200)
    vib.note(final + S, n('E5'), 66, 2 * B - 300)
    fl.ramp(11, final + B, final + 2 * B - 220, 127, 56)
    fl.cc(final + 2 * B, 11, 127)

    s.rit(s.bar(at['outro'] + 1), final, BPM, 58, steps=10)
    s.tempo(final + 1, 54)
    s.finish(final + 2 * B + Q)
    return s.write(path)


if __name__ == '__main__':
    build(r'D:\ESP32S3-MIDI\songs\lofi - late bus.mid')
