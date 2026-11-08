"""Procedural "skin v1" for the FM-1 Song Box UI.

Draws the bitmap layers the UI composites under its live widgets:
  skin_bg        320x240  opaque plate: glass top bar, list backdrop with stage
                          glow, brushed dock panel, recessed stepper wells and
                          play socket. No text, no icons, nothing stateful.
  skin_orb       52x52    play/stop button face (normal)
  skin_orb_pr    52x52    pressed
  skin_orb_off   52x52    disabled (not connected)
  skin_card      304x36   selected song row
  skin_card_live 304x36   selected row that is also playing

Everything is computed per pixel in float with analytic (SDF) anti-aliasing,
then dithered to RGB565 by LVGLImage.py, so gradients do not band.
Geometry below must match ui.c / ui_theme.h.
"""

import math
import random

from PIL import Image

W, H = 320, 240
TOP_H = 28
DOCK_Y = 172
WELLS = [(8, 196, 116, 38), (196, 196, 116, 38)]  # stepper pills (x, y, w, h)
SOCKET = (160, 206, 26)                           # play orb centre x, y, radius


def hexc(h):
    return ((h >> 16) & 255, (h >> 8) & 255, h & 255)


def clamp(x, a=0.0, b=1.0):
    return a if x < a else b if x > b else x


def mix(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)


def smooth(e0, e1, x):
    t = clamp((x - e0) / (e1 - e0))
    return t * t * (3 - 2 * t)


def cover(sd):
    """Coverage of a pixel by a shape, from its signed distance (1 px AA)."""
    return clamp(0.5 - sd)


def sd_rbox(px, py, cx, cy, hw, hh, r):
    qx = abs(px - cx) - (hw - r)
    qy = abs(py - cy) - (hh - r)
    return math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - r


def radial(px, py, cx, cy, r):
    """1 at the centre, 0 at radius r, smooth falloff."""
    d = math.hypot(px - cx, py - cy) / r
    return clamp(1 - d) ** 2


def to_image(pix, w, h, alpha=None):
    img = Image.new("RGBA", (w, h))
    out = img.load()
    for y in range(h):
        for x in range(w):
            c = pix[y * w + x]
            a = 255 if alpha is None else int(round(clamp(alpha[y * w + x]) * 255))
            out[x, y] = (int(round(clamp(c[0], 0, 255))), int(round(clamp(c[1], 0, 255))),
                         int(round(clamp(c[2], 0, 255))), a)
    return img


# ---------------------------------------------------------------------------
# Background plate
# ---------------------------------------------------------------------------

def _recess(c, px, py, sd, cy, half_h):
    """Carved-in look: dark floor, shadow under the top edge, lit bottom lip."""
    inside = cover(sd)
    if inside > 0:
        depth = smooth(0.0, 7.0, py - (cy - half_h))          # 0 at the top edge
        floor = mix(hexc(0x030407), hexc(0x0B0E15), depth)
        c = mix(c, floor, inside * 0.94)
    if -0.5 < sd < 2.0:
        ring = clamp(1 - abs(sd - 0.6) / 1.2)
        lower = smooth(cy - half_h * 0.2, cy + half_h, py)
        c = mix(c, hexc(0x3B465E), ring * lower * 0.85)        # lip catching light
        c = mix(c, hexc(0x020305), ring * (1 - lower) * 0.55)  # shadowed upper edge
    return c


def skin_bg():
    rnd = random.Random(1234)
    row_noise = [rnd.gauss(0, 1) for _ in range(H)]
    pix = []
    for y in range(H):
        for x in range(W):
            px, py = x + 0.5, y + 0.5
            # Base: deep blue-black, a touch lighter at the top
            c = mix(hexc(0x10141E), hexc(0x06070B), py / H)
            # Stage light rising from behind the play button (kept low in the
            # list centre so song names stay legible)
            c = mix(c, hexc(0x4A2ED6), 0.55 * radial(px, py, 160, 262, 245))
            c = mix(c, hexc(0xA24BFF), 0.30 * radial(px, py, 160, 236, 110))
            # Cool fill from the top-left, for depth and a second hue
            c = mix(c, hexc(0x1F4AB8), 0.22 * radial(px, py, -10, -10, 190))
            # Vignette
            v = ((px - 160) / 190) ** 2 + ((py - 120) / 150) ** 2
            c = mix(c, (0, 0, 0), 0.28 * clamp(v - 0.35))

            if py < TOP_H:
                # Glass strip
                panel = mix(hexc(0x1C2230), hexc(0x11151F), py / TOP_H)
                c = mix(c, panel, 0.93)
                if y == 0:
                    c = mix(c, hexc(0x313A4E), 0.9)
                if y == TOP_H - 1:
                    c = mix(c, hexc(0x04050A), 0.9)
            elif py < DOCK_Y:
                # Soft shadows: list slides under both bars
                c = mix(c, (0, 0, 0), 0.45 * clamp(1 - (py - TOP_H) / 8) ** 2)
                c = mix(c, (0, 0, 0), 0.50 * clamp(1 - (DOCK_Y - py) / 9) ** 2)
            else:
                # Brushed dock panel
                panel = mix(hexc(0x1C2232), hexc(0x0C0F16), (py - DOCK_Y) / (H - DOCK_Y))
                c = mix(c, panel, 0.90)
                brush = row_noise[y] * 1.6 + rnd.gauss(0, 0.8)
                c = (c[0] + brush, c[1] + brush, c[2] + brush * 1.1)
                c = mix(c, hexc(0x5A38E6), 0.20 * radial(px, py, 160, 206, 92))
                if y == DOCK_Y:
                    # Lit top edge: neutral at the sides, violet where the orb's light hits
                    lit = clamp(1 - abs(px - 160) / 150) ** 1.5
                    c = mix(hexc(0x39435C), hexc(0xA48CFF), 0.85 * lit)
                elif y == DOCK_Y + 1:
                    c = mix(c, hexc(0x232A3A), 0.5)
                for wx, wy, ww, wh in WELLS:
                    sd = sd_rbox(px, py, wx + ww / 2, wy + wh / 2, ww / 2, wh / 2, wh / 2)
                    if sd < 3:
                        c = _recess(c, px, py, sd, wy + wh / 2, wh / 2)
                sx, sy, sr = SOCKET
                sd = math.hypot(px - sx, py - sy) - (sr + 4)
                if sd < 3:
                    c = _recess(c, px, py, sd, sy, sr + 4)

            g = rnd.gauss(0, 0.9)   # film grain, also helps the RGB565 dither
            pix.append((c[0] + g, c[1] + g, c[2] + g))
    return to_image(pix, W, H).convert("RGB")


