#!/usr/bin/env python3
"""Generates data/raw/r3d/wall_sides.png: tileable side textures for walls in the 3D view.

Original procedural pixel art (no tileset sprites involved), deterministic for a given seed.
The cell order must match "textures" in data/raw/r3d/wall_sides.json.

    python3 tools/r3d_spike/make_wall_textures.py
"""
import os
import random

from PIL import Image

CELL = 32
NAMES = ["plaster", "concrete", "brick", "wood", "logs", "rock", "metal", "glass", "stone",
         "sandbag"]


def clamp(v):
    return max(0, min(255, int(v)))


def shade(rgb, f):
    return tuple(clamp(c * f) for c in rgb)


def noisy(rng, rgb, amount):
    j = rng.uniform(-amount, amount)
    return tuple(clamp(c + j * 255) for c in rgb)


def fill(px, rng, base, amount):
    for y in range(CELL):
        for x in range(CELL):
            px[x, y] = noisy(rng, base, amount)


def plaster(px, rng):
    fill(px, rng, (196, 186, 160), 0.025)
    for _ in range(14):  # faint blotches
        x, y = rng.randrange(CELL), rng.randrange(CELL)
        px[x, y] = shade(px[x, y], 0.93)


def concrete(px, rng):
    fill(px, rng, (150, 150, 146), 0.04)
    for y in (0, 16):  # form-work seams, wrapping so the texture tiles
        for x in range(CELL):
            px[x, y] = shade(px[x, y], 0.85)
    for _ in range(6):
        x, y = rng.randrange(CELL), rng.randrange(CELL)
        px[x, y] = shade(px[x, y], 0.7)


def brick(px, rng):
    mortar = (170, 160, 145)
    for y in range(CELL):
        for x in range(CELL):
            px[x, y] = noisy(rng, mortar, 0.02)
    rows = 4
    h = CELL // rows
    for r in range(rows):
        offset = 0 if r % 2 == 0 else 8
        for b in range(2):
            base = noisy(rng, (150, 70, 52), 0.05)
            x0 = (b * 16 + offset) % CELL
            for dy in range(1, h):
                for dx in range(1, 16):
                    x = (x0 + dx) % CELL
                    y = r * h + dy
                    c = noisy(rng, base, 0.025)
                    px[x, y] = shade(c, 1.08) if dy == 1 else c


def wood(px, rng):
    boards = 4
    w = CELL // boards
    for b in range(boards):
        base = noisy(rng, (128, 92, 58), 0.05)
        for y in range(CELL):
            for dx in range(w):
                x = b * w + dx
                c = noisy(rng, base, 0.02)
                if dx == 0:
                    c = shade(c, 0.6)
                elif (y + b * 7) % 11 == 0 and dx in (2, 3):
                    c = shade(c, 0.8)  # grain
                px[x, y] = c


def logs(px, rng):
    count = 4
    h = CELL // count
    for i in range(count):
        base = noisy(rng, (112, 80, 50), 0.04)
        for dy in range(h):
            light = 0.75 + 0.4 * (1 - abs((dy - h / 2 + 0.5) / (h / 2)))
            for x in range(CELL):
                px[x, i * h + dy] = shade(noisy(rng, base, 0.02), light)


def rock(px, rng):
    fill(px, rng, (112, 108, 100), 0.06)
    for _ in range(40):
        x, y = rng.randrange(CELL), rng.randrange(CELL)
        r = rng.randrange(1, 3)
        f = rng.choice((0.8, 1.15))
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                px[(x + dx) % CELL, (y + dy) % CELL] = shade(px[(x + dx) % CELL, (y + dy) % CELL], f)


def metal(px, rng):
    for y in range(CELL):
        for x in range(CELL):
            c = noisy(rng, (126, 132, 138), 0.015)
            if x % 8 == 0:
                c = shade(c, 0.78)  # corrugation
            elif x % 8 == 1:
                c = shade(c, 1.12)
            px[x, y] = c
    for y in (4, 20):
        for x in (4, 20):
            px[x, y] = (80, 80, 84)


def glass(px, rng):
    for y in range(CELL):
        for x in range(CELL):
            c = noisy(rng, (150, 190, 205), 0.015)
            if (x + y) % 23 in (0, 1):
                c = shade(c, 1.2)  # glints
            if x in (0, 31) or y in (0, 31):
                c = (90, 96, 100)  # frame
            px[x, y] = c


def stone(px, rng):
    mortar = (120, 116, 108)
    for y in range(CELL):
        for x in range(CELL):
            px[x, y] = noisy(rng, mortar, 0.02)
    for r in range(2):
        offset = 0 if r == 0 else 8
        for b in range(2):
            base = noisy(rng, (150, 144, 132), 0.06)
            for dy in range(1, 16):
                for dx in range(1, 16):
                    px[(b * 16 + offset + dx) % CELL, r * 16 + dy] = noisy(rng, base, 0.03)


def sandbag(px, rng):
    for r in range(4):
        offset = 0 if r % 2 == 0 else 8
        for b in range(2):
            base = noisy(rng, (156, 138, 100), 0.04)
            for dy in range(8):
                for dx in range(16):
                    edge = dy in (0, 7) or dx in (0, 15)
                    c = noisy(rng, base, 0.02)
                    px[(b * 16 + offset + dx) % CELL, r * 8 + dy] = shade(c, 0.72 if edge else 1.0)


def main():
    rng = random.Random(1234)
    columns = 4
    rows = (len(NAMES) + columns - 1) // columns
    atlas = Image.new("RGBA", (columns * CELL, rows * CELL), (0, 0, 0, 0))
    for i, name in enumerate(NAMES):
        cell = Image.new("RGB", (CELL, CELL))
        globals()[name](cell.load(), rng)
        atlas.paste(cell.convert("RGBA"), ((i % columns) * CELL, (i // columns) * CELL))
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    out = os.path.join(root, "data", "raw", "r3d", "wall_sides.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    atlas.save(out)
    print(out)


if __name__ == "__main__":
    main()
