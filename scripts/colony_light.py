#!/usr/bin/env python3
"""The light around the colony for its PBR models (content/art.json's
"colony_light" environment): an HDR panorama of a dim navy sky over the
moon's sunlit regolith. The sun itself is not in it: the frame's sun lights
the models directly. Values are linear radiance, as the lit meshes' ambient
(about 0.07 to 0.11): a white wall facing the ground shows the regolith's
bounced light, a roof the sky's. Writes content/light/colony.hdr (Radiance
RGBE with run-length scanlines, as pith's packer reads it). Run after
changing it:
    python scripts/colony_light.py
"""
import math
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'content', 'light', 'colony.hdr')
WIDTH, HEIGHT = 256, 128  # rows from +Y down, as pith's SampleEquirect

ZENITH = (0.012, 0.015, 0.026)  # deep navy
HORIZON = (0.05, 0.056, 0.075)  # paler near the ground's glow
# The regolith (0.2, 0.205, 0.22) under the sun (1, 0.95, 0.9) 25 degrees
# up: what it sends back up.
GROUND = (0.086, 0.083, 0.08)
BLEND = math.radians(3.0)  # the horizon's soft edge


def mix(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def radiance(elevation):
    if elevation >= BLEND:
        return mix(HORIZON, ZENITH, math.sqrt(math.sin(elevation)))
    if elevation <= -BLEND:
        return GROUND
    sky = mix(HORIZON, ZENITH, math.sqrt(math.sin(BLEND)))
    return mix(GROUND, sky, (elevation + BLEND) / (2.0 * BLEND))


def rgbe(color):
    largest = max(color)
    if largest < 1e-32:
        return (0, 0, 0, 0)
    mantissa, exponent = math.frexp(largest)
    scale = mantissa * 256.0 / largest
    return tuple(int(c * scale) for c in color) + (exponent + 128,)


def scanline(pixels):
    """A new-style run-length scanline: each channel in runs of one value (up
    to 127) or of literals (up to 128)."""
    out = bytearray(struct.pack('>BBH', 2, 2, len(pixels)))
    for channel in range(4):
        values = [p[channel] for p in pixels]
        i = 0
        while i < len(values):
            same = 1
            while i + same < len(values) and same < 127 and values[i + same] == values[i]:
                same += 1
            if same >= 3:
                out += bytes((128 + same, values[i]))
                i += same
                continue
            literal = values[i:i + min(128, len(values) - i)]
            out.append(len(literal))
            out.extend(literal)
            i += len(literal)
    return bytes(out)


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'wb') as out:
        out.write(b'#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n')
        out.write(b'-Y %d +X %d\n' % (HEIGHT, WIDTH))
        for row in range(HEIGHT):
            elevation = math.pi * (0.5 - (row + 0.5) / HEIGHT)
            out.write(scanline([rgbe(radiance(elevation))] * WIDTH))
    print('colony_light.py: %s, %dx%d' % (os.path.relpath(OUT, ROOT), WIDTH, HEIGHT))


if __name__ == '__main__':
    main()
