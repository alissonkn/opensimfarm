"""Drawn art for the placeholder pack: crops, machines, animals, buildings, and their insides.

Everything here is made from simple shapes (nothing of the original game is read or copied). Each function draws at
`s` pixels a tile and returns an RGBA picture; make_placeholder_pack.py cuts those into tiles and bank pictures.
Needs the generator's Painter class, which it installs with `install` (to keep the two files from importing each other).
"""
import math
from PIL import Image

Painter = None
ground = None  # function(i) -> RGB of ground tile 15 + i


def install(painter_cls, ground_colour):
    global Painter, ground
    Painter, ground = painter_cls, ground_colour


def shade(c, f):
    return tuple(max(0, min(255, int(x * f))) for x in c[:3])


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


class Cv:
    """A Painter drawn in cell units: 1.0 is one tile across. `z`, `ox`, `oy` zoom and shift what is drawn (used to draw a
    plant big on an icon)."""

    def __init__(self, w, h, s, background=None):
        self.s = s
        self.z, self.ox, self.oy = 1.0, 0.0, 0.0
        self.p = Painter(int(round(w * s)), int(round(h * s)), background)

    def _x(self, x):
        return (self.ox + x * self.z) * self.s

    def _y(self, y):
        return (self.oy + y * self.z) * self.s

    @staticmethod
    def _c(fill):
        return fill + (255,) if fill and len(fill) == 3 else fill

    def rect(self, x0, y0, x1, y1, fill):
        self.p.rect(self._x(x0), self._y(y0), self._x(x1), self._y(y1), self._c(fill))

    def ellipse(self, x0, y0, x1, y1, fill, outline=None, width=0.0):
        self.p.ellipse(self._x(x0), self._y(y0), self._x(x1), self._y(y1), self._c(fill), self._c(outline), width * self.z * self.s)

    def line(self, pts, fill, width):
        self.p.line([(self._x(x), self._y(y)) for x, y in pts], self._c(fill), width * self.z * self.s)

    def poly(self, pts, fill):
        self.p.polygon([(self._x(x), self._y(y)) for x, y in pts], self._c(fill))

    def shadow(self, x0, y0, x1, y1, a=70):
        with self.p.group():
            self.p.ellipse(self._x(x0), self._y(y0), self._x(x1), self._y(y1), (0, 0, 0, a))

    def done(self):
        return self.p.finish()


DARK = (0x26, 0x26, 0x2A)
STEEL = (0x9A, 0xA0, 0xA8)
GLASS = (0xA8, 0xD8, 0xF0)
GOLD = (0xE0, 0xB8, 0x48)


def wheel(c, x0, y0, x1, y1):
    c.rect(x0, y0, x1, y1, DARK)
    n = max(2, int((y1 - y0) / 0.07))
    for k in range(1, n):
        y = y0 + (y1 - y0) * k / n
        c.line([(x0, y), (x1, y)], (0x4A, 0x4A, 0x50), 0.015)


# -------------------------------------------------------------------------------------------- machines (facing up)

def machine(kind, s, loaded=False, spraying=False):
    """A machine seen from above, its front at the top: one tile wide, `Cells` tall. Turn it with `turned`."""
    if kind == "tractor":
        c = Cv(1, 1, s)
        c.shadow(0.1, 0.2, 0.95, 0.95)
        wheel(c, 0.06, 0.5, 0.28, 0.9)
        wheel(c, 0.72, 0.5, 0.94, 0.9)
        wheel(c, 0.14, 0.1, 0.28, 0.36)
        wheel(c, 0.72, 0.1, 0.86, 0.36)
        c.line([(0.2, 0.25), (0.8, 0.25)], DARK, 0.03)
        c.rect(0.34, 0.06, 0.66, 0.56, (0xC8, 0x32, 0x28))
        c.rect(0.47, 0.06, 0.53, 0.56, (0xE8, 0x60, 0x50))
        c.rect(0.36, 0.04, 0.64, 0.09, DARK)
        c.rect(0.28, 0.5, 0.72, 0.9, (0x9E, 0x24, 0x1E))
        c.rect(0.32, 0.54, 0.68, 0.86, (0xD8, 0xD8, 0xD0))
        c.rect(0.36, 0.58, 0.64, 0.7, GLASS)
        c.ellipse(0.4, 0.14, 0.46, 0.2, DARK)
        return c.done()
    if kind == "plow":
        c = Cv(1, 1, s)
        c.shadow(0.1, 0.2, 0.9, 0.95, 50)
        c.poly([(0.5, 0.02), (0.36, 0.3), (0.64, 0.3)], STEEL)
        c.rect(0.12, 0.3, 0.88, 0.38, (0x70, 0x76, 0x80))
        for k, x in enumerate((0.2, 0.46, 0.72)):
            c.poly([(x, 0.38), (x + 0.1, 0.38), (x + 0.26, 0.92), (x + 0.04, 0.8)], STEEL)
            c.line([(x + 0.05, 0.4), (x + 0.15, 0.82)], (0xD0, 0xD4, 0xDA), 0.02)
        wheel(c, 0.04, 0.4, 0.12, 0.62)
        return c.done()
    if kind == "planter":
        c = Cv(1, 1, s)
        c.shadow(0.05, 0.2, 0.95, 0.95, 50)
        c.rect(0.5 - 0.04, 0.0, 0.5 + 0.04, 0.22, STEEL)
        c.rect(0.1, 0.22, 0.9, 0.34, (0x3C, 0x84, 0x3C))
        for x in (0.18, 0.54):
            c.rect(x, 0.36, x + 0.28, 0.7, (0x8C, 0x6A, 0x38))
            c.rect(x + 0.03, 0.39, x + 0.25, 0.5, (0xB8, 0x92, 0x58))
            c.ellipse(x + 0.08, 0.54, x + 0.2, 0.64, (0xE0, 0xC8, 0x60))
        for x in (0.2, 0.38, 0.62, 0.8):
            c.line([(x, 0.7), (x, 0.9)], DARK, 0.025)
        wheel(c, 0.02, 0.36, 0.1, 0.62)
        wheel(c, 0.9, 0.36, 0.98, 0.62)
        return c.done()
    if kind == "sprayer":
        c = Cv(1, 1, s)
        c.shadow(0.05, 0.25, 0.95, 0.95, 50)
        c.rect(0.5 - 0.03, 0.0, 0.5 + 0.03, 0.3, STEEL)
        c.ellipse(0.24, 0.3, 0.76, 0.88, (0xE8, 0xC8, 0x30), (0x8C, 0x74, 0x10), 0.03)
        c.ellipse(0.34, 0.38, 0.5, 0.52, (0xFF, 0xF0, 0x90, 150))
        c.rect(0.03, 0.62, 0.97, 0.68, (0x70, 0x76, 0x80))
        for x in (0.08, 0.3, 0.7, 0.92):
            c.ellipse(x - 0.03, 0.66, x + 0.03, 0.72, DARK)
            if spraying:
                with c.p.group():
                    c.ellipse(x - 0.07, 0.72, x + 0.07, 0.95, (0x60, 0xB0, 0xF0, 150))
        wheel(c, 0.1, 0.36, 0.2, 0.58)
        wheel(c, 0.8, 0.36, 0.9, 0.58)
        return c.done()
    if kind == "harvester":
        c = Cv(1, 2, s)
        c.shadow(0.0, 0.2, 1.0, 1.95, 60)
        c.rect(0.02, 0.04, 0.98, 0.24, (0xE0, 0xC0, 0x30))
        for k in range(10):
            x = 0.06 + 0.09 * k
            c.line([(x, 0.05), (x, 0.23)], (0x9C, 0x80, 0x18), 0.02)
        wheel(c, 0.04, 0.5, 0.2, 1.0)
        wheel(c, 0.8, 0.5, 0.96, 1.0)
        wheel(c, 0.16, 1.55, 0.28, 1.9)
        wheel(c, 0.72, 1.55, 0.84, 1.9)
        c.rect(0.2, 0.24, 0.8, 1.7, (0x3C, 0x8C, 0x3A))
        c.rect(0.28, 0.28, 0.72, 0.8, (0x2C, 0x6C, 0x2C))
        c.rect(0.32, 0.32, 0.68, 0.5, GLASS)
        c.rect(0.26, 0.86, 0.74, 1.5, (0x58, 0xA8, 0x54))
        for k in range(5):
            c.line([(0.28, 0.92 + 0.12 * k), (0.72, 0.92 + 0.12 * k)], (0x2C, 0x6C, 0x2C), 0.02)
        c.line([(0.74, 1.0), (1.0, 1.2)], STEEL, 0.05)
        return c.done()
    if kind == "trailer":
        c = Cv(1, 2, s)
        c.shadow(0.05, 0.2, 0.95, 1.95, 60)
        c.line([(0.5, 0.0), (0.5, 0.2)], STEEL, 0.06)
        c.rect(0.14, 0.18, 0.86, 1.82, (0x96, 0x62, 0x34))
        c.rect(0.2, 0.24, 0.8, 1.76, (0x6C, 0x44, 0x22))
        for k in range(1, 8):
            y = 0.24 + 1.52 * k / 8
            c.line([(0.2, y), (0.8, y)], (0x8A, 0x5A, 0x2E), 0.02)
        wheel(c, 0.04, 1.1, 0.14, 1.6)
        wheel(c, 0.86, 1.1, 0.96, 1.6)
        if loaded:
            c.rect(0.2, 0.24, 0.8, 1.76, GOLD)
            for k in range(14):
                x, y = 0.26 + (k * 37 % 50) / 100.0, 0.3 + (k * 53 % 140) / 100.0
                c.ellipse(x, y, x + 0.07, y + 0.06, (0xF4, 0xD8, 0x70))
        return c.done()
    if kind == "truck":
        c = Cv(1, 2, s)
        c.shadow(0.05, 0.15, 0.97, 1.95, 60)
        wheel(c, 0.03, 0.14, 0.14, 0.5)
        wheel(c, 0.86, 0.14, 0.97, 0.5)
        wheel(c, 0.03, 1.3, 0.14, 1.75)
        wheel(c, 0.86, 1.3, 0.97, 1.75)
        c.rect(0.14, 0.04, 0.86, 0.62, (0x34, 0x68, 0xC0))
        c.rect(0.2, 0.08, 0.8, 0.24, GLASS)
        c.rect(0.2, 0.28, 0.8, 0.56, (0x2A, 0x54, 0xA0))
        c.rect(0.12, 0.66, 0.88, 1.9, (0x70, 0x76, 0x80))
        c.rect(0.18, 0.72, 0.82, 1.84, (0x54, 0x58, 0x62))
        for k in range(1, 6):
            c.line([(0.18, 0.72 + 1.12 * k / 6), (0.82, 0.72 + 1.12 * k / 6)], (0x62, 0x66, 0x70), 0.02)
        if loaded:
            c.rect(0.18, 0.72, 0.82, 1.84, GOLD)
            for k in range(16):
                x, y = 0.24 + (k * 37 % 50) / 100.0, 0.78 + (k * 53 % 95) / 100.0
                c.ellipse(x, y, x + 0.07, y + 0.06, (0xF4, 0xD8, 0x70))
        return c.done()
    if kind == "plane":
        c = Cv(1, 1, s)
        c.shadow(0.1, 0.3, 0.95, 0.98, 60)
        c.rect(0.02, 0.36, 0.98, 0.52, (0xE8, 0xC8, 0x30))
        c.line([(0.02, 0.44), (0.98, 0.44)], (0xB8, 0x98, 0x10), 0.015)
        c.ellipse(0.42, 0.08, 0.58, 0.92, (0xC8, 0x32, 0x28))
        c.ellipse(0.46, 0.34, 0.54, 0.5, GLASS)
        c.rect(0.34, 0.8, 0.66, 0.86, (0xE8, 0xC8, 0x30))
        c.line([(0.5, 0.78), (0.5, 0.94)], (0xC8, 0x32, 0x28), 0.05)
        c.line([(0.28, 0.1), (0.72, 0.1)], (0x40, 0x40, 0x48), 0.025)
        return c.done()
    raise ValueError(kind)


