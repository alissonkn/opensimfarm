#!/usr/bin/env python3
"""Makes OpenSimFarm's placeholder tile pack: full-colour, flat vector-style tiles at any size from 8 to 128 px,
drawn from nothing but the engine's own tile tables (nothing of the original game is read or copied).

    python3 engine/tools/make_placeholder_pack.py [--size 32] [--out engine/packs/placeholder] [--no-labels]

Every one of the 848 plain, 200 masked and 105 event tiles is made, in the PNG-sheet format of
engine/TRUECOLOUR.md. The families the engine knows by name (ground, water, ditches, roads, fences, gates, trees,
rocks, bridges, crop pictures, event grounds) are drawn as what they are, connected pieces with the right arms; every
other tile is coloured by the tile family it belongs to. Each tile carries its number (--no-labels leaves them off),
so the pack doubles as a map of what the engine uses where. Needs Pillow (pip install pillow).
"""
import argparse
import colorsys
import json
import os
import sys

from PIL import Image, ImageDraw

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

# 16 crop slots, a hue each
CROP_COLOURS = [colorsys.hsv_to_rgb(i / 16.0, 0.75, 0.9) for i in range(16)]
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
    """Growth tile `stage` (0..7) of a crop slot: rows of plants that grow, the last stage in the crop's colour."""
    p = Painter(size, size, shade((0x6A, 0x4A, 0x32), 1.0) + (255,))
    for k in range(4):
        y = size * (0.12 + 0.24 * k)
        p.line([(0, y), (size, y)], (0x55, 0x3A, 0x26, 255), size * 0.05)  # furrows
    col = CROP_COLOURS[slot]
    green = (0x3E, 0x9A, 0x3A)
    t = stage / 7.0
    leaf = lerp(green, col, max(0.0, (t - 0.55) / 0.45))
    for row in range(4):
        for cx in (0.25, 0.75):
            x, y = size * cx, size * (0.2 + 0.24 * row)
            r = size * (0.015 + 0.075 * t) if stage else size * 0.012
            p.ellipse(x - r, y - r, x + r, y + r, leaf + (255,))
            if stage >= 6:
                p.ellipse(x - r * 0.4, y - r * 0.4, x + r * 0.4, y + r * 0.4, shade(col, 1.25) + (255,))
    return p.finish()


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
    p = Painter(size, size, frame + (255,))
    p.rect(size * 0.08, size * 0.08, size * 0.92, size * 0.92, (0x22, 0x22, 0x22, 255))
    p.ellipse(size * 0.22, size * 0.22, size * 0.78, size * 0.78, CROP_COLOURS[slot] + (255,))
    with p.group():
        p.ellipse(size * 0.34, size * 0.3, size * 0.5, size * 0.44, (255, 255, 255, 110))
    return p.finish()


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

def draw_masked(m, size):
    """One masked tile (RGBA, see-through around the picture); overlay id 0x350 + m, tile id 848 + m."""
    t = 848 + m
    p = Painter(size, size, (0, 0, 0, 0))
    c = band_colour(t, s=0.55, v=0.85)
    if HAY <= t < HAY + 5:
        with p.group():
            p.ellipse(size * 0.15, size * 0.55, size * 0.85, size * 0.9, (0, 0, 0, 60))
        p.rect(size * 0.2, size * 0.3, size * 0.8, size * 0.75, (0xD8, 0xB8, 0x48, 255))
        for k in range(1, 4):
            p.line([(size * 0.2, size * (0.3 + 0.11 * k)), (size * 0.8, size * (0.3 + 0.11 * k))], (0xA8, 0x88, 0x28, 255), size * 0.025)
    else:
        with p.group():
            p.ellipse(size * 0.12, size * 0.5, size * 0.88, size * 0.92, (0, 0, 0, 70))  # shadow
        p.ellipse(size * 0.14, size * 0.14, size * 0.86, size * 0.8, c + (255,), shade(c, 0.55) + (255,), size * 0.035)
        with p.group():
            p.ellipse(size * 0.3, size * 0.24, size * 0.5, size * 0.4, (255, 255, 255, 90))
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
    ap.add_argument("--no-labels", action="store_true", help="leave the tile numbers off")
    a = ap.parse_args()
    if not 8 <= a.size <= 128:
        sys.exit("--size must be from 8 to 128")
    os.makedirs(a.out, exist_ok=True)
    plain, masked, events = build(a.size, not a.no_labels)
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
    with open(os.path.join(a.out, "tilepack.json"), "w") as f:
        json.dump(meta, f, indent=2)
        f.write("\n")
    print("wrote", os.path.abspath(a.out), f"({a.size} px tiles)")


if __name__ == "__main__":
    main()
