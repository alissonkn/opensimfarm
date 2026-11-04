#!/usr/bin/env python3
"""pixdiff.py A.png B.png X0 Y0 X1 Y1 : count the pixels that differ between two screenshots inside a rectangle
(640x480 space, e.g. work/png/o_X.png from the original and an engine shot) and print their bounding box."""
import sys
from PIL import Image
a = Image.open(sys.argv[1]).convert('RGB'); b = Image.open(sys.argv[2]).convert('RGB')
x0, y0, x1, y1 = map(int, sys.argv[3:7]); diff = []
for y in range(y0, y1):
    for x in range(x0, x1):
        if a.getpixel((x, y)) != b.getpixel((x, y)): diff.append((x, y))
print(len(diff), 'differing pixels', ('bbox %d,%d-%d,%d' % (min(p[0] for p in diff), min(p[1] for p in diff),
      max(p[0] for p in diff), max(p[1] for p in diff))) if diff else '')