def turned(img, heading4):
    """The picture of a machine facing up, turned clockwise to heading 0 up, 1 right, 2 down, 3 left."""
    return img.rotate(-90 * heading4, expand=True) if heading4 else img


def barrel(s, colour):
    c = Cv(1, 1, s)
    c.shadow(0.15, 0.7, 0.85, 0.95, 70)
    c.rect(0.24, 0.22, 0.76, 0.84, colour)
    c.rect(0.3, 0.22, 0.4, 0.84, shade(colour, 1.25))
    for y in (0.34, 0.72):
        c.rect(0.24, y, 0.76, y + 0.05, shade(colour, 0.6))
    c.ellipse(0.24, 0.72, 0.76, 0.92, shade(colour, 0.85))
    c.ellipse(0.24, 0.12, 0.76, 0.32, shade(colour, 1.1), shade(colour, 0.55), 0.02)
    c.rect(0.4, 0.46, 0.6, 0.62, (0xF4, 0xF0, 0xE0))
    return c.done()


def car(s, colour, heading4):
    c = Cv(1, 1, s)
    c.shadow(0.2, 0.2, 0.85, 0.95, 60)
    wheel(c, 0.22, 0.16, 0.32, 0.38)
    wheel(c, 0.68, 0.16, 0.78, 0.38)
    wheel(c, 0.22, 0.64, 0.32, 0.86)
    wheel(c, 0.68, 0.64, 0.78, 0.86)
    c.rect(0.28, 0.08, 0.72, 0.92, colour)
    c.rect(0.32, 0.22, 0.68, 0.34, GLASS)
    c.rect(0.32, 0.64, 0.68, 0.74, GLASS)
    c.rect(0.33, 0.36, 0.67, 0.62, shade(colour, 0.8))
    return turned(c.done(), heading4)


# -------------------------------------------------------------------------------------------------------- animals

def animal(species, s, adult, kick=False):
    """Species 0 horse, 1 cow, 2 pig, 3 sheep, seen from above facing up (turn it any angle with `spun`)."""
    c = Cv(1, 1, s)
    k = 1.0 if adult else 0.68
    cx, cy = 0.5, 0.52

    def at(x, y):
        return cx + (x - 0.5) * k, cy + (y - 0.5) * k

    def ell(x0, y0, x1, y1, fill, outline=None, w=0.0):
        a, b = at(x0, y0)
        d, e = at(x1, y1)
        c.ellipse(a, b, d, e, fill, outline, w * k)

    def seg(x0, y0, x1, y1, fill, w):
        a, b = at(x0, y0)
        d, e = at(x1, y1)
        c.line([(a, b), (d, e)], fill, w * k)

    c.shadow(0.2, 0.3, 0.85, 0.95, 55)
    if species == 0:  # horse
        brown = (0x8B, 0x55, 0x2B)
        seg(0.5, 0.82, 0.5, 0.98, (0x2A, 0x1C, 0x12), 0.05)
        for x in (0.34, 0.66):
            seg(x, 0.34, x, 0.4, (0x5A, 0x38, 0x1C), 0.07)
            seg(x, 0.7, x, 0.76, (0x5A, 0x38, 0x1C), 0.07)
        ell(0.34, 0.26, 0.66, 0.86, brown, shade(brown, 0.6), 0.02)
        ell(0.43, 0.04, 0.57, 0.34, shade(brown, 0.85), shade(brown, 0.55), 0.02)
        seg(0.5, 0.14, 0.5, 0.5, (0x2A, 0x1C, 0x12), 0.05)
        seg(0.43, 0.08, 0.41, 0.03, shade(brown, 0.6), 0.03)
        seg(0.57, 0.08, 0.59, 0.03, shade(brown, 0.6), 0.03)
    elif species == 1:  # cow
        white = (0xF2, 0xF0, 0xEA)
        seg(0.5, 0.8, 0.54, 0.96, (0x30, 0x30, 0x30), 0.04)
        ell(0.3, 0.3, 0.7, 0.88, white, (0x80, 0x80, 0x80), 0.02)
        ell(0.34, 0.4, 0.5, 0.58, (0x2A, 0x2A, 0x2E))
        ell(0.52, 0.6, 0.66, 0.76, (0x2A, 0x2A, 0x2E))
        ell(0.4, 0.08, 0.6, 0.36, white, (0x80, 0x80, 0x80), 0.02)
        ell(0.43, 0.04, 0.57, 0.14, (0xF0, 0xA8, 0xA8))
        seg(0.4, 0.14, 0.35, 0.1, (0xD8, 0xD0, 0xB0), 0.04)
        seg(0.6, 0.14, 0.65, 0.1, (0xD8, 0xD0, 0xB0), 0.04)
    elif species == 2:  # pig
        pink = (0xF0, 0xA0, 0xA8)
        ell(0.3, 0.32, 0.7, 0.88, pink, shade(pink, 0.7), 0.02)
        ell(0.4, 0.12, 0.6, 0.4, shade(pink, 1.05), shade(pink, 0.7), 0.02)
        ell(0.45, 0.06, 0.55, 0.16, (0xE0, 0x80, 0x88))
        for x in (0.38, 0.62):
            ell(x - 0.05, 0.14, x + 0.05, 0.24, shade(pink, 0.8))
        ell(0.47, 0.88, 0.57, 0.96, None, (0xD0, 0x70, 0x78), 0.025)
    else:  # sheep
        wool = (0xF4, 0xF2, 0xEA)
        for (x, y) in ((0.36, 0.36), (0.58, 0.36), (0.3, 0.52), (0.62, 0.52), (0.4, 0.7), (0.56, 0.72), (0.46, 0.5)):
            ell(x - 0.12, y - 0.12, x + 0.12, y + 0.12, wool, (0xB8, 0xB4, 0xA8), 0.015)
        ell(0.42, 0.1, 0.58, 0.34, (0x3A, 0x32, 0x2E))
    img = c.done()
    if kick:
        c2 = Cv(1, 1, s)
        pts = []
        for i in range(16):
            a = math.pi * 2 * i / 16
            r = 0.46 if i % 2 == 0 else 0.26
            pts.append((0.5 + r * math.cos(a), 0.5 + r * math.sin(a)))
        c2.poly(pts, (0xFF, 0x40, 0x30, 200))
        img = Image.alpha_composite(c2.done(), img)
    return img


def spun(img, eighths):
    return img.rotate(-45 * eighths, resample=Image.BICUBIC) if eighths else img


# ------------------------------------------------------------------------------------------------------ buildings

def _ground(c, w, h):
    c.rect(0, 0, w, h, ground(8))


def silo(w, s):
    """Small silo (w = 1) or large silo (w = 2), on ground: a tin cylinder with a dome."""
    c = Cv(w, w, s)
    _ground(c, w, w)
    c.shadow(0.2 * w, 0.55 * w, 0.98 * w, 0.99 * w, 70)
    body = (0xB4, 0xBC, 0xC6)
    c.rect(0.22 * w, 0.3 * w, 0.78 * w, 0.92 * w, body)
    for x, f in ((0.22, 0.85), (0.3, 1.0), (0.4, 1.12), (0.55, 1.0), (0.68, 0.88)):
        c.rect(x * w, 0.3 * w, (x + 0.1) * w, 0.92 * w, shade(body, f))
    for k in range(1, 5):
        c.line([(0.22 * w, (0.3 + 0.124 * k) * w), (0.78 * w, (0.3 + 0.124 * k) * w)], (0x70, 0x78, 0x84), 0.025 * w)
    c.ellipse(0.22 * w, 0.84 * w, 0.78 * w, 0.99 * w, shade(body, 0.8))
    c.ellipse(0.22 * w, 0.1 * w, 0.78 * w, 0.5 * w, (0xC8, 0x40, 0x34), (0x80, 0x24, 0x1C), 0.02 * w)
    c.ellipse(0.34 * w, 0.16 * w, 0.52 * w, 0.28 * w, (0xE8, 0x80, 0x70, 190))
    c.rect(0.44 * w, 0.66 * w, 0.56 * w, 0.92 * w, (0x30, 0x30, 0x38))
    return c.done()


