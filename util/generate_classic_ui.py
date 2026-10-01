#!/usr/bin/env python3
"""Rebuild original classic UI art and the BlockPixel font (Pillow, fontTools).

No Minecraft assets or fonts are used. All bitmap patterns are authored here.
SPDX-License-Identifier: CC0-1.0
"""

from pathlib import Path
import math
import random

from PIL import Image, ImageDraw, ImageFilter
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

ROOT = Path(__file__).resolve().parents[1]
PACK = ROOT / "textures/base/pack"

# Each row is a five-bit bitmap. Lowercase letters have their own x-height.
PATTERNS = {
    "A": [14, 17, 17, 31, 17, 17, 17], "B": [30, 17, 17, 30, 17, 17, 30],
    "C": [14, 17, 16, 16, 16, 17, 14], "D": [30, 17, 17, 17, 17, 17, 30],
    "E": [31, 16, 16, 30, 16, 16, 31], "F": [31, 16, 16, 30, 16, 16, 16],
    "G": [14, 17, 16, 23, 17, 17, 14], "H": [17, 17, 17, 31, 17, 17, 17],
    "I": [14, 4, 4, 4, 4, 4, 14], "J": [7, 2, 2, 2, 18, 18, 12],
    "K": [17, 18, 20, 24, 20, 18, 17], "L": [16, 16, 16, 16, 16, 16, 31],
    "M": [17, 27, 21, 21, 17, 17, 17], "N": [17, 25, 21, 19, 17, 17, 17],
    "O": [14, 17, 17, 17, 17, 17, 14], "P": [30, 17, 17, 30, 16, 16, 16],
    "Q": [14, 17, 17, 17, 21, 18, 13], "R": [30, 17, 17, 30, 20, 18, 17],
    "S": [15, 16, 16, 14, 1, 1, 30], "T": [31, 4, 4, 4, 4, 4, 4],
    "U": [17, 17, 17, 17, 17, 17, 14], "V": [17, 17, 17, 17, 17, 10, 4],
    "W": [17, 17, 17, 21, 21, 21, 10], "X": [17, 17, 10, 4, 10, 17, 17],
    "Y": [17, 17, 10, 4, 4, 4, 4], "Z": [31, 1, 2, 4, 8, 16, 31],
    "a": [0, 0, 14, 1, 15, 17, 15], "b": [16, 16, 30, 17, 17, 17, 30],
    "c": [0, 0, 14, 17, 16, 17, 14], "d": [1, 1, 15, 17, 17, 17, 15],
    "e": [0, 0, 14, 17, 31, 16, 14], "f": [6, 9, 8, 28, 8, 8, 8],
    "g": [0, 0, 15, 17, 15, 1, 14], "h": [16, 16, 30, 17, 17, 17, 17],
    "i": [4, 0, 12, 4, 4, 4, 14], "j": [2, 0, 6, 2, 2, 18, 12],
    "k": [16, 16, 17, 18, 28, 18, 17], "l": [12, 4, 4, 4, 4, 4, 14],
    "m": [0, 0, 26, 21, 21, 21, 21], "n": [0, 0, 30, 17, 17, 17, 17],
    "o": [0, 0, 14, 17, 17, 17, 14], "p": [0, 0, 30, 17, 30, 16, 16],
    "q": [0, 0, 15, 17, 15, 1, 1], "r": [0, 0, 22, 25, 16, 16, 16],
    "s": [0, 0, 15, 16, 14, 1, 30], "t": [8, 8, 28, 8, 8, 9, 6],
    "u": [0, 0, 17, 17, 17, 19, 13], "v": [0, 0, 17, 17, 17, 10, 4],
    "w": [0, 0, 17, 17, 21, 21, 10], "x": [0, 0, 17, 10, 4, 10, 17],
    "y": [0, 0, 17, 17, 15, 1, 14], "z": [0, 0, 31, 2, 4, 8, 31],
    "0": [14, 17, 19, 21, 25, 17, 14], "1": [4, 12, 4, 4, 4, 4, 14],
    "2": [14, 17, 1, 2, 4, 8, 31], "3": [30, 1, 1, 14, 1, 1, 30],
    "4": [2, 6, 10, 18, 31, 2, 2], "5": [31, 16, 16, 30, 1, 1, 30],
    "6": [14, 16, 16, 30, 17, 17, 14], "7": [31, 1, 2, 4, 8, 8, 8],
    "8": [14, 17, 17, 14, 17, 17, 14], "9": [14, 17, 17, 15, 1, 1, 14],
    " ": [0] * 7, "!": [4, 4, 4, 4, 4, 0, 4], "?": [14, 17, 1, 2, 4, 0, 4],
    ".": [0, 0, 0, 0, 0, 0, 4], ",": [0, 0, 0, 0, 0, 4, 8],
    ":": [0, 4, 0, 0, 4, 0, 0], ";": [0, 4, 0, 0, 4, 4, 8],
    "-": [0, 0, 0, 31, 0, 0, 0], "_": [0, 0, 0, 0, 0, 0, 31],
    "+": [0, 4, 4, 31, 4, 4, 0], "=": [0, 0, 31, 0, 31, 0, 0],
    "/": [1, 2, 2, 4, 8, 8, 16], "\\": [16, 8, 8, 4, 2, 2, 1],
    "(": [2, 4, 8, 8, 8, 4, 2], ")": [8, 4, 2, 2, 2, 4, 8],
    "[": [14, 8, 8, 8, 8, 8, 14], "]": [14, 2, 2, 2, 2, 2, 14],
    "{": [3, 4, 4, 8, 4, 4, 3], "}": [24, 4, 4, 2, 4, 4, 24],
    "<": [1, 2, 4, 8, 4, 2, 1], ">": [16, 8, 4, 2, 4, 8, 16],
    "'": [4, 4, 0, 0, 0, 0, 0], '"': [10, 10, 0, 0, 0, 0, 0],
    "*": [0, 21, 14, 31, 14, 21, 0], "#": [10, 31, 10, 10, 31, 10, 0],
    "%": [25, 26, 2, 4, 8, 11, 19], "&": [12, 18, 20, 8, 21, 18, 13],
    "@": [14, 17, 23, 21, 23, 16, 14], "$": [4, 15, 20, 14, 5, 30, 4],
    "^": [4, 10, 17, 0, 0, 0, 0], "`": [8, 4, 0, 0, 0, 0, 0],
    "|": [4, 4, 4, 4, 4, 4, 4], "~": [0, 0, 9, 22, 0, 0, 0],
}


