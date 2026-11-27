#!/usr/bin/env python3
"""Makes OpenSimFarm's placeholder tile pack: full-colour, flat vector-style tiles at any size from 8 to 128 px,
drawn from nothing but the engine's own tile tables (nothing of the original game is read or copied).

    python3 engine/tools/make_placeholder_pack.py [--size 32] [--out engine/packs/placeholder] [--labels]

Every one of the 848 plain, 200 masked and 105 event tiles is made, in the PNG-sheet format of
engine/TRUECOLOUR.md, and so are placeholder pictures for the interface (windows, buttons, gauges, sprites, the
full-screen pictures), at 2 pixels per interface unit (--no-pictures leaves them out), and placeholder data tables
(climate, region map, town layout, message texts, item texts, overview colours; --no-tables leaves them out) and the
interface theme (16 RGB colours) and the weather look (a veil for rain and for frost), and the template farm (the item catalogue every new world starts from) and 24 crops with their pictures, the sound effects, and the Farm Expert's help cards. The families the engine knows by name (ground, water, ditches, roads, fences, gates, trees,
rocks, bridges, crop pictures, event grounds) are drawn as what they are, connected pieces with the right arms; every
other tile is coloured by the tile family it belongs to. With --labels each tile carries its number, so the pack
doubles as a map of what the engine uses where (the shipped pack is without, to play). Needs Pillow (pip install pillow).
"""
import argparse
import colorsys
import glob
import json
import math
import os
import random
import re
import struct
import sys
import wave

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import placeholder_art as art

PLAIN_TILES, MASKED_TILES, EVENT_TILES = 848, 200, 105
PLAIN_COLS, MASKED_COLS, EVENT_COLS = 32, 20, 15

# The engine's tile families (game/Shop.cpp FamilyStart): the first tile of each of the 160 families. The pictures
# of the tiles between two starts belong to one family; used here only to give neighbouring families different colours.
FAMILY_START = [
    15, 39, 87, 232, 150, 161, 440, 441, 445, 451, 460, 469, 518, 273, 282, 529, 300, 309, 688, 696, 704, 712, 720, 728,
    736, 744, 752, 760, 768, 776, 784, 792, 800, 808, 962, 978, 994, 946, 109, 110, 848, 852, 852, 880, 856, 856, 904, 904,
    904, 904, 868, 912, 860, 860, 876, 876, 122, 504, 12, 12, 1010, 120, 131, 132, 133, 134, 2, 2, 2, 2, 518, 892, 896,
    320, 915, 914, 913, 912, 177, 175, 188, 1020, 1028, 87, 98, 539, 248, 832, 833, 834, 835, 836, 837, 838, 839, 840,
    841, 842, 843, 844, 845, 846, 847, 481, 1010, 540, 1025, 1026, 69, 816, 817, 818, 819, 820, 821, 822, 823, 824, 825,
    826, 827, 828, 829, 830, 831, 916, 1025, 1026, 172, 122, 815, 816, 817, 818, 819, 820, 821, 839, 840, 841, 842, 843,
    844, 845, 846, 535, 218, 483]
BANDS = sorted(set(FAMILY_START))


def band_of(tile):
    """Index of the family band a tile id falls into (the largest start not above it)."""
    k = 0
    for i, s in enumerate(BANDS):
        if s <= tile:
            k = i
    return k


def band_colour(tile, s=0.35, v=0.78):
    h = (band_of(tile) * 0.381966) % 1.0  # golden-ratio steps: neighbouring bands never look alike
    r, g, b = colorsys.hsv_to_rgb(h, s, v)
    return (int(r * 255), int(g * 255), int(b * 255))


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def shade(c, f):
    return tuple(max(0, min(255, int(x * f))) for x in c)


# Connected pieces (roads, fences, ditches): the engine's neighbour mask N=1 E=2 S=4 W=8 gives piece 0..10
# (game/Editing.cpp kShape); the arms each piece reaches out with:
PIECE_ARMS = [
    "NS", "EW", "NE", "SE", "NES", "NW", "NEW", "SW", "NSW", "ESW", "NESW",
]
# tile families (game/Editing.cpp)
GROUND = (15, 16)
WATER = (39, 31)
DITCH_EMPTY, DITCH_FULL = (87, 11), (98, 11)
VALVE = (122, 4)
TREES = (128, 19)
PAVED, DIRT = (150, 11), (161, 11)
BRIDGE = (172, 2)
TROUGH = (175, 2)
FENCE = (177, 11)
GATE = (188, 4)
ROCKS = (535, 4)
SIGNS = 539        # 4 tiles a crop slot, a 2 x 2 sign, 16 slots
CROP_GROWTH = 688  # 8 tiles a crop slot
CROP_ICON_A, CROP_ICON_B = 816, 832
EVENT_GROUND = 483  # a 7 x 5 block
HAY = 1020

# 24 crops (16 slots, and the spare crops that can be chosen), a hue each
CROP_COLOURS = [colorsys.hsv_to_rgb(i / 24.0, 0.75, 0.9) for i in range(24)]
CROP_COLOURS = [tuple(int(x * 255) for x in c) for c in CROP_COLOURS]

# a 3 x 5 pixel digit font for the labels
DIGITS = {
    "0": "111101101101111", "1": "010110010010111", "2": "111001111100111", "3": "111001111001111",
    "4": "101101111001001", "5": "111100111001111", "6": "111100111101111", "7": "111001001001001",
    "8": "111101111101111", "9": "111101111001111",
}