def shed(w, h, s):
    """A shed on ground: a tin roof over a tan open-fronted wall."""
    c = Cv(w, h, s)
    _ground(c, w, h)
    c.shadow(0.05 * w, 0.7 * h, 1.0 * w, 1.0 * h, 60)
    wall_top = 0.5 * h
    c.rect(0.08 * w, wall_top, 0.92 * w, 0.95 * h, (0xC8, 0xA8, 0x70))
    c.rect(0.14 * w, wall_top + 0.12 * h, 0.86 * w, 0.95 * h, (0x3A, 0x2A, 0x1C))
    for x in (0.14, 0.5, 0.86):
        c.rect(x * w - 0.03, wall_top + 0.1 * h, x * w + 0.03, 0.95 * h, (0x8C, 0x6A, 0x38))
    c.poly([(0.02 * w, wall_top + 0.06 * h), (0.14 * w, 0.1 * h), (0.86 * w, 0.1 * h), (0.98 * w, wall_top + 0.06 * h)], (0xA8, 0x58, 0x3C))
    c.poly([(0.02 * w, wall_top + 0.06 * h), (0.14 * w, 0.1 * h), (0.86 * w, 0.1 * h), (0.98 * w, wall_top + 0.06 * h)], (0xA8, 0x58, 0x3C))
    n = int(w * 6)
    for k in range(1, n):
        x = 0.14 * w + 0.72 * w * k / n
        c.line([(x, 0.12 * h), (x + (x / w - 0.5) * 0.2 * w, wall_top + 0.05 * h)], (0x84, 0x42, 0x2C), 0.018 * w)
    c.rect(0.14 * w, 0.1 * h - 0.02, 0.86 * w, 0.1 * h + 0.02, (0xC8, 0x84, 0x6C))
    return c.done()


def barn(w, h, s):
    """A barn on ground: dark gambrel roof, red walls, white trim, a big X-braced door and a loft window."""
    c = Cv(w, h, s)
    _ground(c, w, h)
    c.shadow(0.05 * w, 0.75 * h, 1.0 * w, 1.0 * h, 60)
    red, trim = (0xB0, 0x32, 0x28), (0xF2, 0xEE, 0xE2)
    roof = (0x5A, 0x5E, 0x6A)
    c.rect(0.06 * w, 0.46 * h, 0.94 * w, 0.96 * h, red)
    for k in range(1, int(w * 5)):
        x = 0.06 * w + 0.88 * w * k / int(w * 5)
        c.line([(x, 0.46 * h), (x, 0.96 * h)], shade(red, 0.88), 0.012 * w)
    c.poly([(0.0, 0.5 * h), (0.14 * w, 0.18 * h), (0.86 * w, 0.18 * h), (1.0 * w, 0.5 * h)], roof)
    c.poly([(0.14 * w, 0.18 * h), (0.86 * w, 0.18 * h), (0.78 * w, 0.06 * h), (0.22 * w, 0.06 * h)], shade(roof, 1.2))
    for k in range(1, 6):
        y = 0.06 * h + 0.44 * h * k / 6
        c.line([(0.04 * w + (0.1 * w) * (1 - k / 6), y), (0.96 * w - (0.1 * w) * (1 - k / 6), y)], shade(roof, 0.8), 0.012 * h)
    c.rect(0.06 * w, 0.46 * h, 0.94 * w, 0.5 * h, trim)
    dw = min(0.5, 0.3 * w)
    d0, d1 = 0.5 * w - dw / 2, 0.5 * w + dw / 2
    c.rect(d0, 0.6 * h, d1, 0.96 * h, trim)
    c.rect(d0 + 0.03, 0.62 * h, d1 - 0.03, 0.96 * h, shade(red, 0.8))
    c.line([(d0 + 0.03, 0.62 * h), (d1 - 0.03, 0.96 * h)], trim, 0.025)
    c.line([(d1 - 0.03, 0.62 * h), (d0 + 0.03, 0.96 * h)], trim, 0.025)
    c.line([(0.5 * w, 0.62 * h), (0.5 * w, 0.96 * h)], trim, 0.025)
    c.rect(0.5 * w - 0.1, 0.5 * h + 0.02, 0.5 * w + 0.1, 0.5 * h + 0.14, (0x30, 0x26, 0x20))
    return c.done()


def pump(s):
    c = Cv(1, 1, s)
    _ground(c, 1, 1)
    c.shadow(0.15, 0.6, 0.95, 0.98, 60)
    c.ellipse(0.1, 0.72, 0.62, 0.98, (0x3A, 0x7F, 0xC9))
    c.rect(0.3, 0.42, 0.7, 0.82, (0x86, 0x8C, 0x94))
    c.rect(0.3, 0.42, 0.7, 0.48, (0xB0, 0xB6, 0xBE))
    c.line([(0.5, 0.42), (0.5, 0.14)], (0x6C, 0x72, 0x7A), 0.07)
    c.line([(0.5, 0.14), (0.78, 0.2), (0.78, 0.34)], (0x6C, 0x72, 0x7A), 0.06)
    c.line([(0.5, 0.2), (0.28, 0.12)], (0xA0, 0x40, 0x30), 0.05)
    c.ellipse(0.74, 0.34, 0.82, 0.46, (0x60, 0xB0, 0xF0))
    return c.done()


def water_tower(s, broken=False):
    c = Cv(1, 1, s)
    _ground(c, 1, 1)
    if broken:
        c.shadow(0.1, 0.5, 0.95, 0.98, 60)
        c.ellipse(0.12, 0.62, 0.9, 0.96, (0x3A, 0x7F, 0xC9))
        c.poly([(0.22, 0.52), (0.5, 0.4), (0.78, 0.56), (0.62, 0.72), (0.3, 0.7)], (0x70, 0x76, 0x80))
        c.line([(0.3, 0.7), (0.15, 0.9)], (0x50, 0x54, 0x5C), 0.05)
        c.line([(0.66, 0.7), (0.86, 0.86)], (0x50, 0x54, 0x5C), 0.05)
        return c.done()
    c.shadow(0.2, 0.7, 0.95, 0.98, 60)
    for x0, x1 in ((0.3, 0.2), (0.7, 0.8)):
        c.line([(x0, 0.5), (x1, 0.94)], (0x5A, 0x5E, 0x66), 0.04)
    c.line([(0.3, 0.7), (0.7, 0.7)], (0x5A, 0x5E, 0x66), 0.025)
    c.line([(0.3, 0.7), (0.76, 0.9)], (0x5A, 0x5E, 0x66), 0.02)
    c.rect(0.18, 0.2, 0.82, 0.56, (0x8C, 0xA0, 0xB8))
    c.rect(0.18, 0.2, 0.34, 0.56, (0xA8, 0xBC, 0xD2))
    for y in (0.32, 0.46):
        c.rect(0.18, y, 0.82, y + 0.03, (0x50, 0x60, 0x78))
    c.poly([(0.14, 0.2), (0.5, 0.04), (0.86, 0.2)], (0x70, 0x76, 0x80))
    return c.done()


def tank_inside(s, level):
    """The inside of a water tower: a tank seen from the side, filled to `level` (0 empty .. 1 full)."""
    c = Cv(1, 1, s)
    c.rect(0, 0, 1, 1, (0x30, 0x34, 0x3C))
    c.rect(0.1, 0.1, 0.9, 0.9, (0x14, 0x18, 0x20))
    top = 0.9 - 0.8 * level
    c.rect(0.1, top, 0.9, 0.9, (0x3A, 0x7F, 0xC9))
    if level > 0:
        c.rect(0.1, top, 0.9, top + 0.05, (0x8C, 0xC4, 0xF0))
    for y in (0.3, 0.5, 0.7):
        c.line([(0.1, y), (0.9, y)], (0xF0, 0xF0, 0xF0, 70), 0.012)
    return c.done()


def floor(s):
    """Inside a building: a plank floor, the slot an item is shown on."""
    c = Cv(1, 1, s)
    c.rect(0, 0, 1, 1, (0xA8, 0x7C, 0x4C))
    for k in range(4):
        y = k * 0.25
        c.rect(0, y, 1, y + 0.25, lerp((0x9A, 0x70, 0x44), (0xB8, 0x8C, 0x58), (k * 0.37) % 1))
        c.line([(0, y), (1, y)], (0x6C, 0x4A, 0x2A), 0.02)
        x = 0.2 + 0.5 * ((k * 3) % 2)
        c.line([(x, y), (x, y + 0.25)], (0x6C, 0x4A, 0x2A), 0.015)
    return c.done()


def windmill(s, k):
    c = Cv(1, 1, s)
    c.shadow(0.25, 0.8, 0.9, 0.99, 60)
    c.poly([(0.36, 0.96), (0.64, 0.96), (0.56, 0.4), (0.44, 0.4)], (0xC4, 0xB4, 0x98))
    c.line([(0.4, 0.8), (0.6, 0.8)], (0x90, 0x80, 0x68), 0.02)
    c.line([(0.42, 0.62), (0.58, 0.62)], (0x90, 0x80, 0x68), 0.02)
    cx, cy = 0.5, 0.36
    for i in range(4):
        a = math.radians(k * 22.5 + 90 * i)
        ex, ey = cx + 0.4 * math.sin(a), cy - 0.4 * math.cos(a)
        nx, ny = -math.cos(a) * 0.07, -math.sin(a) * 0.07
        mx, my = cx + 0.1 * math.sin(a), cy - 0.1 * math.cos(a)
        c.poly([(mx, my), (ex, ey), (ex + nx, ey + ny), (mx + nx, my + ny)], (0xF0, 0xEC, 0xE0))
        c.line([(cx, cy), (ex, ey)], (0x6C, 0x4A, 0x2A), 0.03)
    c.ellipse(cx - 0.05, cy - 0.05, cx + 0.05, cy + 0.05, (0x80, 0x50, 0x2A))
    return c.done()