def build_font():
    names = {c: "uni%04X" % ord(c) for c in PATTERNS}
    order = [".notdef"] + list(names.values())
    glyphs, metrics = {}, {}
    for c, name in [("?", ".notdef")] + list(names.items()):
        pen = TTGlyphPen(None)
        for row, bits in enumerate(PATTERNS[c]):
            for col in range(5):
                if bits & (16 >> col):
                    x, y = col * 100, (6 - row) * 100
                    pen.moveTo((x, y))
                    pen.lineTo((x, y + 100))
                    pen.lineTo((x + 100, y + 100))
                    pen.lineTo((x + 100, y))
                    pen.closePath()
        glyphs[name] = pen.glyph()
        metrics[name] = (400 if c == " " else 600, 0)
    fb = FontBuilder(800, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap({ord(c): name for c, name in names.items()})
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=800, descent=-100)
    fb.setupNameTable({"familyName": "BlockPixel", "styleName": "Regular",
                       "uniqueFontIdentifier": "LuantiCN BlockPixel 1.0",
                       "fullName": "BlockPixel Regular", "psName": "BlockPixel-Regular",
                       "version": "Version 1.0", "licenseDescription": "CC0-1.0"})
    fb.setupOS2(sTypoAscender=800, sTypoDescender=-100, usWinAscent=800, usWinDescent=100)
    fb.setupPost()
    # Avoid timestamp changes in regenerated assets.
    fb.setupHead(created=2082844800, modified=2082844800)
    fb.font.recalcTimestamp = False
    fb.save(ROOT / "fonts/BlockPixel-Regular.ttf")


def bitmap_text(text, scale, color):
    image = Image.new("RGBA", (len(text) * 6 * scale, 7 * scale))
    draw = ImageDraw.Draw(image)
    for i, c in enumerate(text):
        for row, bits in enumerate(PATTERNS[c]):
            for col in range(5):
                if bits & (16 >> col):
                    x, y = (i * 6 + col) * scale, row * scale
                    draw.rectangle((x, y, x + scale - 1, y + scale - 1), fill=color)
    return image


