#!/usr/bin/env python3
"""Stella Nova's icons, from one set of SVGs (content/icons/, listed in
content/icons/icons.json) into what the game and the website read:

- content/icons/icons.ttf: a font with each icon at U+E000 + its place in
  icons.json. The game packs it like its other fonts (MTSDF glyph images)
  and puts it after them in its fonts' fallbacks, so an icon's character in
  any text draws the icon, sized and tinted like the letters around it.
- client/src/icon_codes.h: each icon's character as UTF-8, by name, and a
  lookup by id (icons share the catalog's ids: "plasma_s", "mine", "metal").
- content/initial.json: the "icons" entry, its atlas listing the characters,
  so its glyph images are made at pack time.
- site/stellanova.html: its SVG sprite, between the "icons:begin" and
  "icons:end" comments, a <symbol id="i-<id>"> per icon.

The SVGs follow content/icons/README.md: a 24-unit grid, filled paths only.
Needs fontTools (pip install fonttools). Run after changing an icon:
    python scripts/icons.py
"""
import json
import os
import re
import subprocess
import sys

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.recordingPen import RecordingPen
from fontTools.pens.reverseContourPen import ReverseContourPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.svgLib.path import parse_path

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICONS = os.path.join(ROOT, 'content', 'icons')
FIRST = 0xE000  # the Private Use Area
EM = 1000
GRID = 24
SCALE = 0.9 * EM / GRID  # an icon is 0.9 em tall...
BOTTOM = -0.1 * EM       # ...from a tenth of an em below the baseline: centered on capitals
ASCENT = 900
DESCENT = 250
PATH = re.compile(r'<path\b[^>]*?\sd="([^"]+)"')


def contours(recording):
    """The recording's contours, each a list of (operator, points)."""
    out, current = [], []
    for op, points in recording.value:
        current.append((op, points))
        if op in ('closePath', 'endPath'):
            out.append(current)
            current = []
    if current:
        out.append(current)
    return out


def flatten(contour):
    """A contour's outline as points: curves cut into short lines."""
    points, at = [], None
    for op, args in contour:
        if op in ('moveTo', 'lineTo'):
            at = args[0]
            points.append(at)
        elif op in ('curveTo', 'qCurveTo'):
            controls = [at] + list(args)
            for i in range(1, 9):
                t = i / 8.0
                layer = controls
                while len(layer) > 1:
                    layer = [(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
                             for a, b in zip(layer, layer[1:])]
                points.append(layer[0])
            at = args[-1]
    return points


def area(points):
    return 0.5 * sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(points, points[1:] + points[:1]))


def inside(point, polygon):
    x, y = point
    hit = False
    for (x1, y1), (x2, y2) in zip(polygon, polygon[1:] + polygon[:1]):
        if (y1 > y) != (y2 > y) and x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
            hit = not hit
    return hit


def replay(contour, pen):
    for op, args in contour:
        getattr(pen, op)(*args)


def glyph_of(svg):
    """The icon's paths as a TrueType glyph: y flipped, quadratic curves, outer
    contours clockwise and holes counter-clockwise (by how deeply each one
    sits inside the others), as TrueType and msdfgen expect."""
    paths = PATH.findall(svg)
    if not paths:
        raise ValueError('no <path d="...">')
    recording = RecordingPen()
    flip = TransformPen(recording, (SCALE, 0, 0, -SCALE, 0, GRID * SCALE + BOTTOM))
    for d in paths:
        parse_path(d, flip)
    shapes = [c for c in contours(recording) if any(op != 'moveTo' for op, _ in c)]
    outlines = [flatten(c) for c in shapes]
    pen = TTGlyphPen(None)
    quadratic = Cu2QuPen(pen, max_err=0.5, all_quadratic=True)
    for i, (contour, outline) in enumerate(zip(shapes, outlines)):
        depth = sum(1 for j, other in enumerate(outlines)
                    if j != i and len(other) > 2 and inside(outline[0], other))
        clockwise = area(outline) < 0
        want_clockwise = depth % 2 == 0
        replay(contour, quadratic if clockwise == want_clockwise else ReverseContourPen(quadratic))
    return pen.glyph()


def utf8_escape(codepoint):
    return ''.join('\\x%02x' % b for b in chr(codepoint).encode('utf-8'))


def write_font(icons, sources):
    order = ['.notdef']
    glyphs = {'.notdef': TTGlyphPen(None).glyph()}
    advances = {'.notdef': int(GRID * SCALE)}
    cmap = {}
    for index, icon in enumerate(icons):
        name = 'icon_' + icon['id']
        try:
            glyph = glyph_of(sources[icon['id']])
        except Exception as error:  # a broken icon names itself
            sys.exit('icons.py: %s.svg: %s' % (icon['id'], error))
        order.append(name)
        glyphs[name] = glyph
        advances[name] = int(GRID * SCALE)
        cmap[FIRST + index] = name
    font = FontBuilder(EM, isTTF=True)
    font.setupGlyphOrder(order)
    font.setupCharacterMap(cmap)
    font.setupGlyf(glyphs)
    glyf = font.font['glyf']
    metrics = {}
    for name in order:
        glyph = glyf[name]
        glyph.recalcBounds(glyf)
        metrics[name] = (advances[name], getattr(glyph, 'xMin', 0))
    font.setupHorizontalMetrics(metrics)
    font.setupHorizontalHeader(ascent=ASCENT, descent=-DESCENT)
    font.setupNameTable({'familyName': 'Stella Nova Icons', 'styleName': 'Regular'})
    font.setupOS2(sTypoAscender=ASCENT, sTypoDescender=-DESCENT, usWinAscent=ASCENT,
                  usWinDescent=DESCENT)
    font.setupPost()
    # The same icons make the same file: no dates in it.
    font.updateHead(created=0, modified=0)
    font.save(os.path.join(ICONS, 'icons.ttf'))


