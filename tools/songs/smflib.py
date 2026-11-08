"""smflib - small Standard MIDI File writer plus music helpers (standard library only).

Used by the song scripts in this folder. Everything is deterministic: no randomness.

Main pieces
- Song / Track: collect notes and controllers, write a format 1 SMF at 480 PPQN.
- n("Bb3"): note name to MIDI number (C4 = 60).
- seq(...): grid notation. Tokens on a fixed step grid (8th, 16th, ...):
      "C4"  note          "C4+E4+G4" chord      "-" tie / hold previous
      "." or "r" rest     "C4!" accent          "C4_" soft
      "C4@75" exact velocity                    "C4~" legato (no gap)
  Tokens that are not note names go through an optional resolver, so chord
  degree tokens ("1", "b3", "5-", "8", "9+") can be written against a chord.
- Chord("Gm7/F"): chord symbol parser with chord-aware degrees and voicing.
- chart("Fmaj7 | Em7b5 A7 | ..."): chord chart to timed segments.
- drum_grid(...): drum patterns per GM key as 'x.x.' strings.
"""
import itertools
import re
import struct

PPQN = 480
Q = PPQN            # quarter note
E = PPQN // 2       # eighth
S = PPQN // 4       # sixteenth
T = PPQN // 3       # triplet eighth
DRUM_LEN = 30       # written length of drum hits (keeps polyphony honest)

_NOTE_RE = re.compile(r'^([A-Ga-g])([#b]?)(-?\d)$')
_PC = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
_ACC = {'#': 1, 'b': -1, '': 0}


def n(name):
    """'C4' -> 60, 'Bb2' -> 46, 'F#5' -> 78."""
    m = _NOTE_RE.match(name)
    if not m:
        raise ValueError('bad note name %r' % name)
    return 12 * (int(m.group(3)) + 1) + _PC[m.group(1).upper()] + _ACC[m.group(2)]


def pc(name):
    """'Bb' -> 10."""
    return (_PC[name[0].upper()] + _ACC[name[1:]]) % 12


def vlq(v):
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.insert(0, 0x80 | (v & 0x7F))
        v >>= 7
    return bytes(out)


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def fold(p, lo, hi):
    """Move pitch by octaves into [lo, hi]."""
    while p < lo:
        p += 12
    while p > hi:
        p -= 12
    return p


def nearest_octave(pc_, ref, lo, hi):
    """The pitch of pitch class pc_ within [lo, hi] closest to ref."""
    best = None
    for p in range(lo, hi + 1):
        if p % 12 == pc_ % 12 and (best is None or abs(p - ref) < abs(best - ref)):
            best = p
    return best


# ---------------------------------------------------------------- chords ---

QUALITIES = {
    '': [0, 4, 7], 'maj': [0, 4, 7], 'm': [0, 3, 7], 'min': [0, 3, 7],
    'dim': [0, 3, 6], 'aug': [0, 4, 8], '5': [0, 7],
    '7': [0, 4, 7, 10], 'maj7': [0, 4, 7, 11], 'm7': [0, 3, 7, 10],
    'm7b5': [0, 3, 6, 10], 'dim7': [0, 3, 6, 9], 'mmaj7': [0, 3, 7, 11],
    '6': [0, 4, 7, 9], 'm6': [0, 3, 7, 9], '6/9': [0, 4, 7, 9, 14], 'm6/9': [0, 3, 7, 9, 14],
    '9': [0, 4, 7, 10, 14], 'maj9': [0, 4, 7, 11, 14], 'm9': [0, 3, 7, 10, 14],
    'm11': [0, 3, 7, 10, 14, 17], '13': [0, 4, 7, 10, 14, 21],
    '7b9': [0, 4, 7, 10, 13], '7#9': [0, 4, 7, 10, 15], '7#11': [0, 4, 7, 10, 18],
    'maj7#11': [0, 4, 7, 11, 18], '7b13': [0, 4, 7, 10, 20], '7alt': [0, 4, 10, 13, 20],
    'sus4': [0, 5, 7], 'sus2': [0, 2, 7], '7sus4': [0, 5, 7, 10], '9sus4': [0, 5, 7, 10, 14],
    '13sus': [0, 5, 7, 10, 14, 21],
    'add9': [0, 4, 7, 14], 'madd9': [0, 3, 7, 14],
}
_CHORD_RE = re.compile(r'^([A-G][#b]?)(.*?)(?:/([A-G][#b]?))?$')

