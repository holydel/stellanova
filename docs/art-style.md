# Art style

Stella Nova has no house style yet (2026-10-02). This file says how we
make one, and holds it once chosen: the palette, light, shapes and rules
that every asset follows, made by hand or generated
(`docs/content-generation.md`).

## How we make it

1. **References (the developer).**
   - Put screenshots, film stills and concept art in `refs/`, sorted by
     topic: `mood`, `ui`, `ships`, `icons`, `space`, plus `no` for what to
     avoid (`refs/README.md`).
   - 5-15 pictures a folder, each with a word on what you like in it.
   - Small sets can also be pasted into the chat.
2. **Analysis (Claude).**
   - `pith image palette refs --board refs/board.png` finds each picture's
     base tones and accents, and the palette of all of them together.
   - Claude reads the pictures for light, shapes, materials and interface
     language, and writes that down.
3. **Three directions (Claude).** Each is one page:
   - a name and three words;
   - a palette;
   - a fighter seen from above, a module icon, a piece of HUD and a planet.

   They are generated from text and the palette only, never from the
   references' pixels. Cost: about $5-10.
4. **The choice (the developer):** one direction, or parts of two.
5. **The style bible (Claude):**
   - this file's sections below, filled in;
   - 10-20 **anchors**: our own generated images that show the style,
     approved by the developer, in `content/style/`.
6. **Kept consistent:**
   - every generation carries the style's prompt fragment for its kind,
     and the anchors as reference images;
   - the hub shows each result beside the anchors;
   - icons go through a Recraft style made from the anchors;
   - a trained style model (a LoRA) comes only if results drift.

## What the references should answer

The vision's rule comes first: readable battles matter more than graphics
detail (`docs/vision.md`), with up to about 500 ships in a battle. The
style must read at all three scales (operational, tactical, strategic), so
it is judged on views of several ships, not only on one fighter at the
flight camera (2026-10-03).

- **Mood:** how dark space is, contrast, haze and glow, warm or cold.
- **Palette:**
  - the base tones;
  - our team against theirs, readable at a glance from above;
  - energy: shots, shields, engines;
  - the interface.
- **Ships:** the game shows them from above and small, so their
  silhouettes and team colors carry them. Also: proportions, how much
  detail, materials (painted hulls, bare metal, panel lines, lights), and
  how factions differ.
- **Interface:** line weight, shapes (chamfers, brackets, rounded),
  fonts, transparency, glow, how dense, how information is grouped.
- **Icons:** line or solid, how much detail, framed or not, one color or
  several.
- **Space:** realistic (NASA's pictures), painterly or stylized; how busy
  the background is; the look of planets.

## Rules for references

- **Never committed and never sent to generation services:** `refs/` is
  git-ignored. Other people's work gives us words and numbers, never
  pixels. Only our own images (the anchors) go to generators as
  references. That keeps shipped content free of others' work, for
  copyright and for Steam's content survey.
- Claude reads the references to describe them; that is analysis, not
  generation.

## The style bible

To fill in once a direction is chosen.

### Pillars

Three words that decide arguments.

### Palette

The first colors chosen (the developer, 2026-10-04): the menu's splash and
other key art, from a color reference of their choosing
(`docs/content-generation.md`). They are the `splash` kind's style in
`content/gen.json`:

- space and dark surfaces: deep navy blacks `#0A0F16`, `#111722`, `#192130`,
  and slate blues `#242D41`, `#3E4D6B`;
- light, the only bright accent: electric blue to pale cyan `#4E87BB`,
  `#71B2D8`, `#ACDBE9`;
- hulls: cool steel grays `#686A79`, `#97999F`.

The roles for the whole game, to fill in:

| Role | Color | Where |
| --- | --- | --- |
| Space | | the background |
| Hull, main | | ships' painted surfaces |
| Hull, second | | panels, trim |
| Our team | | lights, markings, the HUD's own marks |
| Their team | | lights, markings, enemy marks |
| Energy | | shots, beams |
| Shields | | hits on shields |
| Engines | | flames and trails |
| Interface | | lines, panels, text |
| Warning | | low shield, hull |
| Danger | | critical, enemy lock |

### Light

The star's key light, rim light, ambient, bloom and contrast.

### Ships

Shape language by faction and class; silhouettes from above; detail per
size class; materials; lights; team markings.

### Interface

Lines, shapes, panels, fonts, transparency, glow and motion. The game draws
its interface in code from these rules (crisp at any size, translatable):
generated pictures only decide the look and give decorative pieces as
vectors.

### Icons

- **Modules:** line or solid; stroke width; a grid (for example 64 pixels
  with a 4-pixel margin); one color, tinted by the interface palette;
  states (ready, cooling down, disabled).
- **Ships:** generated from each ship's design sheet (its view from
  above), in the module icons' style (`docs/content-generation.md`).

### Space

The background (stars, nebulae), planets, stars, comets and galaxies: how
realistic, what colors, how much contrast against the ships.

### Prompt fragments

The text every request of a kind includes (ship, icon, interface, planet,
nebula).

### Anchors

The approved images in `content/style/`, each with what it shows.
Anchors show the style; design sheets (`content/designs/`, one per ship
and the like) show one thing in it. Both are our own work, and both go to
generators as references.

### Not this

What `refs/no/` taught.
