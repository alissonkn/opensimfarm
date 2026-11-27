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