# literal degree names -> semitones above the root
_DEG = {'1': 0, 'b2': 1, '2': 2, 'b3': 3, '3': 4, '4': 5, '#4': 6, 'b5': 6, '5': 7,
        '#5': 8, 'b6': 8, '6': 9, 'b7': 10, '7': 11, '8': 12, 'b9': 13, '9': 14,
        '#9': 15, '11': 17, '#11': 18, 'b13': 20, '13': 21}


class Chord:
    def __init__(self, sym):
        m = _CHORD_RE.match(sym.strip())
        if not m or m.group(2) not in QUALITIES:
            raise ValueError('bad chord symbol %r' % sym)
        self.sym = sym.strip()
        self.root = pc(m.group(1))
        self.ivs = list(QUALITIES[m.group(2)])
        self.bass = pc(m.group(3)) if m.group(3) else self.root
        self.quality = m.group(2)

    def roles(self):
        r = {'1': 0}
        for iv in self.ivs:
            if iv in (3, 4):
                r['3'] = iv
            elif iv in (6, 7, 8):
                r['5'] = iv
            elif iv in (10, 11):
                r['7'] = iv
            elif iv == 9:
                r['6'] = iv
            elif iv >= 13:
                r.setdefault('ext', []).append(iv)
        if '3' not in r:                      # sus chords: the 4th or 2nd stands in
            for iv in self.ivs:
                if iv in (2, 5):
                    r['3'] = iv
        return r

    def tone(self, name):
        """Chord-aware degree: '3' is the chord's third (major or minor), '7' its seventh."""
        r = self.roles()
        if name == '3':
            return r.get('3', 4)
        if name == '5':
            return r.get('5', 7)
        if name == '7':
            return r.get('7', 11 if self.quality.startswith('maj') else 10)
        if name == '9':
            return 14
        if name == '6':
            return 9
        return _DEG[name]

    def degree(self, tok, root_pitch):
        """'b3' -> pitch, '5-' one octave down, '8+' one octave up."""
        up = tok.count('+')
        down = tok.count('-')
        core = tok.replace('+', '').replace('-', '')
        return root_pitch + self.tone(core) + 12 * (up - down)

    def resolver(self, root_pitch):
        return lambda tok: self.degree(tok, root_pitch)

    def root_in(self, lo):
        """Root pitch inside [lo, lo + 12)."""
        return lo + (self.root - lo) % 12

    def bass_in(self, lo):
        return lo + (self.bass - lo) % 12

    def pcs(self):
        return {(self.root + iv) % 12 for iv in self.ivs}

    def __repr__(self):
        return 'Chord(%s)' % self.sym


def voicing(chord, size, lo, hi, prev=None, omit_root=None, center=None):
    """Pick `size` chord tones and place them in [lo, hi] with smooth voice leading
    from `prev` (a list of pitches). Guide tones first, root last (omitted on 7th chords)."""
    r = chord.roles()
    if omit_root is None:
        omit_root = len(chord.ivs) >= 4
    order = []
    for key in ('3', '7', 'ext', '6', '5', '1'):
        if key == '1' and omit_root:
            continue
        if key == 'ext':
            order += r.get('ext', [])
        elif key in r:
            order.append(r[key])
    if not omit_root:
        order = [0] + [iv for iv in order if iv != 0]
    pcs = []
    for iv in order:
        p = (chord.root + iv) % 12
        if p not in pcs:
            pcs.append(p)
    pcs = pcs[:size]
    pad = (14, 0, r.get('5', 7), r.get('3', 4)) if omit_root else (0, r.get('5', 7), r.get('3', 4))
    if chord.quality in ('dim7', 'm7b5', '7b9', '7#9', '7alt', 'sus4', 'sus2', '7sus4', '13sus'):
        pad = (0, r.get('5', 7), r.get('3', 4))     # no plain 9th on these
    while len(pcs) < size:                      # pad with the 9th / root / fifth
        for iv in pad:
            p = (chord.root + iv) % 12
            if len(pcs) < size and pcs.count(p) < 2 and (p not in pcs or iv == 0):
                pcs.append(p)
    cands = [[p for p in range(lo, hi + 1) if p % 12 == c] for c in pcs]
    mid = center if center is not None else (lo + hi) / 2.0
    best, best_cost = None, None
    for combo in itertools.product(*cands):
        if len(set(combo)) < size:
            continue
        s = sorted(combo)
        if prev:
            cost = sum(min(abs(p - q) for q in prev) for p in s)
            cost += sum(min(abs(p - q) for p in s) for q in prev)
        else:
            cost = abs((s[0] + s[-1]) / 2.0 - mid) * 2
        for a, b in zip(s, s[1:]):
            if b - a == 1:
                cost += 10
            if b - a > 9:
                cost += 4
        if s[-1] - s[0] > 14:
            cost += (s[-1] - s[0] - 14) * 2
        if best is None or cost < best_cost:
            best, best_cost = s, cost
    return best