# ---------------------------------------------------------------------------
# Play orb
# ---------------------------------------------------------------------------

ORB_PALETTES = {
    #          light     mid       dark      bounce    rim top   spec
    "normal": (0xA992FF, 0x6B4BF2, 0x351CAE, 0xC76BFF, 0xDCD2FF, 0.55),
    "pressed": (0x8770EE, 0x5234D6, 0x28138A, 0xA055F0, 0xB9AAF5, 0.30),
    "off": (0x4B5367, 0x2D3341, 0x191D26, None, 0x5F687C, 0.16),
}


def skin_orb(kind):
    light, mid, dark, bounce, rim_top, spec = ORB_PALETTES[kind]
    n, r = 52, 26.0
    pix, alpha = [], []
    for y in range(n):
        for x in range(n):
            px, py = x + 0.5, y + 0.5
            d = math.hypot(px - r, py - r)
            sd = d - r
            t = clamp(math.hypot(px - 19, py - 15) / 46)
            c = mix(hexc(light), hexc(mid), clamp(t / 0.55)) if t < 0.55 else \
                mix(hexc(mid), hexc(dark), (t - 0.55) / 0.45)
            if bounce is not None:
                c = mix(c, hexc(bounce), 0.38 * radial(px, py, 26, 52, 24))
            e = ((px - 26) / 16.5) ** 2 + ((py - 14) / 10) ** 2
            if e < 1:
                c = mix(c, (255, 255, 255), spec * (1 - e) ** 0.8 * clamp(1 - (py - 4) / 17))
            rim = clamp(1 - abs(sd + 1.0) / 1.1)
            c = mix(c, hexc(rim_top), rim * 0.65 * clamp(1 - py / 40))
            c = mix(c, hexc(dark), rim * 0.5 * clamp((py - 20) / 32))
            pix.append(c)
            alpha.append(cover(sd))
    return to_image(pix, n, n, alpha)


# ---------------------------------------------------------------------------
# Selected row card
# ---------------------------------------------------------------------------

def skin_card(live):
    w, h, r = 304, 36, 10.0
    if live:
        top, bot, border, hi, glow = 0x2C2856, 0x1B1A3A, 0x7C5CFF, 0x6C5EC6, 0x6B4BF2
    else:
        top, bot, border, hi, glow = 0x272F43, 0x1A2031, 0x343E57, 0x4A5672, None
    pix, alpha = [], []
    for y in range(h):
        for x in range(w):
            px, py = x + 0.5, y + 0.5
            sd = sd_rbox(px, py, w / 2, h / 2, w / 2, h / 2, r)
            c = mix(hexc(top), hexc(bot), py / h)
            c = mix(c, (255, 255, 255), 0.05 * radial(px, py, 70, -12, 170))
            if glow is not None:
                c = mix(c, hexc(glow), 0.40 * radial(px, py, 16, 18, 80))
            if -2.4 < sd < -0.9 and py < 9:
                c = mix(c, hexc(hi), 0.85 * clamp(1 - py / 9))
            band = clamp(1 - abs(sd + 0.5))           # 1 px border just inside the edge
            c = mix(c, hexc(border), band * (0.85 if live else 1.0))
            pix.append(c)
            alpha.append(cover(sd))
    return to_image(pix, w, h, alpha)


# ---------------------------------------------------------------------------
# Layout guide for image generators (Gemini etc.): flat blockout at 4x
# ---------------------------------------------------------------------------

def layout_guide(scale=4):
    from PIL import ImageDraw
    img = Image.new("RGB", (W * scale, H * scale), (14, 16, 22))
    d = ImageDraw.Draw(img)
    s = scale
    d.rectangle([0, 0, W * s - 1, TOP_H * s - 1], fill=(48, 54, 68))
    d.rectangle([0, DOCK_Y * s, W * s - 1, H * s - 1], fill=(48, 54, 68))
    for wx, wy, ww, wh in WELLS:
        d.rounded_rectangle([wx * s, wy * s, (wx + ww) * s - 1, (wy + wh) * s - 1], radius=wh * s // 2,
                            fill=(6, 7, 10))
    sx, sy, sr = SOCKET
    rr = (sr + 4) * s
    d.ellipse([sx * s - rr, sy * s - rr, sx * s + rr, sy * s + rr], fill=(6, 7, 10))
    return img
