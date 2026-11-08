"""Generate the UI bitmap assets and convert them to LVGL v9 C arrays.

    python tools/ui_assets/make_assets.py                 # draw all PNGs, then convert
    python tools/ui_assets/make_assets.py --convert-only  # keep the PNGs as they are
                                                          # (hand-made / AI-made skin art)

Sources (exact on-screen pixel size):
  assets/icon_play.png, icon_stop.png   A8       alpha only, tinted in code
  assets/logo_mark.png                  RGB565A8
  assets/skin/skin_*.png                skin layers (see skin_gen.py), RGB565 /
                                        RGB565A8 with ordered dither (no banding)
  assets/skin/skin_bg_art.(png|jpg|jfif|webp)
                                        optional artwork for the background plate
                                        (e.g. from an image generator, any 4:3 size).
                                        If present it replaces the procedural plate.
Output: main/ui/assets/img_<name>.c

With --convert-only, a PNG with the right aspect ratio but a larger size (for
example a 1280x960 render from an image generator) is downscaled first.

Needs Pillow, pypng and lz4 (LVGLImage.py). Run from the project root.
"""

import argparse
import math
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
import skin_gen  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
SRC_DIR = ROOT / "assets"
SKIN_DIR = SRC_DIR / "skin"
OUT_DIR = ROOT / "main" / "ui" / "assets"
LVGL_IMAGE = ROOT / "managed_components" / "lvgl__lvgl" / "scripts" / "LVGLImage.py"

SS = 16  # supersampling factor for the icon shapes

# Keep in sync with main/ui/ui_theme.h
ACCENT_TOP = (0x9A, 0x82, 0xFF)
ACCENT_BOT = (0x5E, 0x3E, 0xEB)


def downsample(img, w, h):
    return img.resize((w, h), Image.Resampling.BOX)


def inset_polygon(pts, r):
    """Offset each edge of a convex polygon inward by r, return new vertices."""
    n = len(pts)
    cx = sum(p[0] for p in pts) / n
    cy = sum(p[1] for p in pts) / n
    lines = []
    for i in range(n):
        (x1, y1), (x2, y2) = pts[i], pts[(i + 1) % n]
        dx, dy = x2 - x1, y2 - y1
        ln = math.hypot(dx, dy)
        nx, ny = -dy / ln, dx / ln
        if (cx - x1) * nx + (cy - y1) * ny < 0:
            nx, ny = -nx, -ny
        lines.append(((x1 + nx * r, y1 + ny * r), (dx, dy)))
    out = []
    for i in range(n):
        (p, d), (q, e) = lines[i - 1], lines[i]
        det = d[0] * (-e[1]) - d[1] * (-e[0])
        t = ((q[0] - p[0]) * (-e[1]) - (q[1] - p[1]) * (-e[0])) / det
        out.append((p[0] + t * d[0], p[1] + t * d[1]))
    return out


def rounded_polygon_mask(w, h, pts, r):
    """Minkowski sum of the inset polygon and a disk: round corners, straight
    edges exactly on the original edges."""
    big = Image.new("L", (w * SS, h * SS), 0)
    d = ImageDraw.Draw(big)
    ins = [(x * SS, y * SS) for x, y in inset_polygon(pts, r)]
    rs = r * SS
    d.polygon(ins, fill=255)
    for i in range(len(ins)):
        a, b = ins[i], ins[(i + 1) % len(ins)]
        d.line([a, b], fill=255, width=int(round(2 * rs)))
    for x, y in ins:
        d.ellipse([x - rs, y - rs, x + rs, y + rs], fill=255)
    return downsample(big, w, h)


def alpha_png(mask):
    img = Image.new("RGBA", mask.size, (255, 255, 255, 0))
    img.putalpha(mask)
    return img


def make_play():
    # 20x22. The midpoint between the triangle's bounding-box centre and its
    # centroid sits on the canvas centre: optically centred in a round button.
    pts = [(3.0, 1.5), (3.0, 20.5), (19.5, 11.0)]
    return alpha_png(rounded_polygon_mask(20, 22, pts, 2.2))


def make_stop():
    w = h = 16
    big = Image.new("L", (w * SS, h * SS), 0)
    ImageDraw.Draw(big).rounded_rectangle([1 * SS, 1 * SS, 15 * SS - 1, 15 * SS - 1], radius=3 * SS, fill=255)
    return alpha_png(downsample(big, w, h))