class Seg:
    """One chord segment of a chart: start / duration in quarter-note beats."""
    __slots__ = ('beat', 'beats', 'chord')

    def __init__(self, beat, beats, chord):
        self.beat, self.beats, self.chord = beat, beats, chord

    @property
    def tick(self):
        return int(round(self.beat * Q))

    @property
    def ticks(self):
        return int(round(self.beats * Q))

    def __repr__(self):
        return 'Seg(%g, %g, %s)' % (self.beat, self.beats, self.chord.sym)


def chart(text, bar_beats=4, start_beat=0.0):
    """'Fmaj7 | Em7b5 A7 | Dm7:3 G7:1' -> [Seg...]. Chords in a bar split it equally
    unless given explicit ':beats'. '%' repeats the previous bar."""
    segs = []
    beat = start_beat
    prev_bar = None
    for bar in text.split('|'):
        bar = bar.strip()
        if not bar:
            continue
        if bar == '%':
            bar = prev_bar
        prev_bar = bar
        items = bar.split()
        explicit = [float(i.split(':')[1]) if ':' in i else None for i in items]
        used = sum(e for e in explicit if e)
        free = [e is None for e in explicit].count(True)
        each = (bar_beats - used) / free if free else 0
        for item, e in zip(items, explicit):
            sym = item.split(':')[0]
            d = e if e is not None else each
            segs.append(Seg(beat, d, Chord(sym)))
            beat += d
    return segs


def chart_bars(segs, bar_beats=4):
    return int(round((segs[-1].beat + segs[-1].beats - segs[0].beat) / bar_beats))


def seg_at(segs, beat):
    """Chord segment sounding at `beat`."""
    cur = segs[0]
    for s in segs:
        if s.beat <= beat + 1e-6:
            cur = s
    return cur


# ----------------------------------------------------------- grid notation ---

_TOK_RE = re.compile(r'^(>?)(.+?)([!_]?)(@(\d+))?(~?)$')


def seq(text, step, start=0, swing=0.0, vel=90, gate=0.9, accent=14, resolve=None,
        transpose=0, vel_fn=None):
    """Grid notation to notes. Returns [(tick, pitch, vel, dur), ...].

    step   grid size in ticks (E = eighth, S = sixteenth, T = triplet eighth)
    swing  fraction of `step` that odd grid slots are delayed (1/3 = triplet swing)
    gate   fraction of the written length that sounds (ties with '-' extend it)
    vel_fn optional f(slot_index) -> velocity offset, for metric accents
    """
    toks = [t for t in text.replace('|', ' ').split() if t]
    shift = int(round(step * swing))

    def pos(i):
        return start + i * step + (shift if i % 2 else 0)

    notes = []
    i = 0
    while i < len(toks):
        tok = toks[i]
        if tok in ('.', 'r'):
            i += 1
            continue
        if tok == '-':
            i += 1
            continue
        m = _TOK_RE.match(tok)
        if not m:
            raise ValueError('bad token %r' % tok)
        j = i + 1
        while j < len(toks) and toks[j] == '-':
            j += 1
        length = pos(j) - pos(i)
        v = vel
        if m.group(1) or m.group(3) == '!':
            v += accent
        if m.group(3) == '_':
            v -= accent
        if m.group(5):
            v = int(m.group(5))
        if vel_fn:
            v += vel_fn(i)
        if m.group(6):
            dur = length - 1
        else:
            dur = max(1, int(round(length * gate)))
        for name in m.group(2).split('+'):
            if _NOTE_RE.match(name):
                p = n(name)
            elif resolve:
                p = resolve(name)
            else:
                raise ValueError('cannot resolve token %r' % name)
            notes.append((pos(i), p + transpose, clamp(v, 1, 127), dur))
        i = j
    return notes


def arp(voices, pattern, step, start, count, vel=80, gate=0.9, vel_fn=None):
    """Arpeggiate `voices` (pitches) by index pattern, `count` steps long."""
    out = []
    for k in range(count):
        idx = pattern[k % len(pattern)]
        if idx is None:
            continue
        v = vel + (vel_fn(k) if vel_fn else 0)
        out.append((start + k * step, voices[idx], clamp(v, 1, 127), max(1, int(step * gate))))
    return out


# ---------------------------------------------------------------- drums ---

