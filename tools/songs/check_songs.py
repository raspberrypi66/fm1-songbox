"""check_songs.py - validate every .mid in D:\\ESP32S3-MIDI\\songs (or the paths given).

Reports per file: format, tracks, PPQN, tempo(s), duration, channels + programs,
peak polyphony (drum notes counted with their written length), same-pitch overlaps,
unmatched Note On/Off, notes outside 0..127, missing tick-0 setup, SysEx, size.
Exit code 1 if any file violates a rule.
"""
import glob
import os
import struct
import sys

GM = ['Acoustic Grand Piano', 'Bright Acoustic Piano', 'Electric Grand Piano', 'Honky-tonk Piano',
      'Electric Piano 1', 'Electric Piano 2', 'Harpsichord', 'Clavinet', 'Celesta', 'Glockenspiel',
      'Music Box', 'Vibraphone', 'Marimba', 'Xylophone', 'Tubular Bells', 'Dulcimer', 'Drawbar Organ',
      'Percussive Organ', 'Rock Organ', 'Church Organ', 'Reed Organ', 'Accordion', 'Harmonica',
      'Tango Accordion', 'Nylon Guitar', 'Steel Guitar', 'Jazz Guitar', 'Clean Guitar', 'Muted Guitar',
      'Overdriven Guitar', 'Distortion Guitar', 'Guitar Harmonics', 'Acoustic Bass', 'Finger Bass',
      'Pick Bass', 'Fretless Bass', 'Slap Bass 1', 'Slap Bass 2', 'Synth Bass 1', 'Synth Bass 2',
      'Violin', 'Viola', 'Cello', 'Contrabass', 'Tremolo Strings', 'Pizzicato Strings', 'Harp', 'Timpani',
      'String Ensemble 1', 'String Ensemble 2', 'Synth Strings 1', 'Synth Strings 2', 'Choir Aahs',
      'Voice Oohs', 'Synth Voice', 'Orchestra Hit', 'Trumpet', 'Trombone', 'Tuba', 'Muted Trumpet',
      'French Horn', 'Brass Section', 'Synth Brass 1', 'Synth Brass 2', 'Soprano Sax', 'Alto Sax',
      'Tenor Sax', 'Baritone Sax', 'Oboe', 'English Horn', 'Bassoon', 'Clarinet', 'Piccolo', 'Flute',
      'Recorder', 'Pan Flute', 'Blown Bottle', 'Shakuhachi', 'Whistle', 'Ocarina', 'Lead 1 (square)',
      'Lead 2 (sawtooth)', 'Lead 3 (calliope)', 'Lead 4 (chiff)', 'Lead 5 (charang)', 'Lead 6 (voice)',
      'Lead 7 (fifths)', 'Lead 8 (bass+lead)', 'Pad 1 (new age)', 'Pad 2 (warm)', 'Pad 3 (polysynth)',
      'Pad 4 (choir)', 'Pad 5 (bowed)', 'Pad 6 (metallic)', 'Pad 7 (halo)', 'Pad 8 (sweep)', 'FX 1 (rain)',
      'FX 2 (soundtrack)', 'FX 3 (crystal)', 'FX 4 (atmosphere)', 'FX 5 (brightness)', 'FX 6 (goblins)',
      'FX 7 (echoes)', 'FX 8 (sci-fi)', 'Sitar', 'Banjo', 'Shamisen', 'Koto', 'Kalimba', 'Bag pipe',
      'Fiddle', 'Shanai', 'Tinkle Bell', 'Agogo', 'Steel Drums', 'Woodblock', 'Taiko Drum', 'Melodic Tom',
      'Synth Drum', 'Reverse Cymbal', 'Guitar Fret Noise', 'Breath Noise', 'Seashore', 'Bird Tweet',
      'Telephone Ring', 'Helicopter', 'Applause', 'Gunshot']


def vlq(data, i):
    v = 0
    while True:
        b = data[i]
        i += 1
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            return v, i


def parse(data):
    if data[:4] != b'MThd':
        raise ValueError('no MThd')
    hlen = struct.unpack('>I', data[4:8])[0]
    fmt, ntrks, div = struct.unpack('>HHH', data[8:14])
    pos = 8 + hlen
    tracks = []
    sysex = 0
    while pos + 8 <= len(data) and len(tracks) < ntrks:
        if data[pos:pos + 4] != b'MTrk':
            raise ValueError('bad chunk at %d' % pos)
        tlen = struct.unpack('>I', data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + tlen]
        pos += 8 + tlen
        evs = []
        i, tick, running, name = 0, 0, None, ''
        while i < len(body):
            delta, i = vlq(body, i)
            tick += delta
            b = body[i]
            if b == 0xFF:
                kind = body[i + 1]
                mlen, j = vlq(body, i + 2)
                payload = body[j:j + mlen]
                i = j + mlen
                if kind == 0x03:
                    name = payload.decode('latin1')
                evs.append((tick, 'meta', kind, payload))
                if kind == 0x2F:
                    break
                continue
            if b in (0xF0, 0xF7):
                slen, j = vlq(body, i + 1)
                i = j + slen
                sysex += 1
                continue
            if b & 0x80:
                running = b
                i += 1
            elif running is None:
                raise ValueError('data byte without status')
            st = running
            hi = st & 0xF0
            if hi in (0xC0, 0xD0):
                evs.append((tick, 'ch', st, body[i], 0))
                i += 1
            else:
                evs.append((tick, 'ch', st, body[i], body[i + 1]))
                i += 2
        tracks.append((name, evs, tick))
    return fmt, ntrks, div, tracks, sysex