def make_logo():
    # 18x18 rounded square, diagonal accent gradient, one white sine period
    w = h = 18
    bw, bh = w * SS, h * SS
    grad = Image.new("RGB", (bw, bh))
    px = grad.load()
    for y in range(bh):
        for x in range(bw):
            t = (x + y) / (bw + bh - 2)
            px[x, y] = tuple(int(round(a + (b - a) * t)) for a, b in zip(ACCENT_TOP, ACCENT_BOT))
    shape = Image.new("L", (bw, bh), 0)
    ImageDraw.Draw(shape).rounded_rectangle([0, 0, bw - 1, bh - 1], radius=5 * SS, fill=255)

    wave = Image.new("L", (bw, bh), 0)
    wd = ImageDraw.Draw(wave)
    x0, x1, cy, amp, stroke = 3.6, 14.4, 9.0, 3.1, 1.75
    pts = []
    steps = 200
    for i in range(steps + 1):
        x = x0 + (x1 - x0) * i / steps
        y = cy - amp * math.sin(2 * math.pi * (x - x0) / (x1 - x0))
        pts.append((x * SS, y * SS))
    sw = stroke * SS
    wd.line(pts, fill=255, width=int(round(sw)), joint="curve")
    for x, y in (pts[0], pts[-1]):
        wd.ellipse([x - sw / 2, y - sw / 2, x + sw / 2, y + sw / 2], fill=255)

    white = Image.new("RGB", (bw, bh), (255, 255, 255))
    rgb = Image.composite(white, grad, wave)
    img = rgb.convert("RGBA")
    img.putalpha(shape)
    # premultiplied downsample: no dark fringe on the edge
    r, g, b, a = img.split()
    prem = Image.merge("RGB", [Image.composite(c, Image.new("L", c.size, 0), a) for c in (r, g, b)])
    prem_s = downsample(prem, w, h)
    a_s = downsample(a, w, h)
    out = Image.new("RGBA", (w, h))
    ps, als, op = prem_s.load(), a_s.load(), out.load()
    for y in range(h):
        for x in range(w):
            al = als[x, y]
            op[x, y] = (0, 0, 0, 0) if al == 0 else \
                tuple(min(255, int(round(v * 255 / al))) for v in ps[x, y]) + (al,)
    return out


ART_EXTS = (".png", ".jpg", ".jpeg", ".jfif", ".webp")


def art_source(stem):
    for ext in ART_EXTS:
        p = SKIN_DIR / f"{stem}{ext}"
        if p.exists():
            return p
    return None


def make_skin_bg():
    """Background plate: artwork if supplied, otherwise the procedural one."""
    art = art_source("skin_bg_art")
    if art is None:
        return skin_gen.skin_bg()
    img = Image.open(art).convert("RGB")
    w, h = skin_gen.W, skin_gen.H
    if abs(img.width / img.height - w / h) > 0.02:
        raise SystemExit(f"{art}: {img.width}x{img.height} is not 4:3, crop it first")
    print(f"  using artwork {art.relative_to(ROOT)} ({img.width}x{img.height})")
    # Lanczos with the full support of the downscale: no aliasing of JPEG noise
    return img.resize((w, h), Image.Resampling.LANCZOS)


# name, maker, folder, colour format, dither, size
ASSETS = [
    ("icon_play", make_play, SRC_DIR, "A8", False, (20, 22)),
    ("icon_stop", make_stop, SRC_DIR, "A8", False, (16, 16)),
    ("logo_mark", make_logo, SRC_DIR, "RGB565A8", False, (18, 18)),
    ("skin_bg", make_skin_bg, SKIN_DIR, "RGB565", True, (320, 240)),
    ("skin_orb", lambda: skin_gen.skin_orb("normal"), SKIN_DIR, "RGB565A8", True, (52, 52)),
    ("skin_orb_pr", lambda: skin_gen.skin_orb("pressed"), SKIN_DIR, "RGB565A8", True, (52, 52)),
    ("skin_orb_off", lambda: skin_gen.skin_orb("off"), SKIN_DIR, "RGB565A8", True, (52, 52)),
    ("skin_card", lambda: skin_gen.skin_card(False), SKIN_DIR, "RGB565A8", True, (304, 36)),
    ("skin_card_live", lambda: skin_gen.skin_card(True), SKIN_DIR, "RGB565A8", True, (304, 36)),
]


def fit(png_path, size, tmp):
    """Return a path to a PNG of exactly `size`, downscaling if needed."""
    img = Image.open(png_path)
    if img.size == tuple(size):
        return png_path
    w, h = size
    if abs(img.width / img.height - w / h) > 0.02:
        raise SystemExit(f"{png_path}: aspect {img.width}x{img.height} does not match {w}x{h}")
    print(f"  resizing {img.width}x{img.height} -> {w}x{h}")
    out = Path(tmp) / png_path.name
    img.convert("RGBA").resize((w, h), Image.Resampling.LANCZOS).save(out)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--convert-only", action="store_true", help="do not redraw the PNGs")
    args = ap.parse_args()

    SKIN_DIR.mkdir(parents=True, exist_ok=True)
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    if not args.convert_only:
        skin_gen.layout_guide().save(SKIN_DIR / "layout_guide.png")
    with tempfile.TemporaryDirectory() as tmp:
        for name, maker, folder, cf, dither, size in ASSETS:
            png_path = folder / f"{name}.png"
            if not args.convert_only:
                maker().save(png_path)
                print(f"drew {png_path.relative_to(ROOT)}")
            src = fit(png_path, size, tmp)
            cmd = [sys.executable, str(LVGL_IMAGE), "--ofmt", "C", "--cf", cf, "--name", f"img_{name}",
                   "-o", str(OUT_DIR), str(src)]
            if dither:
                cmd.insert(-1, "--rgb565dither")
            subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print(f"  -> main/ui/assets/img_{name}.c ({cf}{', dithered' if dither else ''})")


if __name__ == "__main__":
    main()
