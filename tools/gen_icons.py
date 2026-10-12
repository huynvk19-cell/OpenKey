#!/usr/bin/env python3
"""Generate NhaKey's minimal icons (app icon + tray status icons).

Usage: python3 tools/gen_icons.py Sources/OpenKey/win32/OpenKey/OpenKey
"""
import sys
from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
BLUE = (10, 132, 255, 255)     # Vietnamese on
GRAY = (142, 142, 147, 255)    # English
WHITE = (255, 255, 255, 255)
SIZES = [16, 20, 24, 32, 40, 48, 64, 256]


def draw(size, letter, fill, text, outline=False):
    scale = 8  # draw big, then shrink for smooth edges
    s = size * scale
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    pad = s * 0.06
    radius = s * 0.24
    box = (pad, pad, s - pad, s - pad)
    if outline:
        d.rounded_rectangle(box, radius=radius, outline=fill, width=int(s * 0.09))
    else:
        d.rounded_rectangle(box, radius=radius, fill=fill)
    font = ImageFont.truetype(FONT, int(s * 0.62))
    l, t, r, b = d.textbbox((0, 0), letter, font=font)
    d.text(((s - (r - l)) / 2 - l, (s - (b - t)) / 2 - t), letter, font=font, fill=text)
    return img.resize((size, size), Image.LANCZOS)


def save(path, letter, fill, text, outline=False):
    images = [draw(sz, letter, fill, text, outline) for sz in SIZES]
    images[-1].save(path, format="ICO", sizes=[(sz, sz) for sz in SIZES], append_images=images[:-1])


def main(out):
    save(f"{out}/icon.ico", "N", BLUE, WHITE)
    save(f"{out}/StatusViet.ico", "V", BLUE, WHITE)
    save(f"{out}/StatusEng.ico", "E", GRAY, WHITE)
    # "modern icon" option: white, to match the Windows taskbar
    save(f"{out}/StatusViet10.ico", "V", WHITE, (0, 0, 0, 0))
    save(f"{out}/StatusEng10.ico", "E", WHITE, WHITE, outline=True)


if __name__ == "__main__":
    main(sys.argv[1])