def check(path, verbose=True):
    data = open(path, 'rb').read()
    fmt, ntrks, div, tracks, sysex = parse(data)
    problems = []
    if div & 0x8000:
        problems.append('SMPTE timebase')
    if fmt > 1:
        problems.append('format %d' % fmt)
    if sysex:
        problems.append('%d SysEx events' % sysex)
    # merge
    merged = []
    for ti, (name, evs, end) in enumerate(tracks):
        for e in evs:
            merged.append((e[0], 0 if (e[1] == 'ch' and e[2] & 0xF0 == 0x80) or (e[1] == 'ch' and e[2] & 0xF0 == 0x90 and e[4] == 0) else 1, ti, e))
    merged.sort(key=lambda m: (m[0], m[1]))
    tempos = []
    timesig = None
    bar_beats = 4
    programs = {}
    setup = {}
    channels = set()
    sounding = {}          # (ch, pitch) -> start tick
    active = []            # list of (end_tick) for polyphony? we compute by events instead
    overlaps, unmatched_off, bad_range = 0, 0, 0
    poly, peak, peak_tick = 0, 0, 0
    notes = 0
    for tick, _, ti, e in merged:
        if e[1] == 'meta':
            if e[2] == 0x51:
                us = (e[3][0] << 16) | (e[3][1] << 8) | e[3][2]
                tempos.append((tick, round(60000000.0 / us, 2)))
            elif e[2] == 0x58:
                timesig = '%d/%d' % (e[3][0], 1 << e[3][1])
                bar_beats = e[3][0] * 4.0 / (1 << e[3][1])
            continue
        st, d1, d2 = e[2], e[3], e[4]
        ch, hi = st & 0x0F, st & 0xF0
        channels.add(ch)
        if hi == 0xC0:
            programs.setdefault(ch, []).append((tick, d1))
            if tick == 0:
                setup.setdefault(ch, set()).add('pc')
        elif hi == 0xB0 and tick == 0:
            setup.setdefault(ch, set()).add(d1)
        elif hi == 0xE0 and tick == 0:
            setup.setdefault(ch, set()).add('bend')
        is_on = hi == 0x90 and d2 > 0
        is_off = hi == 0x80 or (hi == 0x90 and d2 == 0)
        if is_on:
            notes += 1
            if not 0 <= d1 <= 127:
                bad_range += 1
            if (ch, d1) in sounding:
                overlaps += 1
            else:
                poly += 1
            sounding[(ch, d1)] = tick
            if poly > peak:
                peak, peak_tick = poly, tick
        elif is_off:
            if (ch, d1) in sounding:
                del sounding[(ch, d1)]
                poly -= 1
            else:
                unmatched_off += 1
    unmatched_on = len(sounding)
    last_tick = max(t[2] for t in tracks)
    # duration with the tempo map
    temps = sorted(tempos) or [(0, 120.0)]
    if temps[0][0] != 0:
        temps.insert(0, (0, 120.0))
    secs = 0.0
    for (t0, bpm), (t1, _) in zip(temps, temps[1:] + [(last_tick, 0)]):
        if t1 > t0:
            secs += (t1 - t0) / float(div) * 60.0 / bpm
    missing = []
    for ch in sorted(channels):
        got = setup.get(ch, set())
        need = {'pc', 7, 10, 11, 91, 64}
        if ch == 9:
            need = {7, 10, 11, 91, 64}
        if not need <= got:
            missing.append('ch%d:%s' % (ch + 1, ','.join(str(x) for x in sorted(need - got, key=str))))
    if overlaps:
        problems.append('%d same-pitch overlaps' % overlaps)
    if unmatched_on or unmatched_off:
        problems.append('unmatched on=%d off=%d' % (unmatched_on, unmatched_off))
    if bad_range:
        problems.append('%d notes out of range' % bad_range)
    if peak > 16:
        problems.append('peak polyphony %d > 16' % peak)
    if missing:
        problems.append('missing tick-0 setup: ' + ' '.join(missing))
    if len(data) > 100 * 1024:
        problems.append('file larger than 100 KB')
    if verbose:
        print('== %s' % os.path.basename(path))
        print('   format %d, %d tracks, PPQN %d, %s, %d bytes, %d notes' % (fmt, ntrks, div, timesig, len(data), notes))
        tl = ['%g' % b for _, b in temps]
        if len(tl) > 6:
            tl = tl[:3] + ['...'] + tl[-3:]
        print('   tempo: %s   duration %d:%02d' % (' '.join(tl), int(secs // 60), int(round(secs % 60))))
        for ch in sorted(channels):
            progs = programs.get(ch, [])
            desc = 'drums (GM kit)' if ch == 9 else ', '.join('%d %s' % (p, GM[p]) for _, p in progs) or 'no program!'
            print('   ch%-2d %s' % (ch + 1, desc))
        print('   peak polyphony %d at tick %d (bar %.1f)' % (peak, peak_tick, peak_tick / float(div) / bar_beats + 1))
        print('   overlaps %d, unmatched on %d / off %d, out of range %d' % (overlaps, unmatched_on, unmatched_off, bad_range))
        print('   %s' % ('CLEAN' if not problems else 'PROBLEMS: ' + '; '.join(problems)))
    return problems, secs, peak


def main(argv):
    paths = argv[1:] or sorted(glob.glob(r'D:\ESP32S3-MIDI\songs\*.mid'))
    bad = 0
    for p in paths:
        try:
            problems, _, _ = check(p)
        except Exception as ex:   # noqa
            print('== %s\n   PARSE ERROR: %s' % (os.path.basename(p), ex))
            problems = ['parse error']
        bad += bool(problems)
    print('\n%d file(s), %d with problems' % (len(paths), bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