def write_header(icons):
    lines = ['#pragma once', '', '#include <string_view>', '',
             "// Generated by scripts/icons.py from content/icons/icons.json: each icon's",
             '// character as UTF-8, drawn by the icons font (a fallback after the',
             "// text's own fonts). Do not edit.",
             'namespace sn::icon', '{']
    for index, icon in enumerate(icons):
        constant = re.sub(r'[^A-Za-z0-9]', '_', icon['id']).upper()
        if constant[0].isdigit():
            constant = '_' + constant
        lines.append('inline constexpr const char* %s = "%s"; // %s'
                     % (constant, utf8_escape(FIRST + index), icon.get('title', '')))
    lines += ['', "// Every icon by its id (the catalog's, where it has one).",
              'struct Code', '{', '\tconst char* id;', '\tconst char* text;', '};', '',
              'inline constexpr Code ALL[] = {']
    for index, icon in enumerate(icons):
        lines.append('\t{"%s", "%s"},' % (icon['id'], utf8_escape(FIRST + index)))
    lines += ['};', '',
              "// The icon's character, or \"\" when there is none of that id.",
              'inline const char* Find(std::string_view id)', '{',
              '\tfor (const Code& code : ALL)', '\t{', '\t\tif (id == code.id)',
              '\t\t\treturn code.text;', '\t}', '\treturn "";', '}',
              '} // namespace sn::icon', '']
    header = os.path.join(ROOT, 'client', 'src', 'icon_codes.h')
    with open(header, 'w', encoding='utf-8', newline='\n') as out:
        out.write('\n'.join(lines))
    # In the repo's style, as the format check wants it.
    try:
        subprocess.run(['clang-format', '-i', header], check=True)
    except (OSError, subprocess.CalledProcessError):
        print('icons.py: clang-format did not run: format client/src/icon_codes.h by hand')


def write_manifest(icons):
    path = os.path.join(ROOT, 'content', 'initial.json')
    manifest = open(path, encoding='utf-8').read()
    characters = ''.join('\\u%04x' % (FIRST + i) for i in range(len(icons)))
    entry = ('{"name": "icons", "font": "icons/icons.ttf", "atlas": {"characters": "%s", '
             '"pageSize": 512}},' % characters)
    found = re.search(r'\t\t\{"name": "icons",[^\n]*\n', manifest)
    if found:
        manifest = manifest[:found.start()] + '\t\t' + entry + '\n' + manifest[found.end():]
    else:
        anchor = manifest.index('\t\t{"name": "sky"')
        manifest = manifest[:anchor] + '\t\t' + entry + '\n' + manifest[anchor:]
    with open(path, 'w', encoding='utf-8', newline='\n') as out:
        out.write(manifest)


def write_sprite(icons, sources):
    path = os.path.join(ROOT, 'site', 'stellanova.html')
    page = open(path, encoding='utf-8').read()
    begin, end = '<!-- icons:begin -->', '<!-- icons:end -->'
    if begin not in page or end not in page:
        print('icons.py: site/stellanova.html has no icons:begin/end markers; no sprite written')
        return
    symbols = []
    for icon in icons:
        body = ''.join('<path d="%s"/>' % d for d in PATH.findall(sources[icon['id']]))
        symbols.append('  <symbol id="i-%s" viewBox="0 0 24 24">%s</symbol>' % (icon['id'], body))
    sprite = (begin + '\n<svg class="sprite" aria-hidden="true" focusable="false" '
              'xmlns="http://www.w3.org/2000/svg">\n' + '\n'.join(symbols) + '\n</svg>\n' + end)
    page = page[:page.index(begin)] + sprite + page[page.index(end) + len(end):]
    with open(path, 'w', encoding='utf-8', newline='\n') as out:
        out.write(page)


def main():
    listing = json.load(open(os.path.join(ICONS, 'icons.json'), encoding='utf-8'))
    if listing.get('grid', GRID) != GRID:
        sys.exit('icons.py: the grid must be %d' % GRID)
    icons = listing['icons']
    sources = {icon['id']: open(os.path.join(ICONS, icon['id'] + '.svg'), encoding='utf-8').read()
               for icon in icons}
    write_font(icons, sources)
    write_header(icons)
    write_manifest(icons)
    write_sprite(icons, sources)
    print('icons.py: %d icons, U+%04X to U+%04X' % (len(icons), FIRST, FIRST + len(icons) - 1))


if __name__ == '__main__':
    main()
