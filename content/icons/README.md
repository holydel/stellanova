# Icons

One set of vector icons for the game and its website (drawn 2026-10-04;
the developer chose hand-drawn vectors over generated ones for now,
`docs/content-generation.md`). Each icon is `<id>.svg`; `icons.json` lists
them in order, with a title. Ids are the catalog's where the icon stands
for a catalog thing (`plasma_s`, `lancer`, `mine`, `metal`), the website's
stat names otherwise (`hull`, `range`).

`scripts/icons.py` turns them into:

- `icons.ttf`: a font with each icon at U+E000 plus its place in
  `icons.json`, packed by the game like its other fonts (MTSDF glyphs) and
  used after them as a fallback, so an icon's character draws inside any
  text, sized and tinted like the letters;
- `client/src/icon_codes.h`: the characters by name, and `icon::Find(id)`;
- the "icons" entry of `content/initial.json`;
- the website's sprite in `site/stellanova.html` (`<use href="#i-<id>">`).

Run it after changing an icon (it needs fontTools: `pip install fonttools`).
The order in `icons.json` is the characters' order: add new icons at the
end.

## Rules for an icon

The font and the distance fields depend on them:

1. `viewBox="0 0 24 24"`, artwork within 1..23, centered, of a weight like
   the others.
2. Only `<path d="...">` elements, filled with `currentColor`. No strokes
   (line work is drawn as filled outlines, about 2 units wide), no circles,
   rectangles, transforms, groups, clips, gradients, opacity or text. Path
   commands M L H V C S Q T A Z.
3. `fill-rule="nonzero"`: holes are separate contours wound opposite to
   their outline; contours do not overlap one another or themselves.
4. Simple enough for 16 pixels: few bold parts, gaps of 1.5 units at
   least.
5. The style: geometric and technical, solid silhouettes with cut-in
   details, 45° chamfers. No brand marks.