def build_logo():
    mask = bitmap_text("LUANTI", 12, "white")
    logo = Image.new("RGBA", (mask.width + 26, 116))
    alpha = mask.getchannel("A")
    # Extruded stone letters with crisp outlines, retaining the fork's identity.
    for offset in range(16, 0, -1):
        logo.paste((22, 22, 22), (offset + 5, offset + 3), alpha)
    outline = alpha.filter(ImageFilter.MaxFilter(5))
    logo.paste((8, 8, 8), (3, 1), outline)
    rng = random.Random(2109)
    stone = Image.new("RGBA", mask.size)
    for y in range(stone.height):
        for x in range(stone.width):
            value = int(235 - y * 0.9) + rng.randrange(-16, 16)
            stone.putpixel((x, y), (value, value, value, alpha.getpixel((x, y))))
    logo.alpha_composite(stone, (5, 3))
    subtitle = bitmap_text("COMMUNITY EDITION", 2, (235, 235, 235, 255))
    logo.alpha_composite(subtitle, ((logo.width - subtitle.width) // 2, 101))
    logo.save(PACK / "classic_logo.png")
    splash = bitmap_text("Block by block!", 3, (255, 255, 0, 255))
    shadow = Image.new("RGBA", (splash.width + 3, splash.height + 3))
    shadow.paste((72, 72, 0), (3, 3), splash.getchannel("A"))
    shadow.alpha_composite(splash)
    rotated = shadow.rotate(14, expand=True, resample=Image.Resampling.NEAREST)
    frames = Image.new("RGBA", (rotated.width, rotated.height * 12))
    for i in range(12):
        factor = .92 + .08 * (1 + math.sin(i * math.tau / 12)) / 2
        frame = rotated.resize((round(rotated.width * factor), round(rotated.height * factor)),
                               Image.Resampling.NEAREST)
        frames.alpha_composite(frame, ((rotated.width-frame.width)//2,
                                      i*rotated.height + (rotated.height-frame.height)//2))
    frames.save(PACK / "classic_splash.png")


def build_dirt():
    rng = random.Random(987)
    image = Image.new("RGB", (32, 32))
    for y in range(32):
        for x in range(32):
            n = rng.choice((-11, -7, -3, 0, 4, 7, 12))
            image.putpixel((x, y), (61 + n, 43 + n, 30 + n))
    image.save(PACK / "classic_dirt.png")


def build_panorama():
    # Original voxel river valley. Perspective faces are depth-sorted, then
    # lightly blurred just like a distant title-screen panorama.
    w, h = 1280, 640
    image = Image.new("RGB", (w, h))
    draw = ImageDraw.Draw(image)
    for y in range(h):
        t = y / h
        draw.line((0, y, w, y), fill=(int(112 + 44*t), int(164 + 35*t), int(216 + 10*t)))
    rng = random.Random(991)
    for _ in range(20):
        x, y = rng.randrange(w), rng.randrange(30, 150)
        draw.rectangle((x, y, x + rng.randrange(45, 190), y + 12), fill=(219, 232, 245))

    def height(x, z):
        river = 5 * math.sin(z / 15)
        return max(0, round(2 + 4 * math.sin(x / 8) * math.cos(z / 11) +
                            abs(x - river) / 7 - 4 * math.exp(-((x-river)/5)**2)))

    def project(x, y, z):
        depth = z + 28
        return (w/2 + (x-2) * 620/depth, 235 + (10-y) * 620/depth)

    faces = []
    for z in range(85, -8, -1):
        for x in range(-65, 66):
            y = height(x, z)
            top_y = max(1, y)
            n = rng.randrange(-15, 16)
            top = (57+n, 105+n, 167+n) if y < 1 else (85+n, 130+n, 54+n)
            top_points = [project(x, top_y, z), project(x+1, top_y, z),
                          project(x+1, top_y, z+1), project(x, top_y, z+1)]
            faces.append((z + 0.5, top_points, top))
            neighbor = max(1, height(x, z-1))
            if top_y > neighbor:
                pts = [project(x, top_y, z), project(x+1, top_y, z),
                       project(x+1, neighbor, z), project(x, neighbor, z)]
                faces.append((z - 0.05, pts, (92+n, 70+n, 44+n)))
            if y > 2 and rng.random() < 0.017:
                tx, tz = project(x+0.5, y, z)
                s = 620/(z+28)
                faces.append((z - 0.1, [(tx-s*.18,tz), (tx+s*.18,tz),
                                      (tx+s*.18,tz-3*s), (tx-s*.18,tz-3*s)], (85, 65, 43)))
                faces.append((z - 0.2, [(tx-1.2*s,tz-2*s), (tx+1.2*s,tz-2*s),
                                      (tx+1.2*s,tz-4.2*s), (tx-1.2*s,tz-4.2*s)], (42+n, 85+n, 36+n)))
    for _, points, color in sorted(faces, key=lambda face: -face[0]):
        draw.polygon(points, fill=color)
    image = image.filter(ImageFilter.GaussianBlur(1.6))
    # Subtle vignette keeps white text readable without hiding the landscape.
    overlay = Image.new("RGBA", image.size)
    pixels = overlay.load()
    for y in range(h):
        for x in range(w):
            edge = ((x-w/2)/(w/2))**2 + ((y-h/2)/(h/2))**2
            pixels[x, y] = (0, 0, 0, int(12 + min(edge, 2) * 26))
    image = Image.alpha_composite(image.convert("RGBA"), overlay).convert("RGB")
    image.save(PACK / "classic_panorama.png")


def build_hud():
    # New assets are fallbacks; game-supplied hotbar artwork still wins.
    for name, active in [("classic_hotbar.png", False), ("classic_hotbar_selected.png", True)]:
        im = Image.new("RGBA", (22, 22), (0, 0, 0, 220))
        d = ImageDraw.Draw(im)
        d.rectangle((1, 1, 20, 20), fill=(105, 105, 105, 220))
        d.line((1, 1, 20, 1), fill=(240, 240, 240, 255) if active else (165, 165, 165, 255))
        d.line((1, 1, 1, 20), fill=(240, 240, 240, 255) if active else (165, 165, 165, 255))
        d.line((2, 20, 20, 20), fill=(190, 190, 190, 255) if active else (45, 45, 45, 255))
        d.line((20, 2, 20, 20), fill=(190, 190, 190, 255) if active else (45, 45, 45, 255))
        d.rectangle((3, 3, 18, 18), fill=(28, 28, 28, 165) if not active else (110, 110, 110, 110))
        im.save(PACK / name)
    for name, full in [("classic_heart.png", True), ("classic_heart_gone.png", False)]:
        im = Image.new("RGBA", (9, 9))
        d = ImageDraw.Draw(im)
        shape = [(1, 1), (3, 1), (4, 2), (5, 1), (7, 1), (8, 2),
                 (8, 4), (4, 8), (0, 4), (0, 2)]
        d.polygon(shape, fill=(185, 23, 29, 255) if full else (43, 26, 26, 255),
                  outline=(0, 0, 0, 255))
        if full:
            d.line((1, 2, 2, 2), fill=(255, 182, 182, 255))
            d.point((1, 3), fill=(255, 182, 182, 255))
        im.save(PACK / name)
    for name, full in [("classic_bubble.png", True), ("classic_bubble_gone.png", False)]:
        im = Image.new("RGBA", (9, 9))
        d = ImageDraw.Draw(im)
        d.ellipse((1, 1, 7, 7), fill=(90, 171, 234, 240) if full else None,
                  outline=(15, 41, 69, 255))
        d.arc((2, 2, 6, 6), 170, 270, fill=(210, 237, 255, 255))
        im.save(PACK / name)
    for name in ["crosshair.png", "object_crosshair.png"]:
        im = Image.new("RGBA", (15, 15))
        d = ImageDraw.Draw(im)
        d.line((7, 1, 7, 13), fill=(255, 255, 255, 255))
        d.line((1, 7, 13, 7), fill=(255, 255, 255, 255))
        im.save(PACK / name)


if __name__ == "__main__":
    build_font()
    build_logo()
    build_dirt()
    build_panorama()
    build_hud()
