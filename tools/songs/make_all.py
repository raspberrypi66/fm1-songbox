"""make_all.py - build every song into D:\\ESP32S3-MIDI\\songs and run the checker.

    python make_all.py            # build + check
    python make_all.py --no-check # build only

Each song lives in its own song_*.py with a build(path) function; the music is written as
chord charts, grid-notation melodies and drum patterns on top of smflib.py.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(os.path.dirname(HERE)), 'songs')
sys.path.insert(0, HERE)

SONGS = [
    ('song_jazz', 'jazz - blue corner.mid'),
    ('song_bossa', 'bossa - quiet tide.mid'),
    ('song_funk', 'funk - greasy spoon.mid'),
    ('song_synthwave', 'synthwave - midnight drive.mid'),
    ('song_baroque', 'baroque - passacaglia.mid'),
    ('song_house', 'house - after hours.mid'),
    ('song_reggae', 'reggae - sunday lane.mid'),
    ('song_ballad', 'ballad - paper lanterns.mid'),
    ('song_birthday', 'Happy Birthday.mid'),
    ('song_lofi', 'lofi - late bus.mid'),
]


def main(argv):
    os.makedirs(OUT, exist_ok=True)
    paths = []
    for mod, name in SONGS:
        m = __import__(mod)
        paths.append(m.build(os.path.join(OUT, name)))
    if '--no-check' in argv:
        return 0
    print()
    return subprocess.call([sys.executable, os.path.join(HERE, 'check_songs.py')] + paths)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
