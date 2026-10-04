#!/usr/bin/env python3
"""Rebuild original classic UI art and BlockPixel (Pillow, fontTools, NumPy, optipng).

No Minecraft assets or fonts are used. All bitmap patterns are authored here.
SPDX-License-Identifier: CC0-1.0
"""

from pathlib import Path
import shutil
import subprocess
import math
import random

from PIL import Image, ImageDraw, ImageFilter
import numpy as np
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
    "i": [4, 0, 4, 4, 4, 4, 4], "j": [2, 0, 6, 2, 2, 18, 12],
    "k": [16, 16, 17, 18, 28, 18, 17], "l": [4, 4, 4, 4, 4, 4, 4],
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
        cols = [col for col in range(5) if any(bits & (16 >> col) for bits in PATTERNS[c])]
        left = min(cols, default=0)
        width = max(cols, default=2) - left + 1
        for row, bits in enumerate(PATTERNS[c]):
            for col in range(5):
                if bits & (16 >> col):
                    x, y = (col - left) * 100, (6 - row) * 100
                    pen.moveTo((x, y))
                    pen.lineTo((x, y + 100))
                    pen.lineTo((x + 100, y + 100))
                    pen.lineTo((x + 100, y))
                    pen.closePath()
        glyphs[name] = pen.glyph()
        metrics[name] = (400 if c == " " else (width + 1) * 100, 0)
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
    mask = Image.new("RGBA", (260, 35))
    d = ImageDraw.Draw(mask)
    for i, c in enumerate("LUANTI"):
        for row, bits in enumerate(PATTERNS[c]):
            for col in range(5):
                if bits & (16 >> col):
                    d.rectangle((i * 44 + col * 8, row * 5,
                                 i * 44 + col * 8 + 7, row * 5 + 4), fill="white")
    logo = Image.new("RGBA", (274, 44))
    alpha = mask.getchannel("A")
    # Extruded stone letters with crisp outlines, retaining the fork's identity.
    for offset in range(8, 0, -1):
        logo.paste((35 + offset * 2,) * 3, (offset + 2, offset + 1), alpha)
    outline = alpha.filter(ImageFilter.MaxFilter(3))
    logo.paste((8, 8, 8), (1, 0), outline)
    rng = random.Random(2109)
    stone = Image.new("RGBA", mask.size)
    for y in range(stone.height):
        for x in range(stone.width):
            value = int(218 - y * 1.3) + rng.randrange(-12, 13)
            stone.putpixel((x, y), (value, value, value, alpha.getpixel((x, y))))
    logo.alpha_composite(stone, (2, 1))
    logo.save(PACK / "classic_logo.png")
    subtitle = bitmap_text("COMMUNITY EDITION", 1, (235, 235, 235, 255))
    edition = Image.new("RGBA", (128, 14))
    edition.alpha_composite(subtitle, ((128 - subtitle.width) // 2, 3))
    edition.save(PACK / "classic_edition.png")
    splash = bitmap_text("Block by block!", 2, (255, 255, 0, 255))
    shadow = Image.new("RGBA", (splash.width + 2, splash.height + 2))
    shadow.paste((63, 63, 0), (2, 2), splash.getchannel("A"))
    shadow.alpha_composite(splash)
    rotated = shadow.rotate(20, expand=True, resample=Image.Resampling.NEAREST)
    frames = Image.new("RGBA", (160, 64 * 16))
    for i in range(16):
        factor = .76 + .04 * (1 + math.sin(i * math.tau / 16)) / 2
        frame = rotated.resize((round(rotated.width * factor), round(rotated.height * factor)),
                               Image.Resampling.NEAREST)
        assert frame.width <= 160 and frame.height <= 64
        frames.alpha_composite(frame, ((160-frame.width)//2, i*64 + (64-frame.height)//2))
    frames.save(PACK / "classic_splash.png")


def build_dirt():
    rng = random.Random(987)
    image = Image.new("RGB", (32, 32))
    for y in range(32):
        for x in range(32):
            n = rng.choice((-9, -5, -3, 0, 4, 7, 12))
            n += int(6 * math.sin(x / 2) * math.cos(y / 3))
            image.putpixel((x, y), (62 + n, 44 + n, 31 + n))
    image.save(PACK / "classic_dirt.png")


def build_widgets():
    # Original, deterministic pixel textures. The three rows are normal,
    # keyboard/mouse highlight, and disabled; no desktop gradients.
    image = Image.new("RGBA", (200, 60))
    rng = random.Random(1201)
    for state in range(3):
        tile = Image.new("RGBA", (200, 20), (0, 0, 0, 255))
        for y in range(1, 19):
            for x in range(1, 199):
                gray = (55 if state == 2 else 112) + rng.choice((-3, -1, 0, 1, 3))
                tile.putpixel((x, y), (gray, gray, gray, 255))
        d = ImageDraw.Draw(tile)
        if state == 2:
            d.rectangle((1, 1, 198, 18), outline=(80, 80, 80, 255))
        else:
            d.line((1, 1, 198, 1), fill=(170, 170, 170, 255))
            d.line((1, 1, 1, 18), fill=(170, 170, 170, 255))
            d.line((1, 18, 198, 18), fill=(54, 54, 54, 255))
            d.line((198, 1, 198, 18), fill=(54, 54, 54, 255))
        if state == 1:
            d.rectangle((0, 0, 199, 19), outline="white")
        image.alpha_composite(tile, (0, state * 20))
    image.save(PACK / "classic_button.png")
    globe = Image.new("RGBA", (16, 16))
    d = ImageDraw.Draw(globe)
    d.ellipse((1, 1, 14, 14), outline="white")
    d.ellipse((5, 1, 10, 14), outline="white")
    d.line((1, 7, 14, 7), fill="white")
    d.line((3, 4, 12, 4), fill="white")
    d.line((3, 11, 12, 11), fill="white")
    globe.save(PACK / "classic_language.png")
    access = Image.new("RGBA", (16, 16))
    d = ImageDraw.Draw(access)
    d.rectangle((7, 1, 9, 3), fill="white")
    d.line((3, 5, 13, 5), fill="white", width=2)
    d.rectangle((7, 5, 9, 9), fill="white")
    d.line((7, 9, 5, 14), fill="white", width=2)
    d.line((9, 9, 11, 14), fill="white", width=2)
    access.save(PACK / "classic_accessibility.png")


def build_panorama():
    # Cast perspective rays through a complete voxel volume. The old polygon
    # painter omitted side faces and stopped before the foreground, leaving
    # blue holes and incorrect occlusion. Camera roll is always zero.
    w, h = 640, 360
    nx, ny, nz = 160, 48, 192
    xs, zs = np.meshgrid(np.arange(nx), np.arange(nz), indexing="ij")
    river = 80 + 7 * np.sin(zs / 25)
    bank = np.maximum(0, np.abs(xs - river) - 5)
    heights = np.clip(np.rint(4 + bank * .24 +
        2 * np.sin(xs / 11) * np.cos(zs / 17) +
        2 * np.sin(zs / 29)), 2, 23).astype(np.int32)
    levels = np.arange(ny)[None, :, None]
    world = np.where(levels <= heights[:, None, :], 2, 0).astype(np.uint8)
    world[xs, heights, zs] = np.where(heights <= 7, 6, 1)
    world[(world == 0) & (levels <= 6)] = 3

    # Materials: grass, soil, water, wood, foliage, sand, cloud.
    rng = random.Random(991)
    for _ in range(300):
        x, z = rng.randrange(5, nx - 5), rng.randrange(24, nz - 5)
        ground = int(heights[x, z])
        if ground <= 8 or abs(x - river[x, z]) < 10:
            continue
        top = ground + rng.randrange(5, 8)
        world[x, ground + 1:top + 1, z] = 4
        for y in range(top - 2, top + 3):
            radius = 1 if y == top + 2 else 2
            crown = world[x-radius:x+radius+1, y, z-radius:z+radius+1]
            crown[crown == 0] = 5
    for _ in range(18):
        x, z = rng.randrange(4, nx - 20), rng.randrange(50, nz - 20)
        world[x:x+rng.randrange(8, 19), 35:37, z:z+rng.randrange(5, 12)] = 7

    u, v = np.meshgrid((np.arange(w) + .5) / w * 2 - 1,
                       1 - (np.arange(h) + .5) / h * 2)
    fov = math.tan(math.radians(72) / 2)
    pitch = math.radians(8)
    screen_y = v * fov * h / w
    directions = np.stack((u * fov,
        screen_y * math.cos(pitch) - math.sin(pitch),
        math.cos(pitch) + screen_y * math.sin(pitch)), axis=-1).reshape(-1, 3)
    directions /= np.linalg.norm(directions, axis=1)[:, None]
    origin = np.array([82.5, 16.5, 12.5])
    count = w * h
    cells = np.broadcast_to(np.floor(origin).astype(np.int32), (count, 3)).copy()
    steps = np.sign(directions).astype(np.int32)
    deltas = np.full_like(directions, np.inf)
    np.divide(1, np.abs(directions), out=deltas, where=directions != 0)
    crossing = (np.where(steps > 0, cells + 1 - origin, origin - cells)) * deltas
    crossing[directions == 0] = np.inf
    material = np.zeros(count, dtype=np.uint8)
    distance = np.zeros(count)
    axis_hit = np.ones(count, dtype=np.int32)
    active = np.arange(count)
    bounds = np.array([nx, ny, nz])

    # DDA visits each ray's nearest next voxel boundary. First occupied voxel
    # wins, so every visible face has correct perspective and occlusion.
    for _ in range(nx + ny + nz):
        if active.size == 0:
            break
        axes = crossing[active].argmin(axis=1)
        travel = crossing[active, axes].copy()
        cells[active, axes] += steps[active, axes]
        crossing[active, axes] += deltas[active, axes]
        inside = ((cells[active] >= 0) & (cells[active] < bounds)).all(axis=1)
        active, axes, travel = active[inside], axes[inside], travel[inside]
        cell = cells[active]
        types = world[cell[:, 0], cell[:, 1], cell[:, 2]]
        hit = types != 0
        pixels = active[hit]
        material[pixels] = types[hit]
        distance[pixels] = travel[hit]
        axis_hit[pixels] = axes[hit]
        active = active[~hit]
    assert active.size == 0, "Incomplete voxel traversal"
    assert np.all(material.reshape(h, w)[3*h//4:] != 0), "Uncovered foreground"

    sky_t = np.clip(directions[:, 1] * 1.4 + .2, 0, 1)[:, None]
    horizon = np.array([184, 210, 230])
    sky = horizon * (1 - sky_t) + np.array([88, 151, 216]) * sky_t
    palette = np.array([[0, 0, 0], [105, 158, 65], [116, 85, 53],
        [56, 126, 177], [96, 70, 42], [63, 125, 43], [193, 183, 129], [244, 248, 250]])
    colors = palette[material].astype(float)
    point = origin + directions * distance[:, None]
    # Grass occupies only the upper edge of a vertical soil face.
    side = (material == 1) & (axis_hit != 1) & (point[:, 1] % 1 < .78)
    colors[side] = palette[2]
    normal = np.zeros_like(directions)
    normal[np.arange(count), axis_hit] = -steps[np.arange(count), axis_hit]
    sunlight = normal @ np.array([-.45, .8, -.35])
    colors *= (.56 + .44 * np.maximum(sunlight, 0))[:, None]
    texture_cell = np.floor(point * 8).astype(np.int32)
    noise = ((texture_cell[:, 0] * 37 ^ texture_cell[:, 1] * 23 ^
              texture_cell[:, 2] * 47) % 17 - 8)[:, None]
    colors += noise
    water = material == 3
    colors[water] = palette[3] + noise[water] * .6
    fog = (np.clip(distance / 150, 0, 1) ** 1.6 * .8)[:, None]
    colors = colors * (1 - fog) + horizon * fog
    colors[material == 0] = sky[material == 0]
    vignette = (1 - .1 * (u * u + v * v)).reshape(-1, 1)
    pixels = np.clip(colors * vignette * .91, 0, 255).astype(np.uint8).reshape(h, w, 3)
    image = Image.fromarray(pixels).resize((1280, 720), Image.Resampling.NEAREST)
    image.filter(ImageFilter.GaussianBlur(1.8)).save(PACK / "classic_panorama.png")


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
    if not shutil.which("optipng"):
        raise SystemExit("Resource generation requires optipng (PNG CI optimization).")
    build_font()
    build_logo()
    build_dirt()
    build_widgets()
    build_panorama()
    Image.open(PACK / "classic_panorama.png").crop((160, 40, 800, 680)).resize(
        (64, 64), Image.Resampling.NEAREST).save(PACK / "classic_world.png")
    build_hud()

    for path in sorted(PACK.glob("classic_*.png")) + [PACK / "crosshair.png", PACK / "object_crosshair.png"]:
        subprocess.run(["optipng", "-nc", "-strip", "all", "-clobber", str(path)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