def hay(s, level):
    """A heap of hay: `level` 0..4, one more bale each."""
    c = Cv(1, 1, s)
    c.shadow(0.1, 0.55, 0.92, 0.95, 60)
    n = level + 1
    spots = [(0.18, 0.6), (0.52, 0.6), (0.35, 0.34), (0.2, 0.3), (0.5, 0.28)][:n]
    for x, y in spots:
        c.ellipse(x, y, x + 0.32, y + 0.3, (0xD8, 0xB8, 0x48), (0xA0, 0x80, 0x20), 0.02)
        c.ellipse(x + 0.07, y + 0.06, x + 0.25, y + 0.24, None, (0xA0, 0x80, 0x20), 0.015)
    return c.done()


def figure(s, colour):
    """A small person, for the pictures that are neither machine nor animal."""
    c = Cv(1, 1, s)
    c.shadow(0.3, 0.7, 0.75, 0.95, 60)
    c.rect(0.4, 0.45, 0.6, 0.8, colour)
    c.ellipse(0.4, 0.2, 0.6, 0.42, (0xF0, 0xC8, 0xA0))
    c.rect(0.4, 0.8, 0.46, 0.92, DARK)
    c.rect(0.54, 0.8, 0.6, 0.92, DARK)
    c.ellipse(0.38, 0.14, 0.62, 0.3, (0x6A, 0x44, 0x22))
    return c.done()


# ---------------------------------------------------------------------------------------------------------- crops

RIPE = {  # the colour of each crop's harvest, in CROP_ROWS order
    "Corn": (0xF0, 0xCC, 0x40), "Wheat": (0xE0, 0xB6, 0x50), "Barley": (0xD4, 0xA8, 0x58), "Potatoes": (0xB8, 0x90, 0x60),
    "Oats": (0xE2, 0xD0, 0x90), "Rice": (0xE8, 0xDC, 0x98), "Sorghum": (0xC0, 0x5C, 0x38), "Soybeans": (0x8C, 0xB0, 0x48),
    "Cotton": (0xF8, 0xF8, 0xF4), "Onions": (0xE8, 0xDC, 0xC0), "Tomatoes": (0xE0, 0x30, 0x28), "Lettuce": (0x8C, 0xD0, 0x58),
    "Carrots": (0xF0, 0x84, 0x20), "Strawberries": (0xE8, 0x28, 0x3C), "Sugar Cane": (0x98, 0xC8, 0x48), "Peanuts": (0xD0, 0xA4, 0x70),
    "Gladiolus": (0xE0, 0x40, 0xA8), "Sweet Corn": (0xF8, 0xE8, 0x70), "Tobacco": (0x78, 0x9C, 0x38), "Sunflowers": (0xF8, 0xCC, 0x20),
    "Grapes": (0x7A, 0x34, 0x9C), "Oranges": (0xF4, 0x90, 0x18), "Almonds": (0xC8, 0xA0, 0x78), "Apples": (0xD8, 0x28, 0x30),
}
SHAPE = {"Corn": "corn", "Sweet Corn": "corn", "Wheat": "grain", "Barley": "grain", "Oats": "grain", "Rice": "grain",
         "Sorghum": "grain", "Soybeans": "bush", "Peanuts": "bush", "Cotton": "bush", "Tomatoes": "bush", "Strawberries": "low",
         "Potatoes": "root", "Onions": "root", "Carrots": "root", "Lettuce": "leaf", "Tobacco": "leaf", "Sunflowers": "flower",
         "Gladiolus": "spike", "Sugar Cane": "cane", "Grapes": "vine", "Oranges": "tree", "Almonds": "tree", "Apples": "tree"}
GREEN = (0x3E, 0x9A, 0x3A)


def plant(c, name, x, y, t, stage):
    """One plant at (x, y) (its foot) in cell units, grown to `t` (0 seed .. 1 ripe) at stage 0..7."""
    ripe, shape = RIPE[name], SHAPE[name]
    leaf = lerp(GREEN, shade(GREEN, 1.0), 0)
    dry = lerp(GREEN, (0xB0, 0xA0, 0x48), max(0.0, (t - 0.8) / 0.2)) if shape in ("grain", "corn", "cane") else GREEN
    if stage == 0:
        c.ellipse(x - 0.02, y - 0.02, x + 0.02, y + 0.02, (0x40, 0x2C, 0x1C))
        return
    h = 0.06 + 0.2 * t
    if shape == "grain":
        for dx in (-0.06, -0.02, 0.02, 0.06):
            c.line([(x + dx * 0.4 * t, y), (x + dx * (1 + t), y - h)], dry, 0.015)
            if t > 0.55:
                c.ellipse(x + dx * (1 + t) - 0.012, y - h - 0.05, x + dx * (1 + t) + 0.012, y - h + 0.01, ripe)
    elif shape == "corn":
        c.line([(x, y), (x, y - h * 1.4)], dry, 0.025)
        for sgn in (-1, 1):
            c.line([(x, y - h * 0.5), (x + sgn * h * 0.8, y - h * 0.9)], dry, 0.02)
            c.line([(x, y - h * 0.9), (x + sgn * h * 0.7, y - h * 1.2)], dry, 0.018)
        if t > 0.6:
            c.ellipse(x + 0.01, y - h * 0.9, x + 0.045, y - h * 0.45, ripe)
    elif shape in ("bush", "low"):
        r = 0.03 + 0.08 * t
        yy = y - r * (0.6 if shape == "low" else 1.0)
        for dx, dy in ((-0.6, 0.2), (0.6, 0.2), (0, -0.3), (0, 0.3)):
            c.ellipse(x + dx * r - r * 0.7, yy + dy * r - r * 0.7, x + dx * r + r * 0.7, yy + dy * r + r * 0.7, leaf)
        if t > 0.6:
            for dx, dy in ((-0.6, 0.1), (0.5, 0.3), (0.05, -0.4), (0.2, 0.6)):
                c.ellipse(x + dx * r - 0.016, yy + dy * r - 0.016, x + dx * r + 0.016, yy + dy * r + 0.016, ripe)
    elif shape == "root":
        for ang in (-50, -20, 10, 40):
            a = math.radians(ang)
            c.line([(x, y), (x + math.sin(a) * h, y - math.cos(a) * h)], leaf, 0.02)
        if t > 0.6:
            c.ellipse(x - 0.03, y - 0.01, x + 0.03, y + 0.04, ripe)
    elif shape == "leaf":
        r = 0.03 + 0.07 * t
        for ang in range(0, 360, 60):
            a = math.radians(ang)
            ex, ey = x + math.cos(a) * r * 0.8, y - r * 0.8 + math.sin(a) * r * 0.8
            c.ellipse(ex - r * 0.6, ey - r * 0.6, ex + r * 0.6, ey + r * 0.6, lerp(GREEN, ripe, max(0, t - 0.4) * 1.2))
    elif shape == "flower":
        c.line([(x, y), (x, y - h * 1.5)], leaf, 0.025)
        c.line([(x, y - h * 0.6), (x + h * 0.5, y - h * 0.8)], leaf, 0.02)
        r = 0.015 + 0.055 * t
        if t > 0.4:
            for ang in range(0, 360, 45):
                a = math.radians(ang)
                c.ellipse(x + math.cos(a) * r - r * 0.5, y - h * 1.5 + math.sin(a) * r - r * 0.5,
                          x + math.cos(a) * r + r * 0.5, y - h * 1.5 + math.sin(a) * r + r * 0.5, ripe)
            c.ellipse(x - r * 0.6, y - h * 1.5 - r * 0.6, x + r * 0.6, y - h * 1.5 + r * 0.6, (0x6A, 0x44, 0x22))
    elif shape == "spike":
        c.line([(x, y), (x, y - h * 1.5)], leaf, 0.02)
        for k in range(1, 5):
            if t > 0.5 or k < 3:
                col = ripe if t > 0.55 else leaf
                c.ellipse(x - 0.025 + (k % 2) * 0.02, y - h * 0.35 * k - 0.02, x + 0.025 + (k % 2) * 0.02, y - h * 0.35 * k + 0.02, col)
    elif shape == "cane":
        for dx in (-0.025, 0.025):
            c.line([(x + dx, y), (x + dx, y - h * 1.5)], dry, 0.03)
            for k in range(1, 4):
                c.line([(x + dx - 0.02, y - h * 0.38 * k), (x + dx + 0.02, y - h * 0.38 * k)], shade(dry, 0.7), 0.01)
        for sgn in (-1, 1):
            c.line([(x, y - h * 1.2), (x + sgn * h * 0.7, y - h * 1.6)], leaf, 0.018)
    else:  # vine and tree: a trunk and a crown, fruit when ripe
        trunk = (0x6B, 0x42, 0x1F)
        c.line([(x, y), (x, y - h * 0.8)], trunk, 0.03)
        r = 0.04 + 0.07 * t
        c.ellipse(x - r, y - h * 0.8 - r * 1.2, x + r, y - h * 0.8 + r * 0.5, leaf)
        if t > 0.6:
            for dx, dy in ((-0.5, -0.4), (0.5, -0.3), (0, -0.9), (0.1, 0.0)):
                c.ellipse(x + dx * r - 0.018, y - h * 0.8 + dy * r - 0.018, x + dx * r + 0.018, y - h * 0.8 + dy * r + 0.018, ripe)