K, SN, RIM, CLAP, HH, HHP, HHO, RIDE, RIDEB, CRASH, CRASH2 = 36, 38, 37, 39, 42, 44, 46, 51, 53, 49, 57
TOM_L, TOM_LM, TOM_M, TOM_HM, TOM_H = 41, 45, 47, 48, 50
SHAKER, TAMB, CABASA, MARACAS, COWBELL = 82, 54, 69, 70, 56
DRUM_VEL = {'X': 118, 'x': 100, 'o': 76, 'g': 44, 'm': 60}


def drum_grid(track, start, bars, step, patterns, swing=0.0, vel_map=None, vel_scale=1.0,
              bar_ticks=None):
    """patterns: {key: 'x...x...' or [per-bar strings cycled]}. One char per grid slot."""
    vm = vel_map or DRUM_VEL
    shift = int(round(step * swing))
    for key, pat in patterns.items():
        pats = [pat] if isinstance(pat, str) else list(pat)
        for b in range(bars):
            p = pats[b % len(pats)].replace(' ', '').replace('|', '')
            bt = bar_ticks if bar_ticks else len(p) * step
            base = start + b * bt
            for i, ch in enumerate(p):
                if ch == '.':
                    continue
                t = base + i * step + (shift if i % 2 else 0)
                track.note(t, key, clamp(int(vm[ch] * vel_scale), 1, 127), DRUM_LEN)


# ------------------------------------------------------------- SMF writer ---

def _meta(kind, data):
    return b'\xff' + bytes([kind]) + vlq(len(data)) + data


