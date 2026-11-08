"""Build the host preview of the real UI code and write PNGs to docs/ui/.

Needs: cmake, ninja and a host gcc on PATH, Pillow, and the LVGL sources
fetched by one ESP-IDF build. Run from the project root:
    python tools/ui_preview/render.py
Output per scenario: <name>.png (320x240, exact panel pixels after RGB565)
and <name>@3x.png (nearest-neighbour upscale), plus overview.png.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BUILD = Path(os.environ.get("UI_PREVIEW_BUILD", HERE / "build"))  # build dir, overridable
OUT = ROOT / "docs" / "ui"


def main():
    BUILD.mkdir(exist_ok=True)
    subprocess.run(["cmake", "-G", "Ninja", "-S", str(HERE), "-B", str(BUILD), "-DCMAKE_C_COMPILER=gcc",
                    "-DCMAKE_BUILD_TYPE=Release"], check=True)
    subprocess.run(["cmake", "--build", str(BUILD)], check=True)

    frames = BUILD / "frames"
    if frames.exists():
        shutil.rmtree(frames)
    frames.mkdir()
    exe = BUILD / ("ui_preview.exe" if sys.platform == "win32" else "ui_preview")
    subprocess.run([str(exe), str(frames)], check=True)

    OUT.mkdir(parents=True, exist_ok=True)
    shots = []
    for ppm in sorted(frames.glob("*.ppm")):
        img = Image.open(ppm).convert("RGB")
        img.save(OUT / f"{ppm.stem}.png")
        img.resize((img.width * 3, img.height * 3), Image.Resampling.NEAREST).save(OUT / f"{ppm.stem}@3x.png")
        shots.append(img)
        print(f"docs/ui/{ppm.stem}.png")

    # Overview sheet at 2x, two columns
    if shots:
        s, gap, cols = 2, 24, 2
        rows = (len(shots) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * 320 * s + (cols + 1) * gap, rows * 240 * s + (rows + 1) * gap), (40, 44, 52))
        for i, img in enumerate(shots):
            x = gap + (i % cols) * (320 * s + gap)
            y = gap + (i // cols) * (240 * s + gap)
            sheet.paste(img.resize((320 * s, 240 * s), Image.Resampling.NEAREST), (x, y))
        sheet.save(OUT / "overview.png")
        print("docs/ui/overview.png")


if __name__ == "__main__":
    main()