def crop_stage(s, name, stage):
    """Growth tile `stage` (0..7): bare rows, then plants that grow and ripen."""
    c = Cv(1, 1, s)
    c.rect(0, 0, 1, 1, (0x6A, 0x4A, 0x32))
    for k in range(4):
        y = 0.12 + 0.24 * k
        c.line([(0, y), (1, y)], (0x55, 0x3A, 0x26), 0.05)
    t = stage / 7.0
    for row in range(4):
        for cx in (0.27, 0.73):
            plant(c, name, cx, 0.2 + 0.24 * row + 0.08, t, stage)
    return c.done()


def crop_icon(s, name, frame):
    """The 2 icons of a crop (a tan and a green frame): the ripe plant, big, on a pale card."""
    c = Cv(1, 1, s)
    c.rect(0, 0, 1, 1, frame)
    c.rect(0.08, 0.08, 0.92, 0.92, (0xF4, 0xEC, 0xD0))
    c.rect(0.08, 0.76, 0.92, 0.92, (0x8A, 0x62, 0x3C))
    c.z, c.ox, c.oy = 2.4, 0.5 - 0.5 * 2.4, 0.84 - 0.82 * 2.4
    plant(c, name, 0.5, 0.82, 1.0, 7)
    c.z, c.ox, c.oy = 1.0, 0.0, 0.0
    for box in ((0, 0, 1, 0.08), (0, 0.92, 1, 1), (0, 0, 0.08, 1), (0.92, 0, 1, 1)):
        c.rect(*box, frame)
    return c.done()


# ------------------------------------------------------------------------------------- the farmhouse and its yard

LAWN = (0x5C, 0xA4, 0x48)


def lawn(c, w, h, seed=3):
    c.rect(0, 0, w, h, LAWN)
    import random
    rng = random.Random(seed)
    for _ in range(int(14 * w * h)):
        x, y = rng.uniform(0, w), rng.uniform(0, h)
        c.line([(x, y), (x + 0.015, y - 0.05)], shade(LAWN, rng.choice((0.85, 1.15))), 0.012)


def house(tier, s):
    """The farmhouse at richness `tier` (0 cabin, 1 farmhouse, 2 and up a big house), two tiles square, on lawn."""
    c = Cv(2, 2, s)
    lawn(c, 2, 2, 5 + tier)
    c.shadow(0.12, 1.3, 1.95, 1.98, 70)
    if tier == 0:
        wall, roof, trim = (0x9A, 0x6A, 0x3C), (0x6C, 0x70, 0x78), (0x6A, 0x46, 0x26)
        x0, x1, ytop, ywall = 0.3, 1.7, 0.5, 0.95
    elif tier == 1:
        wall, roof, trim = (0xF0, 0xEA, 0xDA), (0xB0, 0x38, 0x2C), (0xFF, 0xFF, 0xFF)
        x0, x1, ytop, ywall = 0.15, 1.85, 0.3, 0.85
    else:
        wall, roof, trim = (0xE8, 0xD8, 0xA8), (0x5A, 0x3A, 0x68), (0xFF, 0xFF, 0xFF)
        x0, x1, ytop, ywall = 0.08, 1.92, 0.15, 0.78
    c.rect(x0, ywall, x1, 1.88, wall)
    c.rect(x0, 1.84, x1, 1.9, shade(wall, 0.7))
    c.poly([(x0 - 0.1, ywall + 0.04), (x0 + 0.2, ytop), (x1 - 0.2, ytop), (x1 + 0.1, ywall + 0.04)], roof)
    c.poly([(x0 + 0.2, ytop), (x1 - 0.2, ytop), (x1 - 0.3, ytop + 0.12), (x0 + 0.3, ytop + 0.12)], shade(roof, 1.25))
    for k in range(1, 5):
        y = ytop + (ywall - ytop) * k / 5
        c.line([(x0 - 0.08 + 0.3 * (1 - k / 5), y), (x1 + 0.08 - 0.3 * (1 - k / 5), y)], shade(roof, 0.8), 0.015)
    if tier >= 1:
        c.rect(x1 - 0.5, ytop - 0.18, x1 - 0.32, ytop + 0.25, (0x8C, 0x4A, 0x38))  # chimney
        c.rect(x1 - 0.52, ytop - 0.2, x1 - 0.3, ytop - 0.14, (0x60, 0x30, 0x24))
    mid = (x0 + x1) / 2
    c.rect(mid - 0.14, 1.2, mid + 0.14, 1.88, (0x6A, 0x3A, 0x1E))  # door
    c.ellipse(mid + 0.07, 1.52, mid + 0.11, 1.56, (0xE0, 0xC0, 0x40))
    for wx in (x0 + 0.25, x1 - 0.25 - 0.3):
        c.rect(wx, 1.15, wx + 0.3, 1.5, trim)
        c.rect(wx + 0.03, 1.18, wx + 0.27, 1.47, GLASS)
        c.line([(wx + 0.15, 1.18), (wx + 0.15, 1.47)], trim, 0.02)
        c.line([(wx + 0.03, 1.325), (wx + 0.27, 1.325)], trim, 0.02)
    if tier >= 2:
        c.rect(mid - 0.45, 1.12, mid + 0.45, 1.18, trim)  # a porch roof on posts
        for px in (mid - 0.42, mid + 0.38):
            c.rect(px, 1.18, px + 0.05, 1.88, trim)
        c.rect(mid - 0.14, 0.45, mid + 0.14, 0.7, trim)
        c.rect(mid - 0.11, 0.48, mid + 0.11, 0.67, GLASS)
    return c.done()