class Track:
    def __init__(self, song, name, ch):
        self.song, self.name, self.ch = song, name, ch
        self.notes = []       # [tick, dur, pitch, vel]
        self.evs = []         # (tick, order, bytes)

    # -- notes --------------------------------------------------------
    def note(self, tick, pitch, vel, dur):
        self.notes.append([int(tick), max(1, int(dur)), clamp(int(pitch), 0, 127), clamp(int(vel), 1, 127)])

    def add(self, notes, vel_scale=1.0, transpose=0):
        for t, p, v, d in notes:
            self.note(t, p + transpose, int(round(v * vel_scale)), d)
        return notes

    def seq(self, text, step, start=0, **kw):
        return self.add(seq(text, step, start, **kw))

    # -- controllers --------------------------------------------------
    def cc(self, tick, num, val):
        self.evs.append((int(tick), 1, bytes([0xB0 | self.ch, num, clamp(val, 0, 127)])))

    def pc(self, tick, prog):
        self.evs.append((int(tick), 1, bytes([0xC0 | self.ch, clamp(prog, 0, 127)])))

    def bend(self, tick, value):
        """value 0..16383, 8192 = centre."""
        v = clamp(int(value), 0, 16383)
        self.evs.append((int(tick), 1, bytes([0xE0 | self.ch, v & 0x7F, v >> 7])))

    def bend_semis(self, tick, semis, range_semis=2):
        self.bend(tick, 8192 + int(round(8191 * semis / float(range_semis))))

    def scoop(self, tick, semis=-2, dur=100, steps=8, range_semis=2):
        """Slide into a note from `semis` below (negative) over `dur` ticks, end centred."""
        for k in range(steps + 1):
            frac = k / float(steps)
            self.bend_semis(tick + int(dur * frac), semis * (1 - frac), range_semis)

    def ramp(self, num, t0, t1, v0, v1, every=40):
        steps = max(1, int((t1 - t0) // every))
        for k in range(steps + 1):
            tick = t0 + (t1 - t0) * k // steps
            self.cc(tick, num, int(round(v0 + (v1 - v0) * k / float(steps))))

    def text(self, tick, s):
        self.evs.append((int(tick), 1, _meta(0x01, s.encode('ascii'))))

    # -- output -------------------------------------------------------
    def build(self, end_tick):
        evs = [(0, 0, _meta(0x03, self.name.encode('ascii')))] + list(self.evs)
        for t, d, p, v in self.notes:
            evs.append((t, 2, bytes([0x90 | self.ch, p, v])))
            evs.append((t + d, 0, bytes([0x80 | self.ch, p, 64])))
        evs.sort(key=lambda e: (e[0], e[1]))
        out = bytearray()
        last = 0
        for t, _, data in evs:
            out += vlq(t - last) + data
            last = t
        out += vlq(max(0, end_tick - last)) + b'\xff\x2f\x00'
        return b'MTrk' + struct.pack('>I', len(out)) + bytes(out)


class Song:
    def __init__(self, title, bpm, num=4, den=4):
        self.title = title
        self.num, self.den = num, den
        self.tempos = [(0, bpm)]
        self.tracks = []
        self.setup_done = set()
        self.bar_ticks = int(num * Q * 4 / den)
        self.end_tick = None

    def bar(self, b, beat=0.0):
        """Tick of bar `b` (0-based) plus `beat` quarter notes."""
        return int(round(b * self.bar_ticks + beat * Q))

    def tempo(self, tick, bpm):
        self.tempos.append((int(tick), bpm))

    def rit(self, t0, t1, bpm0, bpm1, steps=8):
        for k in range(steps + 1):
            self.tempo(t0 + (t1 - t0) * k // steps, bpm0 + (bpm1 - bpm0) * k / float(steps))

    def track(self, name, ch, program, vol=100, pan=64, reverb=40, bends=False):
        t = Track(self, name, ch)
        self.tracks.append(t)
        if ch not in self.setup_done:
            self.setup_done.add(ch)
            t.cc(0, 121, 0)                 # reset all controllers first
            t.pc(0, program)
            t.cc(0, 7, vol)
            t.cc(0, 10, pan)
            t.cc(0, 11, 127)
            t.cc(0, 91, reverb)
            t.cc(0, 93, 0)
            t.cc(0, 64, 0)
            t.cc(0, 1, 0)
            if bends:
                t.cc(0, 101, 0)             # RPN 0 = pitch bend range
                t.cc(0, 100, 0)
                t.cc(0, 6, 2)
                t.cc(0, 38, 0)
                t.cc(0, 101, 127)
                t.cc(0, 100, 127)
            t.bend(0, 8192)
        return t

    def drums(self, name='Drums', vol=110, reverb=25):
        return self.track(name, 9, 0, vol, 64, reverb)

    def finish(self, tick):
        self.end_tick = int(tick)

    # -- cleanup: no same-pitch overlaps on a channel ----------------------
    def _trim(self):
        by_key = {}
        for tr in self.tracks:
            for note in tr.notes:
                by_key.setdefault((tr.ch, note[2]), []).append(note)
        dropped = 0
        for key, notes in by_key.items():
            notes.sort(key=lambda x: x[0])
            for a, b in zip(notes, notes[1:]):
                if a[0] + a[1] >= b[0]:
                    a[1] = b[0] - 1 - a[0]
                    if a[1] < 1:
                        a[1] = 0
                        dropped += 1
                        print('  dup: ch%d pitch %d at tick %d (bar %.2f)' % (key[0] + 1, key[1], b[0], b[0] / float(PPQN) / 4 + 1))
        for tr in self.tracks:
            tr.notes = [x for x in tr.notes if x[1] > 0]
        if dropped:
            print('  note: %d same-pitch duplicates dropped' % dropped)

    def write(self, path):
        self._trim()
        last = 0
        for tr in self.tracks:
            for t, d, _, _ in tr.notes:
                last = max(last, t + d)
            for t, _, _ in tr.evs:
                last = max(last, t)
        end = self.end_tick if self.end_tick is not None else last + Q
        end = max(end, last + 1)
        # conductor track
        cond = bytearray()
        evs = [(0, _meta(0x03, self.title.encode('ascii'))),
               (0, _meta(0x58, bytes([self.num, {1: 0, 2: 1, 4: 2, 8: 3}[self.den], 24, 8])))]
        for t, bpm in sorted(self.tempos):
            us = int(round(60000000.0 / bpm))
            evs.append((t, _meta(0x51, struct.pack('>I', us)[1:])))
        evs.sort(key=lambda e: e[0])
        lastt = 0
        for t, data in evs:
            cond += vlq(t - lastt) + data
            lastt = t
        cond += vlq(end - lastt) + b'\xff\x2f\x00'
        chunks = [b'MTrk' + struct.pack('>I', len(cond)) + bytes(cond)]
        chunks += [tr.build(end) for tr in self.tracks]
        data = b'MThd' + struct.pack('>IHHH', 6, 1, len(chunks), PPQN) + b''.join(chunks)
        with open(path, 'wb') as f:
            f.write(data)
        secs = self.seconds(end)
        print('%-36s %6d bytes  %d:%02d' % (path.split('\\')[-1].split('/')[-1], len(data), int(secs // 60), int(secs % 60)))
        return path

    def seconds(self, tick):
        temps = sorted(self.tempos)
        secs = 0.0
        for (t0, bpm), (t1, _) in zip(temps, temps[1:] + [(tick, 0)]):
            if t1 > tick:
                t1 = tick
            if t1 > t0:
                secs += (t1 - t0) / float(PPQN) * 60.0 / bpm
        return secs