class Painter:
    """Draws at 4x and shrinks, so the shapes are smooth at any tile size."""

    SS = 4

    def __init__(self, w, h, background=None):
        self.w, self.h = w, h
        self.img = Image.new("RGBA", (w * self.SS, h * self.SS), background or (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.img)

    def _p(self, v):
        return v * self.SS

    def rect(self, x0, y0, x1, y1, fill):
        self.d.rectangle([self._p(x0), self._p(y0), self._p(x1) - 1, self._p(y1) - 1], fill=fill)

    def ellipse(self, x0, y0, x1, y1, fill, outline=None, width=0):
        self.d.ellipse([self._p(x0), self._p(y0), self._p(x1), self._p(y1)], fill=fill, outline=outline, width=int(width * self.SS))

    def line(self, pts, fill, width):
        self.d.line([(self._p(x), self._p(y)) for x, y in pts], fill=fill, width=max(1, int(width * self.SS)))

    def polygon(self, pts, fill):
        self.d.polygon([(self._p(x), self._p(y)) for x, y in pts], fill=fill)

    def group(self):
        """`with p.group():` shapes drawn inside are composited over the picture with their alpha (ImageDraw alone
        would overwrite the pixels, not blend them)."""
        painter = self

        class Group:
            def __enter__(self):
                self.base = painter.d
                self.layer = Image.new("RGBA", painter.img.size, (0, 0, 0, 0))
                painter.d = ImageDraw.Draw(self.layer)

            def __exit__(self, *exc):
                painter.img = Image.alpha_composite(painter.img, self.layer)
                painter.d = ImageDraw.Draw(painter.img)
        return Group()

    def finish(self):
        return self.img.resize((self.w, self.h), Image.BOX)


def speckles(p, size, colours, n, seed, rmin=0.02, rmax=0.05, alpha=140):
    """A little scatter of dots (deterministic: a tile looks the same every run)."""
    import random
    rng = random.Random(seed)
    with p.group():
        for _ in range(n):
            x, y = rng.uniform(0.05, 0.95) * size, rng.uniform(0.05, 0.95) * size
            r = rng.uniform(rmin, rmax) * size
            c = rng.choice(colours)
            p.ellipse(x - r, y - r, x + r, y + r, c + (alpha,))


# ----------------------------------------------------------------------------------------------- plain tiles

def ground_colour(i):
    """Ground 15..30: from dry (pale) to wet (dark), 16 steps."""
    return lerp((0xC9, 0xA9, 0x6B), (0x4A, 0x32, 0x22), i / 15.0)


art.install(Painter, ground_colour)


def draw_ground(p, size, i, seed):
    c = ground_colour(i)
    p.rect(0, 0, size, size, c + (255,))
    speckles(p, size, [shade(c, 0.85), shade(c, 1.12)], 16, seed)


def draw_arms(p, size, arms, kind, tile_seed, wet=False):
    """Roads, fences and ditches: the piece reaching out to its neighbours with `arms`."""
    c = size / 2.0
    ends = {"N": (c, 0), "E": (size, c), "S": (c, size), "W": (0, c)}
    pts = [ends[a] for a in arms]
    if kind == "paved":
        for a in pts:
            p.line([(c, c), a], (0x55, 0x58, 0x5C, 255), size * 0.42)
        p.ellipse(c - size * 0.21, c - size * 0.21, c + size * 0.21, c + size * 0.21, (0x55, 0x58, 0x5C, 255))
        for a in pts:
            mid = ((c + a[0]) / 2, (c + a[1]) / 2)
            p.line([(c, c), mid], (0xE8, 0xD8, 0x50, 255), size * 0.04)
    elif kind == "dirt":
        for a in pts:
            p.line([(c, c), a], (0xA8, 0x84, 0x55, 255), size * 0.40)
        p.ellipse(c - size * 0.2, c - size * 0.2, c + size * 0.2, c + size * 0.2, (0xA8, 0x84, 0x55, 255))
        speckles(p, size, [(0x8C, 0x6A, 0x40), (0xC0, 0x9C, 0x6C)], 8, tile_seed, alpha=200)
    elif kind == "fence":
        for a in pts:
            p.line([(c, c), a], (0x8B, 0x5A, 0x2B, 255), size * 0.07)
            m = ((c + a[0]) / 2, (c + a[1]) / 2)
            p.ellipse(m[0] - size * 0.05, m[1] - size * 0.05, m[0] + size * 0.05, m[1] + size * 0.05, (0x6B, 0x42, 0x1F, 255))
        p.ellipse(c - size * 0.07, c - size * 0.07, c + size * 0.07, c + size * 0.07, (0x6B, 0x42, 0x1F, 255))
    elif kind == "ditch":
        col = (0x3C, 0x7C, 0xC8, 255) if wet else (0x2E, 0x20, 0x16, 255)
        rim = (0x6B, 0x4B, 0x2E, 255)
        for a in pts:
            p.line([(c, c), a], rim, size * 0.36)
        p.ellipse(c - size * 0.18, c - size * 0.18, c + size * 0.18, c + size * 0.18, rim)
        for a in pts:
            p.line([(c, c), a], col, size * 0.22)
        p.ellipse(c - size * 0.11, c - size * 0.11, c + size * 0.11, c + size * 0.11, col)


def draw_tree(p, size, height):
    """Trees 131 + height: young at 131, tall at 146; 128..130 saplings."""
    f = 0.18 + 0.30 * (height / 15.0)
    with p.group():
        p.ellipse(size * (0.5 - f * 0.9), size * (0.62 - f * 0.4), size * (0.5 + f * 0.9), size * (0.62 + f * 0.9), (0, 0, 0, 60))  # shadow
    p.rect(size * 0.46, size * 0.55, size * 0.54, size * 0.85, (0x6B, 0x42, 0x1F, 255))
    p.ellipse(size * (0.5 - f), size * (0.5 - f * 1.4), size * (0.5 + f), size * (0.5 + f * 0.6), (0x2F, 0x7D, 0x32, 255))
    p.ellipse(size * (0.5 - f * 0.6), size * (0.5 - f * 1.2), size * (0.5 + f * 0.3), size * (0.5 - f * 0.1), (0x4C, 0xA8, 0x4F, 255))


def draw_plain(t, size, labels):
    """One plain tile (opaque RGBA)."""
    p = Painter(size, size, ground_colour(8) + (255,))
    seed = t * 7919
    f = lambda fam: fam[0] <= t < fam[0] + fam[1]
    if f(GROUND):
        draw_ground(p, size, t - GROUND[0], seed)
    elif f(WATER):
        i = t - WATER[0]
        interior = i == 16
        base = (0x2B, 0x6C, 0xB5) if interior or i > 16 else (0x3A, 0x7F, 0xC9)
        p.rect(0, 0, size, size, base + (255,))
        if not interior and i <= 15:  # shore pieces: a sandy rim, the lower bits of i say which sides
            sand = (0xD8, 0xC0, 0x8A, 255)
            w = size * 0.22
            if i & 1: p.rect(0, 0, size, w, sand)
            if i & 2: p.rect(size - w, 0, size, size, sand)
            if i & 4: p.rect(0, size - w, size, size, sand)
            if i & 8: p.rect(0, 0, w, size, sand)
        with p.group():
            for k in range(3):
                y = size * (0.25 + 0.25 * k)
                p.line([(size * 0.15, y), (size * 0.35, y - size * 0.04), (size * 0.55, y), (size * 0.75, y - size * 0.04)], (255, 255, 255, 90), size * 0.03)
    elif f(DITCH_EMPTY) or f(DITCH_FULL):
        draw_ground(p, size, 8, seed)
        wet = f(DITCH_FULL)
        fam = DITCH_FULL if wet else DITCH_EMPTY
        draw_arms(p, size, PIECE_ARMS[t - fam[0]], "ditch", seed, wet)
    elif f(PAVED) or f(DIRT):
        draw_ground(p, size, 8, seed)
        fam, kind = (PAVED, "paved") if f(PAVED) else (DIRT, "dirt")
        draw_arms(p, size, PIECE_ARMS[t - fam[0]], kind, seed)
    elif f(FENCE):
        draw_ground(p, size, 8, seed)
        draw_arms(p, size, PIECE_ARMS[t - FENCE[0]], "fence", seed)
    elif f(GATE) or t in (192, 193):
        draw_ground(p, size, 8, seed)
        horizontal = (t - GATE[0]) % 2 == 0
        c = size / 2.0
        a, b = ((0, c), (size, c)) if horizontal else ((c, 0), (c, size))
        p.line([a, b], (0x8B, 0x5A, 0x2B, 255), size * 0.07)
        open_gate = t == GATE[0] + 2 or t == GATE[0] + 3
        broken = t in (192, 193)
        if broken:
            p.line([a, ((a[0] + b[0]) / 2 - size * 0.05, (a[1] + b[1]) / 2 - size * 0.05)], (0x8B, 0x5A, 0x2B, 255), size * 0.07)
        elif open_gate:
            p.line([(c, c), (c + size * 0.3, c - size * 0.3)], (0xC8, 0x9B, 0x5A, 255), size * 0.06)
        else:
            p.line([(size * 0.25, c - size * 0.12), (size * 0.75, c + size * 0.12)] if horizontal else [(c - size * 0.12, size * 0.25), (c + size * 0.12, size * 0.75)], (0xC8, 0x9B, 0x5A, 255), size * 0.05)
    elif f(VALVE):
        draw_ground(p, size, 8, seed)
        draw_arms(p, size, PIECE_ARMS[(t - VALVE[0]) % 2], "ditch", seed, (t - VALVE[0]) >= 2)
        p.ellipse(size * 0.38, size * 0.38, size * 0.62, size * 0.62, (0xC0, 0x30, 0x30, 255), (255, 255, 255, 255), size * 0.03)
    elif f(BRIDGE):
        p.rect(0, 0, size, size, (0x3A, 0x7F, 0xC9, 255))
        horizontal = (t - BRIDGE[0]) == 0
        if horizontal:
            p.rect(0, size * 0.2, size, size * 0.8, (0x9C, 0x6B, 0x3A, 255))
            for k in range(1, 6): p.line([(size * k / 6, size * 0.2), (size * k / 6, size * 0.8)], (0x6B, 0x42, 0x1F, 255), size * 0.03)
        else:
            p.rect(size * 0.2, 0, size * 0.8, size, (0x9C, 0x6B, 0x3A, 255))
            for k in range(1, 6): p.line([(size * 0.2, size * k / 6), (size * 0.8, size * k / 6)], (0x6B, 0x42, 0x1F, 255), size * 0.03)
    elif f(TROUGH):
        draw_ground(p, size, 8, seed)
        p.rect(size * 0.15, size * 0.35, size * 0.85, size * 0.65, (0x7A, 0x7C, 0x80, 255))
        p.rect(size * 0.2, size * 0.4, size * 0.8, size * 0.6, (0x3A, 0x7F, 0xC9, 255) if t == TROUGH[0] else (0x2E, 0x20, 0x16, 255))
    elif f(TREES):
        draw_ground(p, size, 8, seed)
        draw_tree(p, size, max(0, t - 131) if t >= 131 else (t - 128) * 2)
    elif f(ROCKS):
        draw_ground(p, size, 8, seed)
        import random
        rng = random.Random(seed)
        for _ in range(3 + t - ROCKS[0]):
            x, y, r = rng.uniform(0.25, 0.75) * size, rng.uniform(0.3, 0.8) * size, rng.uniform(0.1, 0.2) * size
            p.ellipse(x - r, y - r * 0.8, x + r, y + r * 0.8, (0x84, 0x86, 0x8A, 255), (0x55, 0x57, 0x5B, 255), size * 0.02)
    elif t in (73, 74, 113, 114):
        draw_ground(p, size, 3, seed)
        with p.group():
            p.ellipse(size * 0.1, size * 0.1, size * 0.9, size * 0.9, (0x20, 0x20, 0x20, 200))
        p.line([(size * 0.2, size * 0.3), (size * 0.8, size * 0.7)], (0x70, 0x70, 0x78, 255), size * 0.05)
        p.line([(size * 0.25, size * 0.75), (size * 0.7, size * 0.25)], (0x90, 0x60, 0x40, 255), size * 0.05)
    elif t in (12, 36):
        # a field's own soil: bare (36, as the crop pictures' background) and plowed (12, with deep furrows)
        p.rect(0, 0, size, size, (0x6A, 0x4A, 0x32, 255) if t == 36 else (0x5C, 0x3E, 0x2A, 255))
        speckles(p, size, [(0x55, 0x3A, 0x26), (0x7C, 0x5A, 0x3E)], 14, seed, alpha=160)
        for k in range(4):
            y = size * (0.12 + 0.24 * k)
            p.line([(0, y), (size, y)], (0x4A, 0x32, 0x20, 255) if t == 12 else (0x55, 0x3A, 0x26, 255), size * (0.09 if t == 12 else 0.05))
            if t == 12:
                p.line([(0, y + size * 0.07), (size, y + size * 0.07)], (0x86, 0x62, 0x44, 255), size * 0.03)
    elif SIGNS <= t < SIGNS + 64:
        pass  # (made as 2 x 2 signs below)
    elif CROP_GROWTH <= t < CROP_GROWTH + 128:
        pass  # (made below)
    elif CROP_ICON_A <= t < CROP_ICON_A + 32:
        pass  # (made below)
    elif EVENT_GROUND <= t < EVENT_GROUND + 35:
        pass  # (made below)
    else:
        c = band_colour(t)
        p.rect(0, 0, size, size, c + (255,))
        speckles(p, size, [shade(c, 0.8), shade(c, 1.15)], 10, seed, alpha=120)
        p.rect(0, 0, size, size * 0.06, shade(c, 0.7) + (255,))
        p.rect(0, 0, size * 0.06, size, shade(c, 0.7) + (255,))
    img = p.finish()
    return img


# ------------------------------------------------------------------------------------- crops, signs, icons

def draw_crop_stage(size, slot, stage):
    """Growth tile `stage` (0..7) of a crop slot: rows of that crop's plants, growing and ripening."""
    return art.crop_stage(size, CROP_ROWS[slot][1], stage)


def draw_sign(size, slot):
    """A crop's sign: 2 x 2 tiles, returned as one 2 size x 2 size picture to be cut."""
    s = size * 2
    p = Painter(s, s, ground_colour(8) + (255,))  # (the plain sheet is opaque: the sign stands on ground)
    col = CROP_COLOURS[slot]
    p.rect(s * 0.46, s * 0.55, s * 0.54, s * 0.98, (0x6B, 0x42, 0x1F, 255))
    p.rect(s * 0.1, s * 0.12, s * 0.9, s * 0.62, (0xE8, 0xD0, 0x9A, 255))
    p.rect(s * 0.14, s * 0.16, s * 0.86, s * 0.58, col + (255,))
    # the slot number in big digits
    digits = str(slot + 1)
    sc = max(1, s // 14)
    tw = len(digits) * 4 * sc - sc
    x0, y0 = s * 0.5 - tw / 2.0, s * 0.37 - 2.5 * sc
    for n, ch in enumerate(digits):
        bits = DIGITS[ch]
        for r in range(5):
            for c in range(3):
                if bits[r * 3 + c] == "1":
                    p.rect(x0 + (n * 4 + c) * sc, y0 + r * sc, x0 + (n * 4 + c + 1) * sc, y0 + (r + 1) * sc, (255, 255, 255, 255))
    return p.finish()


def draw_icon(size, slot, frame):
    return art.crop_icon(size, CROP_ROWS[slot][1], frame)


def draw_event_ground(size, bank):
    """The 7 x 5 block of tiles 483..517 for bank 0 (plain town square), 1 (rodeo), 2 (fair), as one picture."""
    w, h = size * 7, size * 5
    if bank == 0:
        p = Painter(w, h, (0xB8, 0xB4, 0xA8, 255))
        for ky in range(5):
            for kx in range(7):
                p.rect(kx * size + 1, ky * size + 1, (kx + 1) * size - 1, (ky + 1) * size - 1, ((0xC4, 0xC0, 0xB4, 255) if (kx + ky) % 2 else (0xB0, 0xAC, 0xA0, 255)))
    elif bank == 1:
        p = Painter(w, h, (0xC8, 0xA0, 0x68, 255))
        speckles(p, w, [(0xA8, 0x80, 0x50), (0xDC, 0xB8, 0x80)], 140, 11, rmin=0.004, rmax=0.01)
        p.ellipse(w * 0.06, h * 0.12, w * 0.94, h * 0.88, None, (0x8B, 0x5A, 0x2B, 255), size * 0.12)
    else:
        p = Painter(w, h, (0x4C, 0x9A, 0x4A, 255))
        speckles(p, w, [(0x3C, 0x84, 0x3A), (0x62, 0xB0, 0x5E)], 140, 12, rmin=0.004, rmax=0.01)
        for k in range(7):
            col = [(0xD0, 0x30, 0x30), (0xF0, 0xD0, 0x40), (0x30, 0x60, 0xD0)][k % 3]
            p.polygon([(w * (0.08 + 0.13 * k), h * 0.3), (w * (0.14 + 0.13 * k), h * 0.1), (w * (0.2 + 0.13 * k), h * 0.3)], col + (255,))
            p.rect(w * (0.08 + 0.13 * k), h * 0.3, w * (0.2 + 0.13 * k), h * 0.42, shade(col, 0.8) + (255,))
        p.ellipse(w * 0.4, h * 0.45, w * 0.6, h * 0.95, None, (0xE0, 0xE0, 0xE8, 255), size * 0.08)
        p.ellipse(w * 0.47, h * 0.62, w * 0.53, h * 0.78, (0xE0, 0x40, 0x40, 255))
    return p.finish()


# ----------------------------------------------------------------------------------------------- masked tiles

SPECIES_OF = {"Horse": 0, "Cow": 1, "Pig": 2, "Sheep": 3}
BARREL_COLOURS = {912: (0x30, 0x70, 0xC0), 913: (0xC0, 0x34, 0x30), 914: (0xD0, 0x80, 0x20), 915: (0x4C, 0x9A, 0x3A)}
CAR_COLOURS = [(0xC8, 0x32, 0x28), (0x34, 0x68, 0xC0), (0xEC, 0xEC, 0xE8), (0x3C, 0x9A, 0x4C)]


def cell(img, k, horizontal):
    """Cell k of a two-cell picture: the left one if it lies across, else the top one."""
    w, h = img.size
    n = 2
    return img.crop((k * w // n, 0, (k + 1) * w // n, h)) if horizontal else img.crop((0, k * h // n, w, (k + 1) * h // n))


def masked_sprite(t, size):
    """The picture of masked tile `t` (848..1047), turned and cut the way the engine lays machines, animals and cars out."""
    two = {860: "harvester", 876: "trailer", 896: "truck"}
    if 848 <= t < 852: return art.turned(art.machine("tractor", size), t - 848)
    if 852 <= t < 856: return art.turned(art.machine("plow", size), t - 852)
    if 856 <= t < 860: return art.turned(art.machine("planter", size), t - 856)
    if 860 <= t < 868:
        h, k = divmod(t - 860, 2)
        return cell(art.turned(art.machine("harvester", size), h), k, h % 2 == 1)
    if 868 <= t < 876:
        h, v = divmod(t - 868, 2)
        return art.turned(art.machine("sprayer", size, spraying=bool(v)), h)
    for base, kind in ((876, "trailer"), (896, "truck")):
        if base <= t < base + 16:
            h, r = divmod(t - base, 4)
            return cell(art.turned(art.machine(kind, size, loaded=r >= 2), h), r % 2, h % 2 == 1)
    if t in BARREL_COLOURS: return art.barrel(size, BARREL_COLOURS[t])
    if 916 <= t < 932: return art.car(size, CAR_COLOURS[(t - 916) // 4], (t - 916) % 4)
    if 946 <= t < 1010:
        sp, r = divmod(t - 946, 16)
        return art.spun(art.animal(sp, size, r % 2 == 1), r // 2)
    if 1010 <= t < 1014: return art.windmill(size, t - 1010)
    if 1020 <= t < 1025: return art.hay(size, t - 1020)
    if 1028 <= t < 1036: return art.turned(art.machine("plane", size), (t - 1028) % 4)
    if 1038 <= t < 1042: return art.animal(t - 1038, size, True, kick=True)  # horse, cow, pig, sheep
    if 932 <= t < 946: return art.figure(size, [(0x3A, 0x5A, 0xC0), (0xC0, 0x40, 0x30), (0x3A, 0x9A, 0x4A), (0xD0, 0xA0, 0x30)][t % 4])
    return None


def draw_masked(m, size):
    """One masked tile (RGBA, see-through around the picture); overlay id 0x350 + m, tile id 848 + m."""
    t = 848 + m
    img = masked_sprite(t, size)
    if img is not None:
        return img.convert("RGBA")
    p = Painter(size, size, (0, 0, 0, 0))  # anything else the engine has no use for here: a small crate
    c = band_colour(t, s=0.45, v=0.8)
    with p.group():
        p.ellipse(size * 0.12, size * 0.6, size * 0.9, size * 0.92, (0, 0, 0, 60))
    p.rect(size * 0.2, size * 0.3, size * 0.8, size * 0.8, c + (255,))
    p.rect(size * 0.2, size * 0.3, size * 0.8, size * 0.38, shade(c, 1.2) + (255,))
    p.line([(size * 0.2, size * 0.3), (size * 0.8, size * 0.8)], shade(c, 0.6) + (255,), size * 0.03)
    p.line([(size * 0.8, size * 0.3), (size * 0.2, size * 0.8)], shade(c, 0.6) + (255,), size * 0.03)
    return p.finish()


# ----------------------------------------------------------------------------------------------- labels

def label(img, text, size):
    sc = max(1, size // 32)
    w = len(text) * 4 * sc - sc
    if w + 2 * sc > size or 7 * sc > size:
        return
    px = img.load()
    for y in range(0, 7 * sc):
        for x in range(0, w + 2 * sc):
            r, g, b, a = px[x, y]
            px[x, y] = (r * 2 // 5, g * 2 // 5, b * 2 // 5, max(a, 200))
    for n, ch in enumerate(text):
        bits = DIGITS[ch]
        for r in range(5):
            for c in range(3):
                if bits[r * 3 + c] == "1":
                    for dy in range(sc):
                        for dx in range(sc):
                            px[sc + (n * 4 + c) * sc + dx, sc + r * sc + dy] = (255, 255, 255, 255)


# ----------------------------------------------------------------------------------------------- interface pictures
# Sizes in interface units (the original's 640 x 480 screen), as the engine expects them (main.cpp ArchivePictureTest).
WINDOWS = {
    "about.bmp": (400, 224), "airpwndo.bmp": (256, 176), "bankterm.bmp": (288, 224), "buywndo.bmp": (256, 224),
    "cashflow.bmp": (400, 224), "choscrop.bmp": (320, 224), "cntlbtnh.bmp": (640, 32), "evaluate.bmp": (352, 192),
    "examine.bmp": (208, 208), "farmburo.bmp": (400, 224), "fdstatus.bmp": (336, 224), "filegenr.bmp": (208, 208),
    "futures.bmp": (320, 208), "mapwndo.bmp": (288, 240), "popup.bmp": (256, 192), "sellwndo.bmp": (192, 256),
    "twnchoic.bmp": (368, 224), "weather.bmp": (336, 192),
}
EDITEASY = (48, 208)  # editeasy.lzs: 24 x 24 icons, 2 across
# face files: name, faces, logical size of a face, face names where the engine knows them (picture.h)
BUTTON_FACES = ["OK", "BUY", "SELL", "CANCEL", "GOTO", "USE", "EXIT", "LOAD", "NEW", "USED", "YES", "NO", "SAVE", "NEXT", "PREV", "CLOSE", "TOP", "RATS"]
FACE_FILES = [("buttons.til", 18, (48, 24), BUTTON_FACES), ("fldgrph.til", 20, (48, 24), None), ("wethvane.til", 2, (32, 32), None)]
FULL_SCREENS = ["sftitle.int", "scenario.int", "prsnt480.int", "defworld.int"]
SPRITES = [("airplane.spr", 8), ("apshadow.spr", 8), ("tornado.spr", 4), ("swarm.spr", 4), ("cowman1.spr", 5), ("cowman2.spr", 5), ("cowman3.spr", 5)]
SPRITE_UNITS = 64
PIC_SCALE = 2  # pixels per interface unit in the pictures made here


def font(px):
    try:
        return ImageFont.load_default(size=px)  # Pillow 10.1+: a scalable default font
    except TypeError:
        return ImageFont.load_default()


def text_centre(d, box, text, px, fill):
    f = font(px)
    x0, y0, x1, y1 = box
    l, t, r, b = d.textbbox((0, 0), text, font=f)
    d.text(((x0 + x1 - (r - l)) / 2 - l, (y0 + y1 - (b - t)) / 2 - t), text, fill=fill, font=f)


# ---- what the engine says is where in each picture
# The interface paints its own marks over the pictures (and covers the pictures' words when a translation is shown), so its
# source holds each picture's layout: the labels of PictureText.cpp (a picture, a place, the word), the start screens'
# covers (a place, the word) and button rectangles, and each window's close button. Reading them lets a placeholder
# picture show the words and buttons where the engine expects them.
SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src")


# Regions of window pictures that the engine reads clicks in but nowhere names as a button (found in the click handlers of
# Windows.cpp: BuyWindow, SellWindow): (x, y, w, h, label).
EXTRA_BUTTONS = {
    "buywndo.bmp": [(11, 7 + 30 * c, 27, 27, t) for c, t in enumerate(("MACH", "SEED", "LIVE", "BLDG", "SPLY"))] + [(177, 73, 26, 26, "^"), (208, 73, 26, 26, "v")],
    "sellwndo.bmp": [(17, 3 + 30 * c, 27, 27, t) for c, t in enumerate(("CROP", "MACH", "LIVE"))] + [(16, 96, 16, 16, "^"), (16, 176, 16, 16, "v")],
}


def read_src(*parts):
    path = os.path.join(SRC, *parts)
    if not os.path.exists(path):
        return ""
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


STRING = r'((?:[^"\\]|\\.)*)'
LABEL_RE = re.compile(r'\{ "([a-z0-9_]+\.[a-z]+)", (\d+), (\d+), (\d+), N_\("' + STRING + r'"\), (-?\d+), (\d+), (\d+), (\d+), (\d+), (\d+) \}')
COVER_RE = re.compile(r'\{ (\d+), (\d+), (\d+), (\d+), N_\("' + STRING + r'"\), (\d+), (\d+), (true|false) \}')
RECT_RE = re.compile(r'\b(k[A-Z][A-Za-z]*)\{ (\d+), (\d+), (\d+), (\d+) \}')
WINDOW_RE = re.compile(r'\w+\(\) : Window\(\w+, N_\("[^"]*"\), "([a-z0-9_.]+)", (\d+), (\d+)\)')
CLOSE_RE = re.compile(r'CloseButton\(\) const override \{ return \{ (\d+), (\d+), (\d+), (\d+) \}; \}')


def unescape(t):
    return t.replace("\\n", "\n").replace('\\"', '"')


def source_layout():
    """picture file -> [item]; an item is a dict: kind label | button | gauge, x, y, w, h, text, align, lines, lh, wrap."""
    layout = {}

    def add(pic, **item):
        layout.setdefault(pic, []).append(item)

    for m in LABEL_RE.finditer(read_src("ui", "PictureText.cpp")):
        pic, x, y, room, text, bg, ink, align, lines, lh, wrap = m.groups()
        kind = "label"
        text = unescape(text)
        for prefix, k in (("small button:", "button"), ("button:", "button"), ("gauge:", "gauge")):
            if text.startswith(prefix):
                text, kind = text[len(prefix):], k
        add(pic, kind=kind, x=int(x), y=int(y), w=int(room), h=8, text=text, align=int(align), lines=int(lines), lh=int(lh), wrap=int(wrap))
    for pic, buttons in EXTRA_BUTTONS.items():
        for x, y, w, h, text in buttons:
            add(pic, kind="button", x=x, y=y, w=w, h=h, text=text, align=1, lines=1, lh=8, wrap=0, exact=True, small=len(text) > 1)
    windows = read_src("ui", "Windows.cpp")
    found = list(WINDOW_RE.finditer(windows))
    for i, m in enumerate(found):
        segment = windows[m.end(): found[i + 1].start() if i + 1 < len(found) else len(windows)]
        c = CLOSE_RE.search(segment)
        if c and m.group(1):
            x, y, w, h = (int(v) for v in c.groups())
            add(m.group(1), kind="button", x=x, y=y, w=w, h=h, text="CLOSE", align=1, lines=1, lh=8, wrap=0, exact=True)
    return layout


def start_layout():
    """The start screens: named rectangles, and the covers (the words the pictures carry and where)."""
    text = read_src("ui", "StartScreen.cpp")
    rects = {m.group(1): tuple(int(v) for v in m.groups()[1:]) for m in RECT_RE.finditer(text)}

    def covers(first, last):
        a, b = text.find(first), text.find(last)
        body = text[a:b] if a >= 0 and b > a else ""
        return [dict(x=int(m[0]), y=int(m[1]), w=int(m[2]), h=int(m[3]), text=unescape(m[4]), cx=int(m[5]), ty=int(m[6]), big=m[7] == "true")
                for m in COVER_RE.findall(body)]
    return {"rects": rects, "region": covers("void StartScreen::DrawTranslatedRegion", "void StartScreen::DrawTranslatedDesign"),
            "design": covers("void StartScreen::DrawTranslatedDesign", "// ---------------------------------------------------------------- the phone layout")}


INK = (0x20, 0x20, 0x20, 255)


def face_rect(d, x, y, w, h, k, fill=(0xC4, 0xC4, 0xC4, 255)):
    """A raised button face: black outline, white top and left, grey bottom and right."""
    d.rectangle([x * k, y * k, (x + w) * k - 1, (y + h) * k - 1], fill=fill, outline=(0, 0, 0, 255), width=k)
    d.line([((x + 1) * k, (y + 1) * k), ((x + w - 2) * k, (y + 1) * k)], fill=(255, 255, 255, 255), width=k)
    d.line([((x + 1) * k, (y + 1) * k), ((x + 1) * k, (y + h - 2) * k)], fill=(255, 255, 255, 255), width=k)
    d.line([((x + 1) * k, (y + h - 2) * k), ((x + w - 2) * k, (y + h - 2) * k)], fill=(0x86, 0x86, 0x86, 255), width=k)
    d.line([((x + w - 2) * k, (y + 1) * k), ((x + w - 2) * k, (y + h - 2) * k)], fill=(0x86, 0x86, 0x86, 255), width=k)


def put_text(d, k, text, x, y, w, align=0, px=8, fill=INK, lh=None):
    """Text (lines split at \\n) with its top at y, in a box w wide from x; align 0 left, 1 centre, 2 right."""
    f = font(px * k)
    for n, line in enumerate(text.split("\n")):
        l, t, r, b = d.textbbox((0, 0), line, font=f)
        tx = x * k if align == 0 else (x + w) * k - (r - l) if align == 2 else (x + w / 2.0) * k - (r - l) / 2.0
        d.text((tx - l, (y + n * (lh or px + 1)) * k - t), line, fill=fill, font=f)


def wrap_words(d, k, text, w, px=8):
    f = font(px * k)
    lines, cur = [], ""
    for word in text.split():
        trial = (cur + " " + word).strip()
        if cur and d.textlength(trial, font=f) > w * k:
            lines.append(cur)
            cur = word
        else:
            cur = trial
    if cur:
        lines.append(cur)
    return "\n".join(lines)


def draw_layout(d, k, items):
    """The engine's marks for a picture: button faces and the words it expects."""
    for it in items:
        if it["kind"] == "button":
            if it.get("exact"):
                bx, by, bw, bh = it["x"], it["y"], it["w"], it["h"]
            else:  # the label sits inside its button
                bx, by, bw, bh = it["x"] - 4, it["y"] - 6, it["w"] + 8, 20
            face_rect(d, bx, by, bw, bh, k)
            px = 6 if it.get("small") else 8
            put_text(d, k, it["text"], bx, by + (bh - px) // 2 + (3 if it.get("small") and bh > 20 else 0), bw, 1, px=px)
        elif it["kind"] == "gauge":
            put_text(d, k, it["text"], it["x"], it["y"], it["w"], 1, px=7)
        else:
            text = wrap_words(d, k, it["text"], it["w"]) if it["wrap"] > 0 else it["text"]
            put_text(d, k, text, it["x"], it["y"], it["w"], it["align"], lh=it["lh"] if it["lines"] > 1 or it["wrap"] > 0 else None)


def panel(w, h, name, tint, items=()):
    """A window picture: a bevelled panel, its file name and size in a corner, and the engine's marks."""
    img = Image.new("RGBA", (w * PIC_SCALE, h * PIC_SCALE), tint + (255,))
    d = ImageDraw.Draw(img)
    k = PIC_SCALE
    d.rectangle([0, 0, w * k - 1, h * k - 1], outline=(0x30, 0x30, 0x30, 255), width=k)
    d.line([(k, k), (w * k - 2 * k, k)], fill=(255, 255, 255, 255), width=k)
    d.line([(k, k), (k, h * k - 2 * k)], fill=(255, 255, 255, 255), width=k)
    d.line([(k, h * k - 2 * k), (w * k - 2 * k, h * k - 2 * k)], fill=(0x80, 0x80, 0x80, 255), width=k)
    d.line([(w * k - 2 * k, k), (w * k - 2 * k, h * k - 2 * k)], fill=(0x80, 0x80, 0x80, 255), width=k)
    if h >= 64 and not items:
        text_centre(d, (0, 4 * k, w * k, 20 * k), f"{name}  {w} x {h}", 9 * k, (0x20, 0x20, 0x20, 255))
    draw_layout(d, k, items)
    if name == "buywndo.bmp":
        # the two small pictures the editor's PLANT and SPRAY buttons are cut from (16 x 16 at 16, 42 and 18 x 18 at 15, 131):
        # a framed ear of corn and a barrel
        d.rectangle([16 * k, 42 * k, 32 * k - 1, 58 * k - 1], fill=(0x3C, 0x9A, 0x3A, 255), outline=(0, 0, 0, 255), width=k)
        d.ellipse([21 * k, 44 * k, 27 * k, 56 * k], fill=(0xF0, 0xD0, 0x40, 255))
        d.rectangle([15 * k, 131 * k, 33 * k - 1, 149 * k - 1], fill=(0x80, 0x50, 0x2A, 255), outline=(0, 0, 0, 255), width=k)
        for yy in (136, 144):
            d.line([(15 * k, yy * k), (33 * k - 1, yy * k)], fill=(0x40, 0x40, 0x48, 255), width=k)
    return img, d


def toolbar():
    """cntlbtnh.bmp, 640 x 32 units. The interface cuts it into parts (GameUi::DrawToolbar): 8 buttons of 32 (BUY, SELL,
    EVALUATION, MAP, WEATHER, BALANCE SHEET, BANK at x 0..192, the unused EDIT at 224, MARKET VALUE at 256), the forecast
    box at 304..379 (the three days' sky tiles go in it; 380..399 is the plain grey the bar is stretched with), and the
    date and funds panel at 400..639. A black line runs along the top."""
    k = PIC_SCALE
    img = Image.new("RGBA", (640 * k, 32 * k), (0xC4, 0xC4, 0xC4, 255))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 640 * k - 1, k - 1], fill=(0, 0, 0, 255))  # the black line under the menu bar
    for x, label in ((0, "BUY"), (32, "SELL"), (64, "EVAL"), (96, "MAP"), (128, "WTHR"), (160, "BAL"), (192, "BANK"), (224, "EDIT"), (256, "MKT")):
        face_rect(d, x + 1, 2, 30, 28, k, fill=(0xD0, 0xD0, 0xD0, 255) if label != "EDIT" else (0xB0, 0xB0, 0xB0, 255))
        put_text(d, k, label, x + 1, 12, 30, 1, px=7)
    d.rectangle([304 * k, 2 * k, 379 * k, 30 * k - 1], fill=(0xF0, 0xF4, 0xFF, 255), outline=(0, 0, 0, 255), width=k)  # the forecast box
    d.rectangle([404 * k, 2 * k, 636 * k, 30 * k - 1], fill=(0xE4, 0xE4, 0xE4, 255), outline=(0, 0, 0, 255), width=k)  # the date and funds
    return img


TOOL_ICONS = ["Move", "Exam", "Road", "Dirt", "Fence", "Gate", "Ditch", "Valve", "Feed", "Trough", "Trees", "Doze"]  # GameUi's kToolNames


def editeasy():
    """editeasy.lzs: the editor palette's 12 tool icons, 24 x 24 each in two columns of six, in the order of the engine's
    kToolNames (Move, Examine, Paved Road, Dirt Road, Fence, Gate, Ditch, Valve, Feed, Trough, Trees, Bulldoze)."""
    w, h = EDITEASY
    k = PIC_SCALE
    img = Image.new("RGBA", (w * k, h * k), (0xC0, 0xC0, 0xC0, 255))
    d = ImageDraw.Draw(img)
    for i, name in enumerate(TOOL_ICONS):
        x, y = (i % 2) * 24 * k, (i // 2) * 24 * k
        d.rectangle([x, y, x + 24 * k - 1, y + 24 * k - 1], fill=band_colour((i // 2) * 11 + 3, 0.35, 0.92) + (255,), outline=(0x30, 0x30, 0x30, 255), width=k)
        put_text(d, k, name, x // k, y // k + 8, 24, 1, px=6)
    return img


def face(w, h, text, hue):
    img = Image.new("RGBA", (w * PIC_SCALE, h * PIC_SCALE), band_colour(hue * 11, 0.3, 0.95) + (255,))
    d = ImageDraw.Draw(img)
    k = PIC_SCALE
    d.rectangle([0, 0, w * k - 1, h * k - 1], outline=(0x30, 0x30, 0x30, 255), width=k)
    d.line([(k, k), (w * k - 2 * k, k)], fill=(255, 255, 255, 255), width=k)
    d.line([(k, k), (k, h * k - 2 * k)], fill=(255, 255, 255, 255), width=k)
    text_centre(d, (0, 0, w * k, h * k), text, (9 if h <= 24 else 10) * k, (0x20, 0x20, 0x20, 255))
    return img


def centre_cover(d, k, c, fill=(255, 255, 255, 255)):
    """A cover of the start screens: its words centred on cx, the top at ty; a title is twice the size."""
    px = 16 if c["big"] else 8
    put_text(d, k, c["text"], c["cx"] - 160, c["ty"], 320, 1, px=px, fill=fill, lh=px + 2)


def start_picture(name, hue, kind):
    """scenario.int (the region screen) and defworld.int (the design screen): the full-screen picture with the engine's buttons,
    sliders' tracks and words in the places its code expects."""
    img = full_screen_base(name, hue)
    k = PIC_SCALE
    d = ImageDraw.Draw(img)
    st = start_layout()
    R = st["rects"]
    if kind == "region":
        mx, my, mw, mh = R.get("kMap", (248, 39, 290, 290))
        grids = region_grids()
        for gy in range(18):
            for gx in range(18):
                rain, temp = "abcd".index(grids["rain"][gy][gx]), "abcd".index(grids["temperature"][gy][gx])
                col = lerp(lerp((0xC8, 0xB0, 0x78), (0x3C, 0x9A, 0x4A), rain / 3.0), (0xE8, 0xE0, 0xA0), temp / 6.0)
                d.rectangle([(mx + gx * mw // 18) * k, (my + gy * mh // 18) * k, (mx + (gx + 1) * mw // 18) * k - 1, (my + (gy + 1) * mh // 18) * k - 1], fill=col + (255,))
        d.rectangle([mx * k, my * k, (mx + mw) * k - 1, (my + mh) * k - 1], outline=(0, 0, 0, 255), width=k)
        for y in (101, 149, 197):  # the climate sliders' tracks (the engine fills them from the left)
            d.rectangle([120 * k, y * k, 216 * k - 1, (y + 10) * k - 1], fill=(0x20, 0x20, 0x20, 255), outline=(0, 0, 0, 255), width=k)
        faces = ["kUseRegion", "kDesign", "kLoad", "kQuit"]
        covers = st["region"]
    else:
        for y in (105, 163, 222):  # the sliders' tracks, 80 x 10 at x 160 (the engine draws them whole)
            d.rectangle([160 * k, y * k, 240 * k - 1, (y + 10) * k - 1], fill=(0, 0, 0, 255))
        px, py, pw, ph = R.get("kPreview", (264, 72, 192, 192))
        d.rectangle([px * k, py * k, (px + pw) * k - 1, (py + ph) * k - 1], fill=(0x18, 0x18, 0x18, 255), outline=(0, 0, 0, 255), width=k)
        faces = ["kRainDown", "kRainUp", "kTempDown", "kTempUp", "kWindDown", "kWindUp", "kRiver", "kLake", "kGenerate", "kPlay"]
        covers = st["design"]
    for name_ in faces:
        if name_ in R:
            x, y, w, h = R[name_]
            face_rect(d, x, y, w, h, k)
            if name_.endswith("Down") or name_.endswith("Up"):
                put_text(d, k, "<" if name_.endswith("Down") else ">", x, y + 4, w, 1)
    on_button = [R[n] for n in faces if n in R]
    for c in covers:
        # words that fall on a button are dark on its grey face; the rest are white on the picture
        on = any(x <= c["cx"] < x + w and y <= c["ty"] < y + h for x, y, w, h in on_button)
        centre_cover(d, k, c, INK if on else (255, 255, 255, 255))
    return img


def full_screen_base(name, hue):
    w, h, k = 640, 480, PIC_SCALE
    img = Image.new("RGBA", (w * k, h * k))
    px = img.load()
    top, bottom = band_colour(hue * 13, 0.45, 0.55), band_colour(hue * 13 + 5, 0.5, 0.25)
    for y in range(h * k):
        c = lerp(top, bottom, y / (h * k - 1))
        for x in range(w * k):
            px[x, y] = c + (255,)
    return img


def full_screen(name, hue):
    img = full_screen_base(name, hue)
    d = ImageDraw.Draw(img)
    text_centre(d, (0, 0, 640 * PIC_SCALE, 480 * PIC_SCALE), f"{name}  640 x 480", 24 * PIC_SCALE, (255, 255, 255, 255))
    return img


def sprite_frame(n, frame, hue):
    u, k = SPRITE_UNITS, PIC_SCALE
    p = Painter(u * k, u * k, (0, 0, 0, 0))
    c = band_colour(hue * 17, 0.6, 0.9)
    with p.group():
        p.ellipse(u * k * 0.12, u * k * 0.5, u * k * 0.88, u * k * 0.92, (0, 0, 0, 70))
    p.ellipse(u * k * 0.14, u * k * 0.14, u * k * 0.86, u * k * 0.8, c + (255,), shade(c, 0.55) + (255,), u * k * 0.035)
    img = p.finish()
    d = ImageDraw.Draw(img)
    text_centre(d, (0, 0, u * k, u * k * 0.9), f"{frame}", 16 * k, (0x20, 0x20, 0x20, 255))
    return img


# ---- the picture banks: alltiles.btl (the BUY window's pictures, the message boxes', the Farm Expert's), wethsky.btl and
# wethgrnd.btl (the weather window's sky and ground). Records of 112 x 64 units (the weather ones 112 x 32).
def bank_names():
    """What the engine uses each alltiles.btl record for (game/Shop.cpp, game/Machines.cpp, the event calls)."""
    names = {}
    for i, row in enumerate(CROP_ROWS):
        names[i] = row[1]                                           # 0..23 the crops
    for first, name in ((26, "Sheep"), (28, "Pig"), (30, "Horse"), (32, "Cow")):
        names[first], names[first + 1] = name, name + " (expecting)"  # 26..33 the animals
    for i, name in enumerate(("Tractor", "Plow", "Planter", "Sprayer", "Harvester", "Trailer", "Truck", "Crop Duster")):
        names[34 + i] = name                                         # 34..41 the machines
    names.update({42: "Silo", 43: "Shed", 44: "Barn", 45: "Windmill", 50: "Water Tower", 51: "Water Pump"})   # the buildings
    for i, name in enumerate(("Fertilizer", "Pesticide", "Herbicide", "Fungicide")):
        names[52 + i] = name                                         # 52..55 the supplies
    names.update({47: "Locusts", 56: "Frost", 57: "Windstorm", 58: "Drought", 62: "Flood", 63: "Tornado", 66: "Farm Expert",
                  74: "Notice", 75: "Airport", 76: "Bank"})           # the event boxes' pictures
    return names


def bank_sprite(index):
    """The drawing a shop or Examine picture shows for alltiles.btl record `index` (see bank_names), or None."""
    r = 96
    if index < 24:
        return art.crop_icon(r, CROP_ROWS[index][1], (0x4C, 0xA0, 0x4C))
    if 26 <= index < 34:
        species = {26: 3, 28: 2, 30: 0, 32: 1}[index & ~1]
        return art.animal(species, r, True)
    if 34 <= index < 42:
        kind = ("tractor", "plow", "planter", "sprayer", "harvester", "trailer", "truck", "plane")[index - 34]
        img = art.machine(kind, r, loaded=kind == "truck")
        return img.rotate(-90, expand=True) if img.size[1] > img.size[0] else img
    return {42: lambda: art.silo(2, r), 43: lambda: art.shed(3, 2, r), 44: lambda: art.barn(3, 3, r), 45: lambda: art.windmill(r, 0),
            50: lambda: art.water_tower(r), 51: lambda: art.pump(r),
            **{52 + i: (lambda c=c: art.barrel(r, c)) for i, c in enumerate((BARREL_COLOURS[915], BARREL_COLOURS[914], BARREL_COLOURS[913], BARREL_COLOURS[912]))}
            }.get(index, lambda: None)()


def bank_picture(index, name):
    w, h, k = 112, 64, PIC_SCALE
    hue = (index * 0.0833) % 1.0
    top = tuple(int(255 * v) for v in colorsys.hsv_to_rgb(hue, 0.22, 0.97))
    bottom = tuple(int(255 * v) for v in colorsys.hsv_to_rgb(hue, 0.45, 0.78))
    img = Image.new("RGBA", (w * k, h * k))
    px = img.load()
    for y in range(h * k):
        c = lerp(top, bottom, y / (h * k - 1))
        for x in range(w * k):
            px[x, y] = c + (255,)
    sprite = bank_sprite(index)
    if sprite is not None:
        box_w, box_h = 100 * k, 44 * k
        f = min(box_w / sprite.size[0], box_h / sprite.size[1])
        sprite = sprite.resize((max(1, int(sprite.size[0] * f)), max(1, int(sprite.size[1] * f))), Image.LANCZOS)
        img.alpha_composite(sprite, ((w * k - sprite.size[0]) // 2, 3 * k + (box_h - sprite.size[1]) // 2))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, w * k - 1, h * k - 1], outline=(0, 0, 0, 255), width=k)
    if sprite is None:
        put_text(d, k, str(index), 3, 3, 40, 0, px=8, fill=(0x20, 0x20, 0x20, 255))
        put_text(d, k, name or "Picture", 4, 26, w - 8, 1, px=12, fill=(0x10, 0x10, 0x10, 255))
    else:
        put_text(d, k, name or "", 4, 51, w - 8, 1, px=9, fill=(0x10, 0x10, 0x10, 255))
    return img


def weather_sky(i):
    w, h, k = 112, 32, PIC_SCALE
    colours = [(0x55, 0xAA, 0xFF), (0x7A, 0xB8, 0xF0), (0xA0, 0xA8, 0xB4), (0x60, 0x68, 0x78)]
    img = Image.new("RGBA", (w * k, h * k), colours[i] + (255,))
    d = ImageDraw.Draw(img)
    if i <= 1:
        d.ellipse([80 * k, 4 * k, 100 * k, 24 * k], fill=(0xFF, 0xE1, 0x4D, 255))
    if i >= 1:
        for cx in (20, 50, 80)[: 1 + i]:
            d.ellipse([(cx - 14) * k, 8 * k, (cx + 14) * k, 22 * k], fill=(0xF0, 0xF0, 0xF4, 255) if i < 3 else (0x90, 0x94, 0xA0, 255))
    if i == 3:
        for x in range(6, 108, 10):
            d.line([(x * k, 22 * k), ((x - 3) * k, 30 * k)], fill=(0xA0, 0xC8, 0xFF, 255), width=k)
    return img


def weather_ground(i):
    w, h, k = 112, 32, PIC_SCALE
    colours = [(0xE8, 0xF0, 0xF8), (0x7C, 0xC0, 0x5C), (0x3C, 0x9A, 0x3A), (0xC8, 0x8C, 0x3C)]  # winter, spring, summer, autumn
    img = Image.new("RGBA", (w * k, h * k), colours[i] + (255,))
    d = ImageDraw.Draw(img)
    for x in range(4, 112, 14):
        d.line([(x * k, h * k), ((x + 3) * k, (h - 9) * k)], fill=shade(colours[i], 0.8) + (255,), width=k)
    return img


# ---- the sound effects: tones, sweeps and noise made with nothing but arithmetic (8 kHz, 16 bits, one channel)
RATE = 8000


def _env(n, i, attack=0.02, release=0.3):
    t = i / float(n)
    return min(1.0, t / attack if attack else 1.0) * (1.0 - max(0.0, (t - (1 - release)) / release))


def tone(freq, dur, vol=0.5, vibrato=0.0, square=False, f1=None):
    n = int(RATE * dur)
    out, phase = [], 0.0
    for i in range(n):
        f = freq if f1 is None else freq + (f1 - freq) * i / n
        f *= 1.0 + vibrato * math.sin(2 * math.pi * 6 * i / RATE)
        phase += 2 * math.pi * f / RATE
        v = math.sin(phase)
        out.append((1.0 if v >= 0 else -1.0) * 0.6 * _env(n, i) * vol if square else v * _env(n, i) * vol)
    return out


def noise(dur, vol=0.5, smooth=0.0, decay=True, seed=1):
    rng = random.Random(seed)
    n = int(RATE * dur)
    out, prev = [], 0.0
    for i in range(n):
        prev = prev * smooth + rng.uniform(-1, 1) * (1 - smooth)
        out.append(prev * vol * (1.0 - i / n if decay else _env(n, i, 0.2, 0.4)))
    return out


def mix(*parts):
    n = max(len(p) for p in parts)
    return [sum(p[i] for p in parts if i < len(p)) for i in range(n)]


def save_wav(path, samples):
    peak = max(1e-6, max(abs(v) for v in samples))
    data = struct.pack("<%dh" % len(samples), *[int(max(-1.0, min(1.0, v / max(1.0, peak))) * 30000) for v in samples])
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(data)


def sound_effects():
    """The effects the game asks for by name, as sample lists."""
    chirps = lambda f0, f1, k, seed: sum((tone(f0 + 200 * j, 0.07, 0.4, f1=f1 + 200 * j) + [0.0] * int(RATE * 0.04) for j in range(k)), [])
    return {
        "click.voc": noise(0.04, 0.8, 0.2),
        "uhoh.voc": tone(440, 0.16, 0.5, square=True) + tone(330, 0.22, 0.5, square=True),
        "plopitem.voc": tone(300, 0.12, 0.6, f1=620),
        "ploptool.voc": tone(620, 0.09, 0.6, f1=300),
        "bulldoze.voc": mix(noise(0.55, 0.7, 0.85, decay=False, seed=2), tone(70, 0.55, 0.5, vibrato=0.1)),
        "cheers.voc": mix(noise(0.9, 0.5, 0.35, decay=False, seed=3), tone(520, 0.9, 0.12, vibrato=0.04)),
        "boos.voc": tone(150, 0.8, 0.6, vibrato=0.06, f1=110),
        "explode.voc": mix(noise(0.8, 0.9, 0.7, seed=4), tone(55, 0.8, 0.6, f1=30)),
        "bird1.voc": chirps(1800, 2400, 3, 1), "bird2.voc": chirps(2400, 1900, 4, 2),
        "cow1.voc": tone(120, 0.7, 0.6, vibrato=0.05, f1=100), "cow2.voc": tone(95, 0.9, 0.6, vibrato=0.07, f1=130),
        "horse1.voc": tone(750, 0.55, 0.5, vibrato=0.08, f1=300),
        "pig.voc": tone(210, 0.12, 0.5, square=True) + [0.0] * 600 + tone(180, 0.14, 0.5, square=True),
        "sheep1.voc": tone(360, 0.5, 0.5, vibrato=0.12), "sheep2.voc": tone(420, 0.4, 0.5, vibrato=0.15, f1=340),
        "rooster.voc": tone(500, 0.18, 0.5, f1=900) + tone(900, 0.2, 0.5, f1=700) + tone(700, 0.3, 0.5, f1=380),
        "thunder.voc": mix(noise(1.4, 0.9, 0.9, seed=5), tone(45, 1.4, 0.4, f1=30)),
        "wind.voc": noise(1.2, 0.5, 0.8, decay=False, seed=6),
        "flood.voc": mix(noise(1.0, 0.5, 0.6, decay=False, seed=7), tone(160, 1.0, 0.2, vibrato=0.2)),
        "plane.voc": mix(tone(90, 0.9, 0.5, vibrato=0.03, square=True), noise(0.9, 0.2, 0.8, decay=False, seed=8)),
        "swarm.voc": mix(tone(210, 0.8, 0.4, vibrato=0.25, square=True), noise(0.8, 0.2, 0.3, decay=False, seed=9)),
        "tornado.voc": mix(noise(1.2, 0.6, 0.88, decay=False, seed=10), tone(60, 1.2, 0.4, vibrato=0.2)),
        "smack.voc": noise(0.12, 0.9, 0.3, seed=11),
        "compbeep.voc": tone(880, 0.16, 0.5, square=True),
    }


def grid(images, columns):
    cw, ch = images[0].size
    rows = (len(images) + columns - 1) // columns
    out = Image.new("RGBA", (cw * columns, ch * rows), (0, 0, 0, 0))
    for i, im in enumerate(images):
        out.paste(im, ((i % columns) * cw, (i // columns) * ch))
    return out


def build_pictures(out):
    """Writes the interface pictures under out/ui and returns the manifest's "pictures" block."""
    ui = os.path.join(out, "ui")
    os.makedirs(ui, exist_ok=True)
    single, sets = {}, []
    layout = source_layout()
    for i, (name, (w, h)) in enumerate(sorted(WINDOWS.items())):
        img = toolbar() if name == "cntlbtnh.bmp" else panel(w, h, name, lerp((0xC8, 0xC8, 0xC8), band_colour(i * 5, 0.25, 0.92), 0.5), layout.get(name, []))[0]
        img.save(os.path.join(ui, name.replace(".bmp", ".png")), optimize=True)
        single[name] = {"file": "ui/" + name.replace(".bmp", ".png")}
    editeasy().save(os.path.join(ui, "editeasy.png"), optimize=True)
    single["editeasy.lzs"] = {"file": "ui/editeasy.png"}
    for i, name in enumerate(FULL_SCREENS):
        img = start_picture(name, i, "region") if name == "scenario.int" else start_picture(name, i, "design") if name == "defworld.int" else full_screen(name, i)
        img.save(os.path.join(ui, name.replace(".int", ".png")), optimize=True)
        single[name] = {"file": "ui/" + name.replace(".int", ".png")}
    for fi, (name, count, (w, h), names) in enumerate(FACE_FILES):
        faces = [face(w, h, names[i] if names else str(i), i + fi * 3) for i in range(count)]
        png = "ui/" + name.replace(".til", "-faces.png")
        columns = 6 if count > 6 else count
        grid(faces, columns).save(os.path.join(out, png), optimize=True)
        sets.append({"key": name + "${n}", "file": png, "count": count, "columns": columns})
    names = bank_names()
    grid([bank_picture(i, names.get(i)) for i in range(96)], 8).save(os.path.join(out, "ui", "alltiles-bank.png"), optimize=True)
    sets.append({"key": "alltiles.btl#{n}", "file": "ui/alltiles-bank.png", "count": 96, "columns": 8})
    grid([weather_sky(i) for i in range(4)], 4).save(os.path.join(out, "ui", "wethsky-bank.png"), optimize=True)
    sets.append({"key": "wethsky.btl#{n}", "file": "ui/wethsky-bank.png", "count": 4, "columns": 4})
    grid([weather_ground(i) for i in range(4)], 4).save(os.path.join(out, "ui", "wethgrnd-bank.png"), optimize=True)
    sets.append({"key": "wethgrnd.btl#{n}", "file": "ui/wethgrnd-bank.png", "count": 4, "columns": 4})
    for si, (name, frames) in enumerate(SPRITES):
        png = "ui/" + name.replace(".spr", "-frames.png")
        grid([sprite_frame(name, f, si) for f in range(frames)], min(frames, 4)).save(os.path.join(out, png), optimize=True)
        sets.append({"key": name + "@{n}", "file": png, "count": frames, "columns": min(frames, 4)})
    return {"scale": PIC_SCALE, "single": single, "sets": sets}


# ----------------------------------------------------------------------------------------------- data tables
# Written as the "tables" block of tilepack.json (engine/src/assets/PackTables.h describes each).

def climate_tables():
    """12 climate tables of 240 weeks (5 years of 48): seasons and weather by class 0 (dry, cool) .. 3 (wet, hot).

    The soil's wetness is the one thing the weather tables must get right: the engine keeps an accumulator (a rain day
    adds 8, a dry day takes about 1.4 off) and moves the wetness 0..12 by acc >> 3 whenever it passes 8 either way, so
    rain classes 3 and up add 2 or more steps a week and the soil soaks to 12 for good. The rain class of each week is
    therefore chosen by simulating that model and steering the wetness round a target for the climate class, with a
    spring wet season and some luck, so that a crop's water range of about 3..21 (moisture is twice the wetness) is met
    most weeks.
    """
    rng = random.Random(1993)
    mean = [45, 55, 65, 78]            # degrees F
    swing = [22, 24, 26, 28]
    target = [4.0, 5.5, 7.0, 8.5]      # the wetness the rain steers round
    out = {"rain": [], "temperature": [], "wind": []}
    for cls in range(4):
        rain, temp, wind = [], [], []
        wet, acc = 6.0, 0.0
        for week in range(240):
            season = math.sin((week % 48 - 12) / 48.0 * 2 * math.pi)  # warmest in week 36 (midsummer in a year from January)
            temp.append(int(round(mean[cls] + swing[cls] * season + rng.uniform(-4, 4))))
            wetter = 0.8 * math.sin((week % 48 - 6) / 48.0 * 2 * math.pi)  # a wet spring, a drier late summer
            want = (target[cls] + wetter - wet) * 0.45 + rng.uniform(-1.0, 1.0)
            step = max(-2, min(3, int(round(want))))
            days = {-2: 0, -1: rng.choice([0, 1]), 0: 1, 1: 2, 2: 3, 3: 4}[step]  # the rain class is the number of rain days
            rain.append(days)
            wind.append(int(2 + cls * 3 + rng.uniform(0, 6)))
            # the engine's accumulator: a rain day adds 8; a dry day takes off 2, 1 or 1 (clear, mostly clear, cloudy) in a
            # cool week (below 70 F), 1, 1 or 2 .. in a warm one: on average 1.25, 1.5 and 1.75 a day for weeks up to 70,
            # up to 85 and warmer (Simulation::DrySky)
            t = temp[-1]
            acc += 8 * days - (7 - days) * (1.25 if t <= 70 else 1.5 if t <= 85 else 1.75)
            if abs(acc) >= 8:
                wet = max(0.0, min(12.0, wet + math.floor(acc / 8)))
                acc = 0.0
        out["rain"].append(rain)
        out["temperature"].append(temp)
        out["wind"].append(wind)
    return out


def region_grids():
    """The region map: rain from dry in the west to wet in the east, temperature from cold in the north to hot in the
    south, wind in diagonal bands; every cell but the corners is somewhere a farm may be chosen."""
    rows = {"rain": [], "temperature": [], "wind": [], "hotpoints": []}
    for y in range(18):
        r, t, w, h = "", "", "", ""
        for x in range(18):
            r += "abcd"[min(3, x * 4 // 18)]
            t += "abcd"[min(3, y * 4 // 18)]
            w += "abcd"[((x + y) // 5) % 4]
            corner = (x < 2 or x > 15) and (y < 2 or y > 15)
            h += "a" if corner else "b"
        rows["rain"].append(r)
        rows["temperature"].append(t)
        rows["wind"].append(w)
        rows["hotpoints"].append(h)
    return rows


def town_blocks():
    """15 town plot templates, 8 x 8 each: bare ground (15) with a dirt-road cross (the engine joins the pieces); the
    airport (type 5) a paved runway across the ground."""
    blocks = []
    for t in range(15):
        b = [[15] * 8 for _ in range(8)]
        if t == 5:
            for i in range(8):
                b[3][i] = 151
        else:
            for k in range(8):
                b[k][3] = 161       # north-south dirt piece
                b[3][k] = 162       # east-west dirt piece
            b[3][3] = 171           # the crossing
        blocks.append(b)
    return blocks


def event_texts():
    """The message boxes' texts by event id: the engine's own English strings, found in its source where an event is
    raised with an id and a Tr("...") (game/*.cpp), else a plain placeholder."""
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src")
    found = {}
    pattern = re.compile(r'(?:Event|DisasterEvent)\(\s*(\d+)\s*,\s*\d+\s*,\s*Tr(?:F)?\(\s*"((?:[^"\\]|\\.)*)"')
    for path in glob.glob(os.path.join(src, "game", "*.cpp")):
        with open(path, encoding="utf-8", errors="replace") as f:
            for m in pattern.finditer(f.read()):
                found.setdefault(int(m.group(1)), m.group(2).replace('\\"', '"').replace("{0}", "..."))
    found.setdefault(12, "This is a placeholder card.")
    found.setdefault(45, "Do you want to quit the game?")
    found.setdefault(61, "Bet 500 on a rodeo rider?")
    events = []
    for eid in range(0, 80):
        text = found.get(eid, "Placeholder message %d." % eid)
        text = "".join(c for c in text if " " <= c <= "z" and c not in "@~`")
        lines, cur = [], ""
        for word in text.split():
            word = word[:27]
            if cur and len(cur) + 1 + len(word) > 27:
                lines.append(cur)
                cur = ""
            cur = (cur + " " + word).strip()
        if cur:
            lines.append(cur)
        events.append({"id": eid, "title": "NOTICE %d" % eid, "lines": lines[:6]})
    return events


def family_of(tile):
    """The tile family that starts at `tile` (the first one when several do): an item's family is also its text's id."""
    return FAMILY_START.index(tile)


# What each catalogue record is, by the engine's own role table (game/Shop.cpp, game/Machines.cpp, game/Simulation.cpp):
# (record, name, first tile of its picture, ledger code "kind", price, size (across, down)). The prices are placeholder
# balance; the sizes are the number of consecutive tiles each building's picture takes (small silo 1, large silo 2 x 2,
# small shed 3 x 2, large shed and small barn 3 x 3, large barn 4 x 3).
MACHINES = [  # record = the machine's type; the tile is its upright picture's first (overlay 0x350.. + 848)
    (0, "Tractor", 848, 6000, (1, 1)), (1, "Plow", 852, 1800, (1, 1)), (2, "Planter", 856, 2800, (1, 1)),
    (3, "Sprayer", 868, 2200, (1, 1)), (4, "Harvester", 860, 7500, (1, 2)), (5, "Trailer", 876, 1400, (1, 2)),
    (7, "Truck", 896, 4500, (1, 2)), (8, "Crop Duster", 1028, 12000, (1, 1)),
]
BUILDINGS = [  # records 12..20, kind 0x40..0x48
    ("Small Silo", 440, 500, (1, 1)), ("Large Silo", 441, 1400, (2, 2)), ("Small Shed", 445, 900, (3, 2)),
    ("Large Shed", 451, 1900, (3, 3)), ("Small Barn", 460, 1600, (3, 3)), ("Large Barn", 469, 3000, (4, 3)),
    ("Water Pump", 120, 1000, (1, 1)), ("Windmill", 1010, 1300, (1, 1)), ("Water Tower", 481, 1700, (1, 1)),
]
ANIMALS = [("Horse", 946, 800), ("Cow", 962, 600), ("Pig", 978, 300), ("Sheep", 994, 200)]  # records 45..48, kind 0xA0 + species
SUPPLIES = [("Fertilizer", 915, 40), ("Pesticide", 914, 50), ("Herbicide", 913, 45), ("Fungicide", 912, 55)]  # records 82..85, kind 0xDC..
CROPS = ["Corn", "Wheat", "Barley", "Potatoes", "Oats", "Rice", "Sorghum", "Soybeans", "Cotton", "Onions", "Tomatoes", "Lettuce", "Carrots",
         "Strawberries", "Sugar", "Peanuts"]


# ----------------------------------------------------------------------------------------------- crops
# The crops (engine/src/game/PackCrops.h describes each parameter): the 16 a new world starts with, then 8 to choose from.
# (file, name, temperature class, water class, weeks to harvest, yield, price, special). The numbers are placeholder
# balance: yield x price / 10 is what a field is worth, a seed bag costs a quarter of seedBase.
TEMP_CLASS = {"cool": (40, 50), "warm": (50, 100), "hot": (55, 130)}   # baseTemp F, weekly heat needed
WATER_CLASS = {"dry": (3, 16), "mid": (5, 19), "wet": (8, 24)}        # moisture range (0..24)
CROP_ROWS = [
    ("corn.crp", "Corn", "warm", "mid", 18, 300, 300, ""), ("wheat.crp", "Wheat", "cool", "dry", 16, 250, 220, ""),
    ("barley.crp", "Barley", "cool", "dry", 14, 240, 200, ""), ("potatoes.crp", "Potatoes", "cool", "mid", 15, 350, 180, ""),
    ("oats.crp", "Oats", "cool", "mid", 13, 230, 160, ""), ("rice.crp", "Rice", "hot", "wet", 20, 330, 260, ""),
    ("sorghum.crp", "Sorghum", "hot", "dry", 17, 260, 190, ""), ("soybeans.crp", "Soybeans", "warm", "mid", 16, 280, 280, ""),
    ("cotton.crp", "Cotton", "hot", "mid", 22, 220, 420, ""), ("onions.crp", "Onions", "cool", "mid", 14, 300, 210, ""),
    ("tomatoes.crp", "Tomatoes", "warm", "wet", 15, 360, 320, ""), ("lettuce.crp", "Lettuce", "cool", "wet", 9, 280, 170, ""),
    ("carrots.crp", "Carrots", "cool", "mid", 12, 310, 200, ""), ("strawber.crp", "Strawberries", "warm", "wet", 12, 260, 480, "hand"),
    ("sugar.crp", "Sugar Cane", "hot", "wet", 24, 400, 150, ""), ("peanuts.crp", "Peanuts", "warm", "dry", 18, 240, 330, ""),
    ("gladiolu.crp", "Gladiolus", "cool", "mid", 12, 180, 450, "hand"), ("sweet.crp", "Sweet Corn", "warm", "mid", 14, 260, 280, ""),
    ("tobacco.crp", "Tobacco", "hot", "mid", 20, 200, 520, "hand"), ("sunflowe.crp", "Sunflowers", "warm", "dry", 16, 240, 250, ""),
    ("grapes.crp", "Grapes", "warm", "mid", 18, 220, 500, "orchard30"), ("oranges.crp", "Oranges", "hot", "mid", 20, 240, 440, "orchard40"),
    ("almonds.crp", "Almonds", "warm", "dry", 20, 200, 560, "orchard34"), ("apples.crp", "Apples", "cool", "mid", 18, 260, 460, "orchard36"),
]


CROP_COLOURS = [art.RIPE[row[1]] for row in CROP_ROWS]  # the colour of each crop's harvest (signs, popups, icons)


def crop_block(size, out):
    """The crops block, and a strip of 14 pictures for each crop under out/crops."""
    os.makedirs(os.path.join(out, "crops"), exist_ok=True)
    crops = []
    for i, (file, name, temp, water, weeks, yld, price, special) in enumerate(CROP_ROWS):
        base_temp, heat = TEMP_CLASS[temp]
        wmin, wmax = WATER_CLASS[water]
        params = {"weeks": weeks, "stages": 8, "baseTemp": base_temp, "heat": heat, "yield": yld, "price": price,
                  "seedBase": max(20, price // 3), "waterMin": wmin, "waterMax": wmax,
                  "drainA": 3 + i % 3, "drainB": 2 + i % 4, "drainC": 2 + i % 2, "wetPenalty": 3 + i % 3, "dryPenalty": 3 + (i + 1) % 3}
        if special.startswith("orchard"):
            params.update({"perennial": True, "orchard": True, "handPicked": True, "harvestWeek": int(special[7:]), "regrowAge": 1})
        elif special == "hand":
            params["handPicked"] = True
        # resistance is a threshold: each week a random 1..255 above it adds the excess (at most 64) to the crop's pest, weed
        # or disease counter, and any resistance at all quarters what a spray changes; 0 is the fastest growth there is, so
        # the crops get 120..230, hardier ones the more, delicate (hand-picked) ones the less
        delicate = special == "hand"
        for k, name_ in enumerate(("resistPests", "resistWeeds", "resistDisease")):
            params[name_] = (110 if delicate else 150) + (i * 37 + k * 53) % 80
        strip = Image.new("RGBA", (14 * size, size), (0, 0, 0, 0))
        for k in range(8):
            strip.paste(draw_crop_stage(size, i, k), (k * size, 0))
        sign = draw_sign(size, i)
        for q in range(4):
            strip.paste(sign.crop(((q % 2) * size, (q // 2) * size, (q % 2 + 1) * size, (q // 2 + 1) * size)), ((8 + q) * size, 0))
        strip.paste(draw_icon(size, i, (0xC8, 0xB0, 0x78)), (12 * size, 0))
        strip.paste(draw_icon(size, i, (0x4C, 0xA0, 0x4C)), (13 * size, 0))
        strip.save(os.path.join(out, "crops", file.replace(".crp", ".png")), optimize=True)
        temp_word = {"cool": "Cool", "warm": "Warm", "hot": "Hot"}[temp]
        rain_word = {"dry": "Low", "mid": "Medium", "wet": "High"}[water]
        crops.append({"file": file, "name": name, "params": params, "art": "crops/" + file.replace(".crp", ".png"),
                      "help": [f"{temp_word} season crop.", f"Rain: {rain_word}.", f"{weeks} weeks to grow."],
                      "info": f"{name}~Temperature: {temp_word}~Rain: {rain_word}~Days: {weeks * 7}"})
    return crops


def template_block():
    """The template farm: the item catalogue (103 records of 18 bytes) the engine starts every new world from."""
    recs = []
    for rec, name, tile, price, size in MACHINES:
        recs.append({"id": rec, "family": family_of(tile), "kind": rec, "price": price, "size": list(size), "turns": 2})
    for k, (name, tile, price, size) in enumerate(BUILDINGS):
        recs.append({"id": 12 + k, "family": family_of(tile), "kind": 0x40 + k, "price": price, "size": list(size)})
    for c in range(16):  # the crops' market records: family 18 + c (the growth pictures), ledger code 168 + c; prices at world creation
        recs.append({"id": 21 + c, "family": 18 + c, "kind": 168 + c, "price": 100})
    for sp, (name, tile, price) in enumerate(ANIMALS):
        recs.append({"id": 45 + sp, "family": family_of(tile), "kind": 0xA0 + sp, "price": price})
    for c in range(16):  # the crops' stored stacks and seeds: prices at world creation
        recs.append({"id": 50 + c, "kind": 168 + c, "price": 100})
        recs.append({"id": 66 + c, "kind": 0xC0 + c, "price": 25})
    for k, (name, tile, price) in enumerate(SUPPLIES):
        recs.append({"id": 82 + k, "family": family_of(tile), "kind": 0xDC + k, "price": price})
    recs.sort(key=lambda r: r["id"])
    return {"version": 1, "catalogue": recs}


DESCRIPTIONS = {
    "Tractor": "Pulls the plow and other tools.", "Plow": "Turns the soil before planting.", "Planter": "Sows seed in rows.",
    "Sprayer": "Sprays chemicals on a field.", "Harvester": "Cuts and gathers the crop.", "Trailer": "Hauls the harvest.",
    "Truck": "Carries goods to town.", "Crop Duster": "A plane that sprays fields.", "Small Silo": "Holds a little grain.",
    "Large Silo": "Holds a lot of grain.", "Small Shed": "Shelters machines.", "Large Shed": "Shelters more machines.",
    "Small Barn": "Stores seed and spray.", "Large Barn": "Stores much seed and spray.", "Water Pump": "Draws water for ditches.",
    "Windmill": "Pumps water free.", "Water Tower": "Stores water for ditches.", "Horse": "Eats hay and grazes.",
    "Cow": "Gives milk.", "Pig": "Eats almost anything.", "Sheep": "Grazes and gives wool.", "Fertilizer": "Feeds the soil.",
    "Pesticide": "Kills crop pests.", "Herbicide": "Kills weeds.", "Fungicide": "Stops crop disease.",
}


def item_texts():
    """Item help texts by the id the engine looks them up with: the item's tile family (see template_block)."""
    items = {}
    for rec, name, tile, price, size in MACHINES:
        items[family_of(tile)] = (name, DESCRIPTIONS[name])
    for name, tile, price, size in BUILDINGS:
        items[family_of(tile)] = (name, DESCRIPTIONS[name])
    for name, tile, price in ANIMALS + SUPPLIES:
        items[family_of(tile)] = (name, DESCRIPTIONS[name])
    for c, name in enumerate(CROPS):
        items[18 + c] = (name, "A crop to grow.")
    return [{"id": i, "name": n, "description": d} for i, (n, d) in sorted(items.items())]


# The interface's 16 colours as RGB (engine/src/assets/Theme.h): what the interface draws fills, frames, text and the
# overview map's legend with. These are the engine's own defaults, written out so that a pack's author can change them.
THEME = {
    "black": "#000000", "yellow": "#ffe14d", "sky": "#55aaff", "red": "#d03030", "pink": "#f080c0", "navy": "#1c2a7a",
    "blue": "#3a5fd0", "tan": "#d8c08a", "green": "#3f9f3f", "darkGreen": "#1f6a2e", "brown": "#80502a",
    "colour11": "#70d8d8", "lightGrey": "#c4c4c4", "grey": "#868686", "darkGrey": "#484848", "white": "#ffffff",
}


# How rain and frost look on the map (engine/src/assets/Weather.h): the map is laid under a translucent veil; "strength"
# is its share, 0 none .. 1 the colour alone. The engine's own defaults, written out so that an author can change them.
WEATHER = {
    "rain": {"colour": "#30508c", "strength": 0.30},
    "frost": {"colour": "#e8f4ff", "strength": 0.45},
}


def overview_colours():
    """Overview map colours by tile family, RGB: a tile's 2 x 2 block is one colour or four (row by row). They are the
    interface theme's own colours, so the map matches the windows around it."""
    T = THEME
    ranges = [
        (15, 30, T["brown"]),                                       # ground
        (31, 38, T["green"]),
        (39, 69, T["blue"]),                                        # water
        (87, 97, [T["brown"], T["sky"], T["sky"], T["brown"]]),     # dry ditches
        (98, 108, T["sky"]),                                        # full ditches
        (128, 146, T["darkGreen"]),                                 # trees
        (150, 171, [T["grey"], T["tan"], T["tan"], T["grey"]]),     # roads
        (172, 173, [T["blue"], T["brown"], T["brown"], T["blue"]]), # bridges
        (177, 193, [T["brown"], T["green"], T["green"], T["brown"]]),  # fences
        (232, 300, T["red"]),                                       # buildings
        (440, 480, [T["red"], T["darkGrey"], T["darkGrey"], T["red"]]),  # closed buildings
        (483, 517, T["lightGrey"]),                                 # event grounds
        (535, 538, [T["darkGrey"], T["grey"], T["grey"], T["darkGrey"]]),  # rocks
        (688, 815, [T["green"], T["darkGreen"], T["green"], T["darkGreen"]]),  # crops
    ]
    for k in range(32):                   # the ramp of tiles 0x1D0.. the map modes use: tan to green
        step = k // 8
        pat = [[T["tan"]] * 4, [T["tan"], T["green"], T["green"], T["tan"]],
               [T["green"], T["green"], T["green"], T["tan"]], [T["green"], T["darkGreen"], T["darkGreen"], T["green"]]][step]
        ranges.append((0x1D0 + k, 0x1D0 + k, pat))
    return {"default": T["green"], "ranges": [{"from": a, "to": b, "colours": c} for a, b, c in ranges]}


def cards_block():
    """The Farm Expert: a welcome card with five topics, each a card of plain advice with a way back. Positions count from the
    window's top-left corner (the title bar is the first 16); text is wrapped by the engine."""
    topics = [
        (2, "Getting started", 66,
         "Click BUY to buy machines and buildings, then click your own land to place them. A farm needs a tractor, a plow, a planter, "
         "a sprayer, a harvester and a trailer. Plot a field, pick a crop from the list on the left and click the field to plant it. "
         "The date and your cash are at the top right."),
        (3, "Fields and crops", 0,
         "A field is a block of land you farm. Crops need warmth and water, and weeds, pests and disease slow them down: spray a field "
         "when its status shows a problem. Poor soil needs fertilizer. Harvest when the crop is ripe, or it spoils. Open EVAL to see how "
         "the farm is doing and which crops to try."),
        (4, "Machines and storage", 34,
         "A tractor pulls a plow and a planter. A harvester cuts the crop and a trailer carries it. Park machines in a shed or a barn "
         "so that they wear out slowly. A silo keeps the harvest until you sell it; seed and spray are kept in barns."),
        (5, "Weather and water", 57,
         "Open WTHR for today's weather and the forecast. Rain wets the soil. A field that is too dry or too wet grows badly: dig "
         "ditches, and add a pump, a windmill or a water tower to bring water where it is needed."),
        (6, "Money and the town", 76,
         "Open MKT for the price of each crop over the last thirty weeks, BAL for your balance sheet and BANK to borrow money. "
         "Taxes fall due every year. As the town grows it buys more, and it holds a fair or a rodeo now and then."),
    ]
    cards = [{"id": 1, "items": [
        {"picture": 66, "x": 24, "y": 40},
        {"text": "Hello! I am the Farm Expert. What would you like to know about?", "x": 150, "y": 44, "width": 230},
    ] + [item for row, (cid, title, pic, body) in enumerate(topics) for item in (
        {"text": title, "x": 40, "y": 92 + 24 * row + 8, "width": 200},
        {"button": 4, "x": 280, "y": 88 + 24 * row, "goto": cid})]}]
    for cid, title, pic, body in topics:
        cards.append({"id": cid, "items": [
            {"picture": pic, "x": 24, "y": 36},
            {"text": title.upper(), "x": 150, "y": 40, "width": 230},
            {"text": body, "x": 24, "y": 112, "width": 352},
            {"button": 3, "x": 336, "y": 200, "goto": 1}]})
    return cards


def build_sounds(out):
    """Writes the effects under out/sounds and returns the manifest's "sounds" block."""
    os.makedirs(os.path.join(out, "sounds"), exist_ok=True)
    block = {}
    for name, samples in sound_effects().items():
        save_wav(os.path.join(out, "sounds", name.replace(".voc", ".wav")), samples)
        block[name] = "sounds/" + name.replace(".voc", ".wav")
    return block


def build_tables():
    return {
        "climate": climate_tables(),
        "regions": region_grids(),
        "townLayout": {"blocks": town_blocks()},
        "events": event_texts(),
        "items": item_texts(),
        "overview": overview_colours(),
    }


# ----------------------------------------------------------------------------------------------- the pack

def sheet(tiles, cols, size):
    rows = (len(tiles) + cols - 1) // cols
    out = Image.new("RGBA", (cols * size, rows * size), (0, 0, 0, 0))
    for i, t in enumerate(tiles):
        out.paste(t, ((i % cols) * size, (i // cols) * size))
    return out


def build(size, labels):
    plain = [draw_plain(t, size, labels) for t in range(PLAIN_TILES)]
    # the pictures that are cut from bigger drawings
    for slot in range(16):
        for k in range(8):
            plain[CROP_GROWTH + 8 * slot + k] = draw_crop_stage(size, slot, k)
        sign = draw_sign(size, slot)
        for q in range(4):
            plain[SIGNS + 4 * slot + q] = sign.crop(((q % 2) * size, (q // 2) * size, (q % 2 + 1) * size, (q // 2 + 1) * size))
        plain[CROP_ICON_A + slot] = draw_icon(size, slot, (0xC8, 0xB0, 0x78))  # a tan frame
        plain[CROP_ICON_B + slot] = draw_icon(size, slot, (0x4C, 0xA0, 0x4C))  # a green frame
    for first, picture, w, h in ((440, art.silo(1, size), 1, 1), (441, art.silo(2, size), 2, 2), (445, art.shed(3, 2, size), 3, 2),
                                 (451, art.shed(3, 3, size), 3, 3), (460, art.barn(3, 3, size), 3, 3), (469, art.barn(4, 3, size), 4, 3)):
        for k in range(w * h):  # a building's tiles are numbered along x first
            plain[first + k] = picture.crop(((k % w) * size, (k // w) * size, (k % w + 1) * size, (k // w + 1) * size))
    plain[120] = art.pump(size)
    plain[481] = art.water_tower(size)
    plain[0x95] = art.water_tower(size, broken=True)
    plain[0xD9] = art.floor(size)  # inside a building: the floor a stored item lies on
    for k, level in enumerate((0.12, 0.4, 0.7, 1.0)):
        plain[0x53 + k] = art.tank_inside(size, level)  # inside a water tower
    block = draw_event_ground(size, 0)
    for k in range(35):
        plain[EVENT_GROUND + k] = block.crop(((k % 7) * size, (k // 7) * size, (k % 7 + 1) * size, (k // 7 + 1) * size))
    masked = [draw_masked(m, size) for m in range(MASKED_TILES)]
    events = []
    for bank in range(3):
        block = draw_event_ground(size, bank)
        events += [block.crop(((k % 7) * size, (k // 7) * size, (k % 7 + 1) * size, (k // 7 + 1) * size)) for k in range(35)]
    if labels:
        for t, img in enumerate(plain):
            if not (CROP_GROWTH <= t < CROP_GROWTH + 128 or SIGNS <= t < SIGNS + 64 or EVENT_GROUND <= t < EVENT_GROUND + 35 or CROP_ICON_A <= t < CROP_ICON_A + 32):
                label(img, str(t), size)
            elif CROP_GROWTH <= t < CROP_GROWTH + 128 and (t - CROP_GROWTH) % 8 == 0:
                label(img, str(t), size)
        for m, img in enumerate(masked):
            label(img, str(848 + m), size)
    for img in plain:
        px = img.load()  # the plain sheet is drawn opaque
        for y in range(size):
            for x in range(size):
                r, g, b, a = px[x, y]
                px[x, y] = (r, g, b, 255)
    return plain, masked, events


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--size", type=int, default=32, help="pixels a tile, 8..128 (default 32)")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "packs", "placeholder"))
    ap.add_argument("--labels", action="store_true", help="print each tile's number on it, for artists (the shipped pack is clean)")
    ap.add_argument("--no-pictures", action="store_true", help="leave the interface pictures out")
    ap.add_argument("--no-tables", action="store_true", help="leave the data tables out")
    ap.add_argument("--no-sounds", action="store_true", help="leave the sound effects out")
    a = ap.parse_args()
    if not 8 <= a.size <= 128:
        sys.exit("--size must be from 8 to 128")
    os.makedirs(a.out, exist_ok=True)
    plain, masked, events = build(a.size, a.labels)
    sheet(plain, PLAIN_COLS, a.size).save(os.path.join(a.out, "plain.png"), optimize=True)
    sheet(masked, MASKED_COLS, a.size).save(os.path.join(a.out, "masked.png"), optimize=True)
    sheet(events, EVENT_COLS, a.size).save(os.path.join(a.out, "events.png"), optimize=True)
    meta = {
        "format": 1,
        "name": "OpenSimFarm placeholder",
        "tileSize": a.size,
        "sheets": {
            "plain": {"file": "plain.png", "count": PLAIN_TILES, "columns": PLAIN_COLS},
            "masked": {"file": "masked.png", "count": MASKED_TILES, "columns": MASKED_COLS},
            "events": {"file": "events.png", "count": EVENT_TILES, "columns": EVENT_COLS},
        },
    }
    if not a.no_pictures:
        meta["pictures"] = build_pictures(a.out)
    if not a.no_sounds:
        meta["sounds"] = build_sounds(a.out)
    meta["theme"] = THEME
    meta["weather"] = WEATHER
    if not a.no_tables:
        meta["template"] = template_block()
        meta["crops"] = crop_block(a.size, a.out)
        meta["cards"] = cards_block()
    if not a.no_tables:
        meta["tables"] = build_tables()
    with open(os.path.join(a.out, "tilepack.json"), "w") as f:
        # one line for each short array keeps the file readable and small
        text = json.dumps(meta, indent=2)
        text = re.sub(r"\[\s+((?:-?\d+,?\s*)+)\]", lambda m: "[" + re.sub(r"\s+", " ", m.group(1).strip()) + "]", text)
        f.write(text + "\n")
    print("wrote", os.path.abspath(a.out), f"({a.size} px tiles)")


if __name__ == "__main__":
    main()