def yard(piece, s):
    """Yard pieces 0 .. 10 (tiles 518 .. 528) on lawn: a mailbox, flower beds, hedges, a tree, a bench, a birdbath, a well..."""
    c = Cv(1, 1, s)
    lawn(c, 1, 1, 40 + piece)
    c.shadow(0.15, 0.6, 0.9, 0.97, 55)
    if piece == 0:  # mailbox
        c.rect(0.46, 0.45, 0.54, 0.92, (0x6A, 0x46, 0x26))
        c.rect(0.28, 0.25, 0.72, 0.5, (0x30, 0x58, 0xB0))
        c.rect(0.66, 0.2, 0.7, 0.34, (0xD0, 0x30, 0x28))
    elif piece in (1, 2, 8):  # flower beds
        c.rect(0.1, 0.3, 0.9, 0.85, (0x6A, 0x4A, 0x32))
        cols = {1: ((0xE0, 0x30, 0x40), (0xF0, 0x80, 0xA0)), 2: ((0xF4, 0xD0, 0x30), (0xFF, 0xF0, 0x90)), 8: ((0xA0, 0x50, 0xD0), (0xE0, 0xA0, 0xFF))}[piece]
        for i in range(8):
            x, y = 0.2 + (i % 4) * 0.2, 0.42 + (i // 4) * 0.27
            c.line([(x, y + 0.1), (x, y)], (0x3E, 0x9A, 0x3A), 0.02)
            c.ellipse(x - 0.06, y - 0.06, x + 0.06, y + 0.06, cols[i % 2])
    elif piece in (3, 4):  # hedges
        w = (0.05, 0.95) if piece == 3 else (0.05, 0.6)
        c.rect(w[0], 0.35, w[1], 0.85, (0x2C, 0x78, 0x34))
        for i in range(6):
            c.ellipse(w[0] + 0.02 + i * 0.14, 0.3 + (i % 2) * 0.05, w[0] + 0.2 + i * 0.14, 0.5 + (i % 2) * 0.05, (0x3C, 0x92, 0x44))
    elif piece == 5:  # small tree
        c.rect(0.46, 0.55, 0.54, 0.9, (0x6B, 0x42, 0x1F))
        c.ellipse(0.2, 0.1, 0.8, 0.65, (0x2F, 0x7D, 0x32))
        c.ellipse(0.3, 0.16, 0.55, 0.38, (0x4C, 0xA8, 0x4F))
    elif piece == 6:  # bench
        c.rect(0.15, 0.45, 0.85, 0.55, (0x9C, 0x6B, 0x3A))
        c.rect(0.15, 0.3, 0.85, 0.4, (0x9C, 0x6B, 0x3A))
        for x in (0.2, 0.78):
            c.rect(x, 0.55, x + 0.05, 0.8, (0x5A, 0x38, 0x1C))
    elif piece == 7:  # birdbath
        c.rect(0.45, 0.55, 0.55, 0.88, (0xA8, 0xAC, 0xB4))
        c.ellipse(0.25, 0.38, 0.75, 0.62, (0xC4, 0xC8, 0xD0), (0x80, 0x84, 0x8C), 0.02)
        c.ellipse(0.32, 0.43, 0.68, 0.57, (0x60, 0xB0, 0xF0))
    elif piece == 9:  # well
        c.ellipse(0.2, 0.45, 0.8, 0.9, (0x8C, 0x8E, 0x94), (0x5A, 0x5C, 0x62), 0.02)
        c.ellipse(0.3, 0.52, 0.7, 0.78, (0x20, 0x40, 0x70))
        c.rect(0.2, 0.12, 0.26, 0.6, (0x6B, 0x42, 0x1F))
        c.rect(0.74, 0.12, 0.8, 0.6, (0x6B, 0x42, 0x1F))
        c.poly([(0.12, 0.16), (0.5, 0.0), (0.88, 0.16)], (0xA0, 0x40, 0x30))
    else:  # a rock garden
        for x, y, r in ((0.3, 0.6, 0.16), (0.58, 0.5, 0.2), (0.7, 0.75, 0.12)):
            c.ellipse(x - r, y - r * 0.8, x + r, y + r * 0.8, (0x84, 0x86, 0x8A), (0x55, 0x57, 0x5B), 0.02)
    return c.done()


# ------------------------------------------------------------------------------- field markers: what is wrong

def marker(kind, s):
    """One of the eight problem markers a field shows beside its sign, two tiles square: on a plaque, a picture of
    what is wrong. kind: pests, wet, toxic, dry, weeds, disease, wilt, hungry."""
    c = Cv(2, 2, s)
    c.rect(0, 0, 2, 2, (0x6A, 0x4A, 0x32))
    for k in range(8):
        c.line([(0, 0.12 + 0.24 * k), (2, 0.12 + 0.24 * k)], (0x55, 0x3A, 0x26), 0.05)
    plaque = {"pests": (0xC0, 0x30, 0x2C), "wet": (0x34, 0x6C, 0xC0), "toxic": (0x7A, 0x44, 0xA8), "dry": (0xD0, 0x84, 0x20),
              "weeds": (0x2C, 0x80, 0x34), "disease": (0xB0, 0xA0, 0x20), "wilt": (0x80, 0x80, 0x84), "hungry": (0x8A, 0x62, 0x3C)}[kind]
    c.rect(0.1, 0.1, 1.9, 1.9, (0x20, 0x20, 0x20))
    c.rect(0.16, 0.16, 1.84, 1.84, plaque)
    c.rect(0.16, 0.16, 1.84, 0.3, shade(plaque, 1.3))
    W = (0xFF, 0xFF, 0xFF)
    if kind == "pests":  # a beetle
        c.ellipse(0.62, 0.55, 1.38, 1.5, (0x20, 0x20, 0x24), W, 0.03)
        c.ellipse(0.78, 0.35, 1.22, 0.7, (0x20, 0x20, 0x24), W, 0.03)
        c.line([(1.0, 0.7), (1.0, 1.45)], W, 0.03)
        for dy in (0.75, 0.98, 1.2):
            c.line([(0.62, dy), (0.35, dy - 0.12)], W, 0.04)
            c.line([(1.38, dy), (1.65, dy - 0.12)], W, 0.04)
        c.line([(0.9, 0.38), (0.78, 0.2)], W, 0.03)
        c.line([(1.1, 0.38), (1.22, 0.2)], W, 0.03)
    elif kind == "wet":  # a big drop and waves
        c.poly([(1.0, 0.35), (0.62, 1.0), (1.38, 1.0)], (0xBC, 0xE0, 0xFF))
        c.ellipse(0.62, 0.75, 1.38, 1.4, (0xBC, 0xE0, 0xFF))
        for y in (1.55, 1.7):
            c.line([(0.4, y), (0.7, y - 0.07), (1.0, y), (1.3, y - 0.07), (1.6, y)], W, 0.04)
    elif kind == "toxic":  # a skull
        c.ellipse(0.55, 0.38, 1.45, 1.2, W)
        c.rect(0.72, 1.0, 1.28, 1.52, W)
        for x in (0.7, 1.05):
            c.ellipse(x, 0.68, x + 0.25, 0.98, (0x20, 0x10, 0x30))
        c.poly([(1.0, 1.0), (0.93, 1.12), (1.07, 1.12)], (0x20, 0x10, 0x30))
        for x in (0.85, 1.0, 1.15):
            c.line([(x, 1.32), (x, 1.5)], (0x20, 0x10, 0x30), 0.03)
    elif kind == "dry":  # a sun over cracked ground
        c.ellipse(0.65, 0.3, 1.35, 1.0, (0xFF, 0xE0, 0x40), (0xFF, 0xFF, 0xA0), 0.04)
        for a in range(0, 360, 45):
            r = math.radians(a)
            c.line([(1.0 + 0.45 * math.cos(r), 0.65 + 0.45 * math.sin(r)), (1.0 + 0.62 * math.cos(r), 0.65 + 0.62 * math.sin(r))], (0xFF, 0xE0, 0x40), 0.05)
        c.line([(0.3, 1.5), (0.7, 1.4), (1.0, 1.6), (1.4, 1.45), (1.7, 1.55)], (0x40, 0x20, 0x10), 0.05)
        c.line([(1.0, 1.6), (0.9, 1.8)], (0x40, 0x20, 0x10), 0.04)
    elif kind == "weeds":  # a jagged weed
        c.line([(1.0, 1.7), (1.0, 0.9)], (0x10, 0x40, 0x14), 0.07)
        for ang, ln in ((-60, 0.7), (-25, 0.85), (25, 0.85), (60, 0.7), (0, 0.9)):
            r = math.radians(ang)
            c.poly([(1.0, 1.2), (1.0 + ln * math.sin(r) - 0.08, 1.2 - ln * math.cos(r)), (1.0 + ln * math.sin(r) * 1.05, 1.2 - ln * math.cos(r) * 1.05 - 0.04), (1.0 + ln * math.sin(r) + 0.08, 1.2 - ln * math.cos(r))], (0xB8, 0xF0, 0x70))
    elif kind == "disease":  # a spotted leaf
        c.poly([(1.0, 0.3), (1.55, 0.95), (1.0, 1.7), (0.45, 0.95)], (0x8C, 0xB8, 0x48))
        c.line([(1.0, 0.4), (1.0, 1.7)], (0x4C, 0x70, 0x24), 0.04)
        for x, y, r in ((0.85, 0.8, 0.12), (1.2, 1.0, 0.15), (0.9, 1.25, 0.1), (1.12, 0.65, 0.08)):
            c.ellipse(x - r, y - r, x + r, y + r, (0x70, 0x30, 0x20))
    elif kind == "wilt":  # an exclamation mark
        c.poly([(0.88, 0.35), (1.12, 0.35), (1.06, 1.15), (0.94, 1.15)], W)
        c.ellipse(0.88, 1.3, 1.12, 1.54, W)
    else:  # hungry: a bag with a low gauge and a down arrow
        c.rect(0.55, 0.35, 1.45, 1.05, (0xE8, 0xD8, 0xB0))
        c.rect(0.55, 0.85, 1.45, 1.05, (0x70, 0x50, 0x30))
        c.line([(0.82, 0.75), (0.82, 0.45), (1.18, 0.75), (1.18, 0.45)], (0x30, 0x20, 0x10), 0.06)
        c.poly([(0.75, 1.2), (1.25, 1.2), (1.0, 1.65)], W)
    return c.done()


# --------------------------------------------------------------------------------------------- weather pictures

PAPER = (0xEC, 0xF2, 0xFA)


def sun(c, x, y, r, rays=True):
    if rays:
        for a in range(0, 360, 45):
            t = math.radians(a)
            c.line([(x + (r + 0.05) * math.cos(t), y + (r + 0.05) * math.sin(t)), (x + (r + 0.2) * math.cos(t), y + (r + 0.2) * math.sin(t))], (0xF0, 0xB0, 0x20), 0.07)
    c.ellipse(x - r, y - r, x + r, y + r, (0xFF, 0xD8, 0x30), (0xE8, 0xA0, 0x10), 0.03)


def cloud(c, x, y, w, col=(0xF8, 0xF8, 0xFC), edge=(0x90, 0x9C, 0xB0)):
    for dx, dy, r in ((0.25, 0.25, 0.22), (0.5, 0.15, 0.27), (0.75, 0.27, 0.2)):
        c.ellipse(x + dx * w - r * w, y + dy * w - r * w, x + dx * w + r * w, y + dy * w + r * w, edge)
    for dx, dy, r in ((0.25, 0.25, 0.2), (0.5, 0.15, 0.25), (0.75, 0.27, 0.18)):
        c.ellipse(x + dx * w - r * w, y + dy * w - r * w, x + dx * w + r * w, y + dy * w + r * w, col)
    c.rect(x + 0.1 * w, y + 0.28 * w, x + 0.9 * w, y + 0.44 * w, col)


def weather_icon(kind, s):
    """Tiles 200..208: the seasons (winter, spring, summer, autumn) and the day's sky (clear, fair, cloudy, rain, snow)."""
    c = Cv(1, 1, s)
    c.rect(0, 0, 1, 1, PAPER)
    if kind == "winter":
        c.ellipse(0.28, 0.5, 0.72, 0.95, (0xFF, 0xFF, 0xFF), (0x90, 0xA0, 0xC0), 0.03)
        c.ellipse(0.34, 0.2, 0.66, 0.52, (0xFF, 0xFF, 0xFF), (0x90, 0xA0, 0xC0), 0.03)
        c.ellipse(0.42, 0.3, 0.46, 0.34, (0x20, 0x20, 0x20)); c.ellipse(0.54, 0.3, 0.58, 0.34, (0x20, 0x20, 0x20))
        c.poly([(0.5, 0.36), (0.66, 0.4), (0.5, 0.42)], (0xF0, 0x80, 0x20))
        c.rect(0.34, 0.14, 0.66, 0.22, (0x30, 0x40, 0x90))
    elif kind == "spring":
        c.line([(0.5, 0.95), (0.5, 0.45)], (0x3E, 0x9A, 0x3A), 0.07)
        c.ellipse(0.2, 0.4, 0.5, 0.62, (0x5C, 0xB8, 0x4A)); c.ellipse(0.5, 0.5, 0.8, 0.72, (0x5C, 0xB8, 0x4A))
        for a in range(0, 360, 72):
            t = math.radians(a)
            c.ellipse(0.5 + 0.16 * math.cos(t) - 0.1, 0.3 + 0.16 * math.sin(t) - 0.1, 0.5 + 0.16 * math.cos(t) + 0.1, 0.3 + 0.16 * math.sin(t) + 0.1, (0xF0, 0x70, 0xB0))
        c.ellipse(0.42, 0.22, 0.58, 0.38, (0xFF, 0xE0, 0x40))
    elif kind == "summer":
        sun(c, 0.5, 0.5, 0.2)
    elif kind == "autumn":
        c.poly([(0.5, 0.1), (0.62, 0.3), (0.85, 0.3), (0.72, 0.5), (0.85, 0.62), (0.58, 0.64), (0.5, 0.85), (0.42, 0.64), (0.15, 0.62), (0.28, 0.5), (0.15, 0.3), (0.38, 0.3)], (0xD8, 0x60, 0x20))
        c.line([(0.5, 0.35), (0.5, 0.95)], (0x70, 0x40, 0x20), 0.06)
    elif kind == "clear":
        sun(c, 0.5, 0.5, 0.22)
    elif kind == "fair":
        sun(c, 0.4, 0.4, 0.2)
        cloud(c, 0.3, 0.45, 0.65)
    elif kind == "cloudy":
        cloud(c, 0.08, 0.15, 0.6, (0xC8, 0xCE, 0xD8))
        cloud(c, 0.3, 0.35, 0.65, (0xF0, 0xF2, 0xF8))
    elif kind == "rain":
        cloud(c, 0.15, 0.08, 0.7, (0xB4, 0xBC, 0xC8))
        for x in (0.3, 0.48, 0.66):
            c.line([(x, 0.62), (x - 0.06, 0.82)], (0x34, 0x6C, 0xC0), 0.06)
    else:  # snow
        cloud(c, 0.15, 0.08, 0.7, (0xC8, 0xCE, 0xD8))
        for x, y in ((0.3, 0.7), (0.5, 0.82), (0.68, 0.68), (0.4, 0.58)):
            c.line([(x - 0.06, y), (x + 0.06, y)], (0x60, 0x80, 0xD0), 0.04)
            c.line([(x, y - 0.06), (x, y + 0.06)], (0x60, 0x80, 0xD0), 0.04)
    return c.done()


# ---------------------------------------------------------------------------------------------------- quality badges

def star(c, x, y, r, fill, edge):
    pts = []
    for i in range(10):
        a = -math.pi / 2 + i * math.pi / 5
        rr = r if i % 2 == 0 else r * 0.42
        pts.append((x + rr * math.cos(a), y + rr * math.sin(a)))
    c.poly(pts, edge)
    inner = [(x + (px - x) * 0.8, y + (py - y) * 0.8) for px, py in pts]
    c.poly(inner, fill)


def quality_badge(level, s):
    """A crop's (or animal's, or machine's) condition, two tiles square: 3 high, 2 fair, 1 poor, 0 bad."""
    c = Cv(2, 2, s)
    col = {3: (0x3C, 0xA4, 0x48), 2: (0xE0, 0xC0, 0x30), 1: (0xE0, 0x86, 0x24), 0: (0xC8, 0x30, 0x2C)}[level]
    c.rect(0, 0, 2, 2, (0xC4, 0xC4, 0xC4))
    c.rect(0.08, 0.08, 1.92, 1.92, (0x20, 0x20, 0x20))
    c.rect(0.14, 0.14, 1.86, 1.86, col)
    c.rect(0.14, 0.14, 1.86, 0.28, shade(col, 1.3))
    W, edge = (0xFF, 0xFF, 0xFF), shade(col, 0.5)
    if level == 0:
        c.line([(0.5, 0.5), (1.5, 1.5)], W, 0.2)
        c.line([(1.5, 0.5), (0.5, 1.5)], W, 0.2)
    else:
        pos = {3: [(0.5, 0.95), (1.0, 0.95), (1.5, 0.95)], 2: [(0.7, 1.0), (1.3, 1.0)], 1: [(1.0, 1.0)]}[level]
        r = 0.27 if level == 3 else 0.34 if level == 2 else 0.42
        if level == 3:
            pos = [(0.55, 1.15), (1.0, 0.85), (1.45, 1.15)]
        for x, y in pos:
            star(c, x, y, r, W, edge)
    return c.done()


# ------------------------------------------------------------------------------------------------------ the trough

def trough(s, water):
    """A water trough on ground (tile 176 holds water, 175 is empty): a wooden box, filled with water or dry and bare."""
    c = Cv(1, 1, s)
    c.rect(0, 0, 1, 1, ground(8))
    c.shadow(0.06, 0.55, 0.98, 0.95, 60)
    c.rect(0.08, 0.25, 0.92, 0.78, (0x6A, 0x44, 0x22))
    c.rect(0.08, 0.25, 0.92, 0.32, (0xA8, 0x78, 0x42))
    c.rect(0.14, 0.34, 0.86, 0.68, (0x2A, 0x1C, 0x12) if not water else (0x1E, 0x5A, 0xA8))
    if water:
        c.rect(0.14, 0.34, 0.86, 0.6, (0x4C, 0xA0, 0xF0))
        for y in (0.42, 0.52):
            c.line([(0.2, y), (0.35, y - 0.04), (0.5, y), (0.65, y - 0.04), (0.8, y)], (0xFF, 0xFF, 0xFF), 0.03)
        c.ellipse(0.2, 0.37, 0.3, 0.43, (0xFF, 0xFF, 0xFF, 160))
    else:
        c.rect(0.14, 0.34, 0.86, 0.68, (0x8A, 0x62, 0x3C))
        c.rect(0.14, 0.34, 0.86, 0.4, (0x4A, 0x30, 0x1A))
        for x, y in ((0.3, 0.55), (0.55, 0.5), (0.7, 0.6)):
            c.line([(x, y), (x + 0.1, y + 0.03)], (0xC8, 0xA8, 0x60), 0.025)
        c.line([(0.25, 0.62), (0.4, 0.5), (0.5, 0.64)], (0x4A, 0x30, 0x1A), 0.02)
    c.rect(0.08, 0.7, 0.92, 0.78, (0x50, 0x34, 0x1A))
    for x in (0.12, 0.84):
        c.rect(x, 0.76, x + 0.06, 0.9, (0x40, 0x28, 0x14))
    return c.done()


# --------------------------------------------------------------------------------------------- interface chart tiles

def slider_track(s):
    c = Cv(1, 1, s)
    c.rect(0, 0, 1, 1, (0xC4, 0xC4, 0xC4))
    c.rect(0, 0.4, 1, 0.6, (0x48, 0x48, 0x48))
    c.rect(0, 0.46, 1, 0.58, (0x86, 0x86, 0x86))
    return c.done()


def slider_thumb(s):
    c = Cv(1, 1, s)
    c.rect(0, 0, 1, 1, (0xC4, 0xC4, 0xC4))
    c.rect(0.2, 0.1, 0.8, 0.9, (0x20, 0x20, 0x20))
    c.rect(0.26, 0.16, 0.74, 0.84, (0xC4, 0xC4, 0xC4))
    c.rect(0.26, 0.16, 0.74, 0.24, (0xFF, 0xFF, 0xFF))
    c.rect(0.26, 0.76, 0.74, 0.84, (0x86, 0x86, 0x86))
    c.line([(0.5, 0.3), (0.5, 0.7)], (0x48, 0x48, 0x48), 0.05)
    return c.done()


def chart_today(s):
    """Under today's marker in a field's chart (0xC6): a small calendar leaf."""
    c = Cv(1, 1, s)
    c.rect(0, 0, 1, 1, (0xC4, 0xC4, 0xC4))
    c.rect(0.18, 0.15, 0.82, 0.88, (0x20, 0x20, 0x20))
    c.rect(0.22, 0.2, 0.78, 0.84, (0xFF, 0xFF, 0xF4))
    c.rect(0.22, 0.2, 0.78, 0.38, (0xC8, 0x30, 0x2C))
    for y in (0.5, 0.62, 0.74):
        c.line([(0.3, y), (0.7, y)], (0x70, 0x70, 0x70), 0.04)
    return c.done()


def chart_fallow(s):
    """A fallow stretch in a field's chart (0xD1): bare furrows."""
    c = Cv(1, 1, s)
    c.rect(0, 0, 1, 1, (0xB8, 0x98, 0x68))
    for k in range(4):
        y = 0.14 + 0.24 * k
        c.line([(0, y), (1, y)], (0x7C, 0x5A, 0x38), 0.07)
        c.line([(0, y + 0.07), (1, y + 0.07)], (0xD8, 0xBC, 0x88), 0.03)
    return c.done()


# ------------------------------------------------------------------------------------------------------ the airport

def plane_parked(s):
    """The parked crop duster, a 2 x 2 picture on the apron (tiles 71, 72 over 111, 112)."""
    c = Cv(2, 2, s)
    c.rect(0, 0, 2, 2, (0x70, 0x72, 0x78))
    for x in (0.3, 1.0, 1.7):
        c.line([(x, 1.9), (x, 1.75)], (0xE8, 0xD8, 0x50), 0.04)
    c.shadow(0.2, 0.5, 1.95, 1.95, 70)
    c.rect(0.1, 0.65, 1.9, 1.08, (0xE8, 0xC8, 0x30))
    c.line([(0.1, 0.86), (1.9, 0.86)], (0xB8, 0x98, 0x10), 0.03)
    c.ellipse(0.82, 0.2, 1.18, 1.85, (0xC8, 0x32, 0x28), (0x80, 0x1C, 0x14), 0.03)
    c.ellipse(0.9, 0.7, 1.1, 1.0, GLASS)
    c.rect(0.65, 1.55, 1.35, 1.7, (0xE8, 0xC8, 0x30))
    c.rect(0.95, 0.12, 1.05, 0.3, (0x30, 0x30, 0x34))
    c.line([(0.6, 0.2), (1.4, 0.2)], (0x30, 0x30, 0x34), 0.05)
    return c.done()


def plane_wreck(s):
    """A crashed crop duster (tiles 73, 74 over 113, 114): broken wings and a scorch mark."""
    c = Cv(2, 2, s)
    c.rect(0, 0, 2, 2, (0x70, 0x72, 0x78))
    c.ellipse(0.2, 0.3, 1.8, 1.8, (0x22, 0x1E, 0x1C, 200))
    c.poly([(0.15, 0.7), (0.7, 0.9), (0.6, 1.1), (0.1, 0.95)], (0xB8, 0x98, 0x20))
    c.poly([(1.9, 0.6), (1.35, 0.95), (1.45, 1.15), (1.95, 0.85)], (0xB8, 0x98, 0x20))
    c.poly([(0.75, 0.55), (1.2, 0.5), (1.3, 1.5), (0.85, 1.6)], (0x80, 0x2A, 0x22))
    c.poly([(0.9, 1.55), (1.5, 1.7), (1.4, 1.85), (0.8, 1.75)], (0x40, 0x40, 0x44))
    for x, y in ((0.5, 0.45), (1.6, 0.5), (1.1, 0.3)):
        c.ellipse(x - 0.12, y - 0.12, x + 0.12, y + 0.12, (0x50, 0x50, 0x54, 180))
    c.line([(0.4, 1.3), (0.6, 1.5)], (0xE0, 0x60, 0x20), 0.05)
    return c.done()


# ----------------------------------------------------------------------------------------------------- the town

TOWN_HOUSES = [  # (wall, roof, storeys, extras): from a hut up to a mansion, seven richness levels
    ((0xA8, 0x7C, 0x48), (0x8C, 0x6C, 0x30), 1, ""), ((0xE0, 0xD0, 0xB0), (0x9C, 0x4A, 0x34), 1, ""),
    ((0xC8, 0xDC, 0xE8), (0x58, 0x5E, 0x6C), 1, "chimney"), ((0xE8, 0xC8, 0x90), (0xB0, 0x38, 0x2C), 2, "chimney"),
    ((0xB8, 0x58, 0x3C), (0x44, 0x48, 0x54), 2, "chimney"), ((0xF0, 0xEC, 0xE0), (0x38, 0x68, 0x58), 2, "porch"),
    ((0xE8, 0xE0, 0xC8), (0x58, 0x38, 0x68), 2, "porch"),
]


def town_house(style, s):
    """A house of the town, two tiles square, on lawn: `style` 0 (a hut) .. 6 (a mansion)."""
    wall, roof, storeys, extra = TOWN_HOUSES[style]
    c = Cv(2, 2, s)
    lawn(c, 2, 2, 20 + style)
    c.shadow(0.1, 1.35, 1.95, 1.98, 70)
    wtop = 0.7 if storeys == 2 else 0.95
    c.rect(0.15, wtop, 1.85, 1.85, wall)
    c.rect(0.15, 1.8, 1.85, 1.88, shade(wall, 0.7))
    c.poly([(0.05, wtop + 0.05), (0.3, wtop - 0.35), (1.7, wtop - 0.35), (1.95, wtop + 0.05)], roof)
    for k in range(1, 4):
        y = wtop - 0.35 + 0.4 * k / 4
        c.line([(0.1 + 0.2 * (1 - k / 4), y), (1.9 - 0.2 * (1 - k / 4), y)], shade(roof, 0.8), 0.015)
    if "chimney" in extra:
        c.rect(1.3, wtop - 0.55, 1.5, wtop - 0.15, (0x8C, 0x4A, 0x38))
    trim = (0xFF, 0xFF, 0xFF)
    c.rect(0.82, 1.3, 1.18, 1.85, (0x6A, 0x3A, 0x1E))
    rows = (wtop + 0.12, 1.2) if storeys == 2 else (1.15,)
    for yy in rows:
        for wx in (0.3, 1.4) if yy > 1.0 else (0.3, 0.82, 1.4):
            if yy > 1.0 and 0.7 < wx + 0.15 < 1.3:
                continue
            c.rect(wx, yy, wx + 0.3, yy + 0.3, trim)
            c.rect(wx + 0.03, yy + 0.03, wx + 0.27, yy + 0.27, GLASS)
    if "porch" in extra:
        c.rect(0.55, 1.2, 1.45, 1.26, trim)
        for px in (0.58, 1.38):
            c.rect(px, 1.26, px + 0.05, 1.85, trim)
    return c.done()


def civic(kind, s, n=3):
    """A building of the town's centre, `n` x `n` tiles (3, or 2 for the smaller ones), on paving. Drawn in a 2 x 2 design
    and zoomed to fit."""
    c = Cv(n, n, s)
    c.z = n / 2.0
    c.rect(0, 0, 2, 2, (0xC4, 0xC0, 0xB4))
    for k in range(1, 4):
        c.line([(0, 0.5 * k), (2, 0.5 * k)], (0xB0, 0xAC, 0xA0), 0.015)
        c.line([(0.5 * k, 0), (0.5 * k, 2)], (0xB0, 0xAC, 0xA0), 0.015)
    c.shadow(0.1, 1.35, 1.98, 1.98, 70)
    col = {"hall": ((0xE8, 0xE4, 0xD8), (0x6C, 0x74, 0x84)), "church": ((0xF4, 0xF0, 0xE8), (0x6A, 0x4A, 0x38)),
           "market": ((0xD8, 0x9C, 0x58), (0xA8, 0x40, 0x30)), "bank": ((0xC8, 0xCC, 0xD4), (0x48, 0x58, 0x70)),
           "school": ((0xC8, 0x58, 0x40), (0x58, 0x5E, 0x6C)), "diner": ((0x68, 0xB8, 0xC0), (0xD0, 0x40, 0x48)),
           "clinic": ((0xF4, 0xF4, 0xF8), (0x40, 0x88, 0xC8)), "mill": ((0xB8, 0x98, 0x68), (0x70, 0x50, 0x30)),
           "depot": ((0xC8, 0xB4, 0x80), (0x3C, 0x7C, 0x48)), "dealer": ((0xF0, 0xF0, 0xEC), (0xC8, 0x32, 0x28))}[kind]
    wall, roof = col
    trim = (0xFF, 0xFF, 0xFF)
    c.rect(0.12, 0.85, 1.88, 1.86, wall)
    c.rect(0.12, 1.8, 1.88, 1.88, shade(wall, 0.7))
    c.poly([(0.05, 0.9), (0.25, 0.5), (1.75, 0.5), (1.95, 0.9)], roof)
    if kind == "hall":
        c.rect(0.82, 0.05, 1.18, 0.55, wall); c.poly([(0.78, 0.08), (1.0, -0.05), (1.22, 0.08)], roof)
        c.ellipse(0.9, 0.18, 1.1, 0.38, trim, (0x30, 0x30, 0x30), 0.02)
        for x in (0.3, 0.55, 1.3, 1.55):
            c.rect(x, 1.0, x + 0.1, 1.85, trim)
    elif kind == "church":
        c.rect(0.8, 0.15, 1.2, 0.9, wall); c.poly([(0.74, 0.2), (1.0, -0.15), (1.26, 0.2)], roof)
        c.line([(1.0, -0.15), (1.0, -0.3)], (0xE0, 0xC0, 0x40), 0.04)
        c.ellipse(0.88, 0.5, 1.12, 0.72, (0x30, 0x30, 0x40))
    elif kind in ("market", "diner"):
        for k in range(8):
            c.rect(0.12 + 0.22 * k, 0.9, 0.12 + 0.22 * (k + 1), 1.15, roof if k % 2 == 0 else trim)
        c.rect(0.3, 1.25, 1.0, 1.6, GLASS)
        if kind == "market":
            for x, colr in ((0.25, (0xE0, 0x30, 0x30)), (0.45, (0xF0, 0xC0, 0x30)), (0.65, (0x50, 0xB0, 0x40))):
                c.ellipse(x, 1.62, x + 0.14, 1.78, colr)
    elif kind == "bank":
        for x in (0.3, 0.7, 1.1, 1.5):
            c.rect(x, 1.0, x + 0.12, 1.85, trim)
        c.ellipse(0.88, 0.55, 1.12, 0.78, (0xE0, 0xC0, 0x40), (0x80, 0x60, 0x10), 0.02)
    elif kind == "school":
        c.rect(0.85, 0.3, 1.15, 0.6, wall); c.poly([(0.8, 0.35), (1.0, 0.1), (1.2, 0.35)], roof)
        c.ellipse(0.93, 0.38, 1.07, 0.52, (0xE0, 0xC0, 0x40))
    elif kind == "clinic":
        c.rect(0.85, 0.95, 1.15, 1.2, (0xD0, 0x30, 0x2C)); c.rect(0.93, 0.87, 1.07, 1.28, (0xD0, 0x30, 0x2C))
    elif kind == "depot":  # a grain depot: a roll-up door, sacks and a sign
        c.rect(0.35, 1.1, 1.65, 1.85, shade(wall, 0.6))
        for k in range(1, 6):
            c.line([(0.35, 1.1 + 0.125 * k), (1.65, 1.1 + 0.125 * k)], shade(wall, 0.45), 0.02)
        c.rect(0.6, 0.62, 1.4, 0.8, trim)
        for x in (0.45, 0.75, 1.05, 1.35):
            c.ellipse(x, 1.7, x + 0.22, 1.9, (0xD8, 0xC0, 0x88), (0x80, 0x68, 0x30), 0.02)
    elif kind == "dealer":  # a machinery dealer: a big window with a tractor in it
        c.rect(0.25, 1.05, 1.75, 1.8, GLASS)
        c.rect(0.7, 1.3, 1.1, 1.75, (0xC8, 0x32, 0x28)); c.rect(0.62, 1.5, 0.72, 1.75, DARK); c.rect(1.08, 1.5, 1.18, 1.75, DARK)
        c.rect(0.6, 0.62, 1.4, 0.8, (0xC8, 0x32, 0x28))
    else:  # mill
        c.rect(0.9, 0.2, 1.1, 0.5, (0x5A, 0x38, 0x1C))
        for a in range(0, 360, 90):
            t = math.radians(a + 20)
            c.line([(1.0, 0.35), (1.0 + 0.4 * math.sin(t), 0.35 - 0.4 * math.cos(t))], (0xF0, 0xEC, 0xE0), 0.07)
    if kind not in ("market", "diner", "dealer", "depot"):
        door = (0.88, 1.35, 1.12, 1.86)
        c.rect(*door, (0x5A, 0x34, 0x1C))
    elif kind in ("market", "diner"):
        c.rect(1.2, 1.3, 1.5, 1.86, (0x5A, 0x34, 0x1C))
    if kind not in ("hall", "bank", "market", "diner", "dealer", "depot"):
        for wx in (0.3, 1.5):
            c.rect(wx, 1.2, wx + 0.28, 1.5, trim); c.rect(wx + 0.03, 1.23, wx + 0.25, 1.47, GLASS)
    return c.done()
