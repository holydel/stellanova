# Generated content

Stella Nova's side of pith's plan (`../pith/docs/ai-content-plan.md`):
what we generate first, with which services, for how much, and in what
order. The look itself is in `docs/art-style.md`.

## Decided (2026-10-02, changed 2026-10-03)

- **Budget:** about $800 a month at most. The developer, 2026-10-03: it is
  a ceiling, not a target; spending starts as low as possible, in case the
  project stops (see Budget).
- **When:** after the game's core loop (2026-10-03; roadmap steps 47-54).
  The house style goes on meanwhile, as the developer's references come in.
- **Hosted services through their APIs.** No generation on the dev PC.
- **The target:** pith's content hub. Content is generated, seen in the
  engine as the game shows it, accepted into the packs, and appears in the
  running game. Claude drives the same tools through MCP.
- **The hub's center: a model reviewed in the editor.** The developer,
  2026-10-03: so far the plan reads as JSON written in advance, with
  prompts sent through pith as a proxy that makes models. We need to
  generate a model and let a person review it in the editor: rotate it,
  zoom, maybe move some sliders, then accept it into the pack.
- **First content:** the interface, ship models, ship icons, module icons,
  and space objects (planets, stars, comets, galaxies, nebulae).
- **House style:** none yet. It is made first, from the developer's
  references (`docs/art-style.md`), because every asset after it depends on
  it.
- **Design sheets are the source:** a ship is designed once; its five
  forms in the game, its icon, card and trailer shots are all generated
  from that sheet. Icons are not rendered from models.

## How each kind is made

The game has three scales (`docs/vision.md`). At the operational scale, as
in today's flight, the camera looks down from 40 m (54 m in a fight) with
a vertical field of view of 0.8 radians: about 32 pixels a meter on a
1080-pixel screen, so a fighter is 100-160 pixels long. The tactical scale
shows ships much farther away and with less detail; the strategic scale
shows only the map's zones. In battle, silhouettes and team colors carry
the read; close-up detail is for the hangar model.

### Design sheets: one ship, many forms

The developer's rule (2026-10-02): a ship is designed once, and everything
else of it is generated from that design, never from another form.

- **The sheet:** the ship from above (the game's view), the side, the
  front and three-quarters, consistent with each other and in the house
  style, made from the concept the developer chose. Beside it, in words:
  its name, class, the features every form must keep (shapes, markings,
  where the team color goes) and its length in meters.
- **Its forms:** first, the five ways the game shows the ship (the
  developer, 2026-10-03; `docs/vision.md`): the operational model, close
  and well shown; a tactical model, simpler, for the far view; its mark on
  the strategic map; the hangar model, seen up close; and its blueprint.
  Then its icon, its card in menus and the store, and its shots in
  trailers. Each is generated from the sheet's views and checked against
  the sheet.
- **Changes:** a new version of the sheet marks the forms made from the
  old one as stale; the hub makes them again when asked.
- **Tiers and variants:** a T2 or a faction's version of a ship starts as
  a design of its own from the T1's sheet (the vision's blueprints), so a
  family keeps its look.
- Planets, stations and anything else that appears in several forms get
  sheets the same way.

### Module icons

The developer, 2026-10-04: vector icons drawn by hand for now, one set for
the game and the site (`content/icons/README.md`), baked into a font of
the game's own (`scripts/icons.py`) and drawn as MTSDF glyphs, as planned
below. Generated ones can replace them later, the plan's way:

- **What:** one-color glyphs on a common grid, drawn by the game the way it
  draws text: tinted by the interface's palette, with an outline or a glow,
  crisp at any size (MTSDF, like pith's fonts). States (ready, cooling
  down, disabled) come from tint and fill, not from more pictures.
- **How:** Recraft's vector models (SVG, $0.05-0.08 each), with a Recraft
  style made from 1-10 of our approved icon anchors ($0.005 once, through
  Recraft's own API: fal's Recraft has no styles for V4). Its V4.1
  "Utility" line looks right for icons (flat, front-facing, predictable):
  that is our reading, not Recraft's own statement (its pages, checked on
  2026-10-02). 4 variants an icon, reviewed as a set: one sheet at 32, 48
  and 96 pixels, on the HUD's colors.
- **Into the game:** SVG entries in the pack (pith step 4 of the plan).
- **First set:** what the game has (gun, shield, hull, thruster) and what
  fleets need next; the full list comes with the fleet design (M1.4). A
  module that also appears in other forms gets a design sheet.

### Ship icons

- **What:** each ship from above as a one-color icon: the HUD's marks
  toward ships off screen, fleet lists, later the map.
- **How:** a form of the ship's design: the sheet's top view, turned into
  the module icons' style by an image edit that keeps its shape, then made
  a vector (Recraft's vectorizing, $0.01). Drawn like module icons:
  filled, outlined, tinted by team at run time.

### Ship models

1. **Concepts:** 4-8 per ship, from above and in three-quarter view, with
   the style's ship anchors as references: Gemini's Nano Banana Pro (up to
   6 object references) or Nano Banana 2 (3 style references and 10
   object references); GPT Image (up to 16 references by fal's page only;
   OpenAI's own pages do not say it) where a clean, transparent background
   matters. OpenAI's image model ids are dated snapshots: pin
   `gpt-image-2.5-sunburst-2026-09-08` (or `-flare`); there is no plain
   `gpt-image-2.5`.
2. **The design sheet:** the chosen concept's other views, generated with
   the concept as their reference, approved together, with the words
   beside them.
3. **To 3D:** the sheet's views through Meshy's multi-image to 3D,
   meshy-7.1: 30 credits textured, about $0.30 on the $100 plan (Ultra,
   10,000 credits) and $0.60 on the $20 plan, at most half of what fal
   charges for the same model. Tripo, Hunyuan3D, Rodin or TRELLIS.2
   through fal to compare ($0.20-0.50). Meshy's "smart topology"
   (100-15,000 faces) replaces its low-poly mode, which retires on
   2026-10-30; it needs its own model id, `meshy-t2`. Battles aim at
   about 500 ships, so fighters get 5,000-10,000 triangles and levels of
   detail. Meshy deletes API results after 3 days; the hub fetches them at
   once.
4. **Normalized by the packer:** meters, +Y up, facing the way the game
   expects, the pivot at the center, the length the design gives;
   textures at 1024 pixels for fighters, 2048 for big ships.
5. **Review in the editor,** beside the design sheet: a person turns it,
   zooms and moves sliders (the developer's item 3, 2026-10-03), and sees
   it from the game's own camera; accepted into the pack, it flies in the
   running game.

pith needs textured meshes for this (its meshes v1: UVs, base color,
normal and occlusion-roughness-metal maps); Kenney's models carry only
material colors.

### The interface

1. **Mockups:** a few full-screen pictures of the HUD and the main menu in
   each style direction (Nano Banana Pro; GPT Image when the layout and
   its words must be exact), to choose the look.
2. **Rules:** the chosen look becomes the style bible's interface section:
   colors, line widths, corner shapes, glow, fonts, spacing.
3. **Code:** the game draws the interface itself with pith's canvas and
   text: sharp at every resolution and on phones, translatable, animated.
   No pictures of panels with words baked in.
4. **Ornaments:** frames, brackets and emblems as Recraft vectors in the
   same style, packed like the icons.

### Space objects

- **Planets:** generated surface maps, 2:1 equirectangular: OpenAI's
  image model makes 3840x1920 (Nano Banana has no 2:1). Image models do
  not know the projection, so the seam is healed by shifting the map half
  its width and inpainting the join (an edit with a mask), and the poles
  are calmed. The game draws spheres with a planet material (the star
  lights a day side, an atmosphere glows at the rim, clouds turn on their
  own) below the play plane, moving slowly with parallax. A first set of
  8-12: rocky, desert, ice, ocean, lava, banded gas giants, one with rings.
- **Stars:** procedural, no generation: a sphere colored by temperature,
  darker at its rim, with a corona and flares from pith's effects.
- **Comets:** a rock (Kenney's meteor or a generated one) with two tails
  from pith's effects: curved, pale dust, and straight blue ions pointing
  away from the star.
- **Nebulae and galaxies:** generated images (transparent, or on black for
  additive blending) as large layers under the play plane, with parallax;
  galaxies as distant sprites. A whole sky can be a generated 2:1
  panorama through the cube-map path that NASA's star map uses now.

### Trailers and the store page

- **Clips and pictures of ships** are forms of their design sheets: video
  models that keep an object from reference images (Veo 3.1's reference
  images, Kling's elements, Runway's references; checked when we get
  there), $0.05-0.60 a second, and still pictures from the same views.
- They are for trailers and the store page, never inside the game, and
  they count in Steam's content survey (store material is part of it).

## Budget

The $800 is a ceiling (the developer, 2026-10-03): spending starts as low
as possible, in case the project stops. The hub's limits start at the
lowest that lets work go on, proposed at $50 a month and $5 a day, and go
up only on the developer's word. Each account gets its own cap, set as
low. The table is how the ceiling would split if the work ever needed it.

| Where | A month | What it buys |
| --- | --- | --- |
| OpenAI (images) | up to $200 | concepts with transparency, interface mockups, 2:1 planet maps |
| Google Gemini (images) | up to $150 | concepts and style work: about 1,000 Nano Banana Pro images |
| Recraft (vectors) | $60 of prepaid units | 750-1,200 icons and ornaments |
| Meshy, from step 52 | $20-100 plan | 1,000-10,000 credits: 33-333 textured models |
| fal, from step 52 | $100 prepaid | other 3D models to compare; a style model (LoRA, about $6 a run) if needed |
| Reserve | the rest | trailer clips, audio later, new models |

- **The real limit is review time,** not money: $800 buys more variants
  than one person can look at in a month. The first months stay within
  the starting caps.
- **Prices the hub estimates before each request:** a vector $0.05-0.08;
  an image $0.04-0.24 (more for large maps); a textured model $0.30-0.60
  at Meshy (by plan), $0.20-0.50 at fal.

### Not chosen, and why

- **Scenario** (a game-asset platform): on self-serve plans it may use our
  content to train its models; a promise not to needs an enterprise
  contract.
- **Skybox AI:** its API comes with the $60 plan, commercial licensing is
  listed only with the $140 plan, and its terms license our prompts and
  outputs to it. OpenAI makes 2:1 panoramas instead.
- **Midjourney:** no official API.
- **Meshy through fal:** twice Meshy's own price.

### Recraft's terms

Recraft's developer terms (2026-08-11) give us the outputs and their
copyright, but section 3.6 limits caching outputs to 30 days and forbids
"a persistent database of content". That reads as aimed at apps that
resell generation. Section 6.2 also forbids training models on the
outputs, and a Recraft style made from anchors that Recraft made may count
as that. Before the first Recraft icons ship, ask Recraft in writing that
both are fine: shipping its icons in a game, and styles made from
Recraft-made anchors.

## Accounts and keys

Open the first three when the plumbing starts (step 48, after the core
loop; the style directions use them too), each with the lowest cap, and
the others when ship models start:

| Service | When | Key variable | Its own cap |
| --- | --- | --- | --- |
| OpenAI API (platform.openai.com) | step 48 | `OPENAI_API_KEY` | the project's hard spend limit (Settings, Project, Limits) |
| Google AI Studio (aistudio.google.com), billing on | step 48 | `GEMINI_API_KEY` | the monthly spend cap (Spend; it lags about 10 minutes) |
| Recraft API (recraft.ai) | step 48 | `RECRAFT_API_TOKEN` | prepaid units: buy $20-60 at a time |
| Meshy, Pro or higher (the free plan has no API) | step 52 | `MESHY_API_KEY` | plan credits; leave auto-recharge off |
| fal (fal.ai) | step 52 | `FAL_KEY` | prepaid credits (no self-serve limit) |

- **Google's limits:** a new billing account is held to $250 a month
  (Tier 1) whatever the project's cap, and Batch jobs can run past the
  project's cap.
- **Setting a key:** Windows' environment variables window (Win+R,
  `rundll32 sysdm.cpl,EditEnvironmentVariables`), "User variables", New.
  pith reads the user's variables as saved, so `pith gen` sees a new key
  at once (pith ADR 0041); other tools need VS Code restarted. Typing keys
  into a terminal leaves them in its history.
- **Never paste a key into the chat** or into a file in a repository.
  `pith gen providers` (step 48) shows which keys are set, without them.

## Order

Roadmap steps 47-54, after the game's core loop (2026-10-03); pith's side
is its plan's "Order of work".

1. **As references come in:** the developer's references into `refs/`;
   Claude's palette board and notes (`pith image palette` is done).
2. **Step 48, the plumbing:** `pith gen` with Gemini, OpenAI and Recraft,
   recipes and budget limits. Built and tested against a fake provider;
   the first real run needs the keys above. In part (pith ADR 0041,
   2026-10-04): Gemini's images (`pith gen providers | image | accept |
   jobs`). `content/gen.json` holds the limits and refuses anything in
   `refs/` as a reference; jobs wait in `.pith/gen` (git-ignored). The
   developer's `GEMINI_API_KEY` works (a second key: the first one's
   project had no billing); the first tests are below.
3. **Three style directions** through `pith gen`; the developer chooses;
   the style bible and its anchors.
4. **Step 49, the hub v1:** generate a model, review it in the editor
   (rotate, zoom, sliders), accept it into the pack, see it in the running
   game; design sheets and their forms.
5. **Steps 50-54:** icons, the interface, ship models, space objects,
   trailer clips.

## First tests (2026-10-04)

Nine images through `pith gen image` from text alone (no house style yet),
$0.76 in all; kept in `content/local/gen-tests/` (git-ignored) with their
recipes.

- **Ships from above** (Nano Banana 2): a fighter and a cruiser with clear
  silhouettes, engines and team-color areas; the fighter at first had
  invented lettering on it, which "no text or lettering" removed.
- **The same ship from another view:** asked for the fighter's
  three-quarter view with its top view as the reference, the model gave
  the top view back. Design sheets need another way (pith ADR 0041).
- **Pro** drew the fighter as a painted sketch pointing left, and the HUD
  as pixel art: it takes more liberties. It costs twice as much and is
  half as fast.
- **A module icon:** a clean one-color glyph on black, ready to become a
  vector, though it reads more as "power" than "shield".
- **The HUD:** Nano Banana 2 made a busy, cinematic battle with numbers
  and labels despite "no readable text"; Pro's top-down screen read
  better. Mockups are for choosing a look, not for pixels (as planned).
- **Space:** a Mars-like desert planet lit from the left, with a thin
  atmosphere, and a teal and magenta nebula ready to use as a layer.

## The menu's splash (2026-10-04)

The first generated asset in the game: the main menu's background.

- **The brief (the developer):** laconic and neat; nebulae far behind; a
  planet that almost hides its sun; one ship and two asteroids in front;
  gray and blue.
- **The prompt:** the scene as the subject; the look as the `splash` kind's
  style in `content/gen.json`, so later key art matches. The backlit
  eclipse turns everything in front into rim-lit silhouettes. The left
  third stays dark for the title and the items.
- **The palette:** the developer's color reference, a picture of their
  own choosing kept outside the repository. Only its numbers went into the
  style (`pith image palette`: deep navy blacks, slate blues, electric blue
  to pale cyan light, steel gray hulls); its pixels went to no provider.
- **Four rounds, $2.25 in all:**
  1. six variants at 2K;
  2. the developer chose one, which was soft;
  3. it was edited into the palette at 4K, with itself as the reference;
  4. the developer chose Nano Banana Pro's second edit, the sharpest.
- **Where it is:** `content/generated/splash/menu.png` (5504x3072) and its
  recipe `menu.png.gen.json`.
  - The image stays on this PC for now (the developer, 2026-10-04); the
    recipe is committed.
  - Block compression will read its textures from this folder.
- **In the game:** `content/art.json` packs it at 1920x1072 with mip levels
  into `stellanova-art.pak` (10.5 MB).
  - The game reads that pack with its main pack, and the menu waits for
    both. The developer, 2026-10-04: no glimpse of the old menu. The
    turning ship and the stars are gone, and the splash is the whole
    background. Reading it first costs about 40 ms of startup on the PC.
  - Builds without the image show the menu on the dark.
  - On the web without mip levels (`content/art-web.json`): the web
    preloads every pack before starting, so the page loads 8 MB more,
    about 20 MB in all (the developer's choice, 2026-10-04).
  - Android's APK gets it from `scripts/build.ps1` (pith's
    `android-build.ps1 -ExtraPacks`).
  - The menu's screenshot test shows the splash.
- **Open:** the title's warm gold against the cold blue (keep it, or a cold
  white); a 2560-pixel pack for 1440p screens once textures are
  compressed.

## Concepts for the first models (2026-10-04)

The developer, 2026-10-04: concepts first, then Meshy; one colony scene;
turrets are not modeled (the player's ship shows its turret in the HUD).

- **36 pictures in 14 jobs, $2.44** (Nano Banana 2, and Pro for two colony
  scenes). A `concept` kind in `content/gen.json` carries the splash's
  palette for one object on a plain background, without lettering.
- **Waiting for the developer's picks** in `.pith/gen` (not accepted), with
  a local gallery that lists each picture's id, job, cost and a note.
- **What came out:**
  - the Lancer from above, four designs: L2 (slim, forward-swept wings,
    blue light strips, twin engines) reads best small; L3 grew from the
    menu splash's ship;
  - the Lancer at three-quarters, from L2 as the reference: the first
    attempt changed the view and kept the design. A lower camera failed:
    with a three-quarter picture among the references, the model copies
    its view. Next time, send only the top view;
  - the Raider: a pincer silhouette, patched plates, red-orange accents;
  - the five buildings, two each: one family (octagonal modules on pads,
    dark frames, blue light strips), mostly as clean game art, simple
    enough for image-to-3D;
  - the colony scene: daylight first (too bright, off-palette), then a
    night-side round from the chosen buildings: the moon's cratered
    ground, the gas giant behind the horizon, the buildings in an arc,
    the lower third left free. The game's scene follows that layout.
- **The developer's picks** (2026-10-05), accepted into
  `content/generated/concepts/` (the pictures stay on this PC like the
  splash; their recipes are committed): L1 (`lancer_top`), L3Q1
  (`lancer_three_quarter`), R4 (`raider_top`), MINE1, EXT2, FAB2, DEP1,
  CMD1 (`mine`, `extractor`, `fab`, `depot`, `command`) and SCN1
  (`colony_scene`: the daylight cluster, which the game's scene follows).
  - L3Q1 was made from L2, the same design as L1 drawn cleaner; it adds tail
    fins. If Meshy mixes the two views badly, a three-quarter view made
    from L1 alone costs about $0.14.
  - EXT2's stairs and railings are the hardest picks for image-to-3D.

## The first models (2026-10-05)

`pith gen model` (pith ADR 0041's addendum): Meshy's multi-image to 3D,
meshy-7.1, 30 credits ($0.60 on Pro) whatever the texture size; models go
to `content/generated/models/` (git-ignored like the pictures, their
recipes committed).

- **The depot first** (DEP1, 20k triangles, 2k maps): faithful to the
  concept from its corner; a smeared wall between the racks, the crane
  without its hook. The developer liked it and set the budget for the rest
  (2026-10-05): **150k triangles, 4k maps.**
- **The command center and the fab** (CMD1, FAB2) at that budget: 135k and
  131k triangles, 4096-pixel base color and normal maps, a 2048-pixel
  metal-roughness map, 34-37 MB each. Both close to their concepts.
- **What every Meshy model needs in the game** (`client/src/resources.cpp`):
  - its metal maps make painted walls half metal: the game scales metal by
    0.3;
  - no emissive map: the blue strips are painted, they do not glow;
  - its own octagonal pad: the scene draws none under it.
- **In the game:** the art pack (`content/art.json`) holds each model at
  1024 pixels, with the colony's light (`scripts/colony_light.py`) and the
  BRDF table: 83 MB for three, 261 MB for all eight. The web gets them
  too (the developer, 2026-10-05: on a fast line it still starts in
  seconds); pith's packs have no block compression or simplified meshes
  yet (pith ADR 0039), the way to make them smaller later.
- **The night of 2026-10-05** (the developer asleep, picks mine; every
  variant waits in `.pith/gen` to be swapped):
  - with a texture prompt (matte painted panels, glowing strips), Meshy
    adds an emissive map: the lights glow. The mine, the extractor, the
    depot again, the command center and the fab again, all at 150k and
    4k ($4.20);
  - the Lancer from L3Q1 and L1 together: faithful, its strips glowing;
    the Raider from R4 and a three-quarter view made from it (#2 of 4),
    red-orange glow; a crater (#1 of 4) for the colony's ground ($2.30
    with the concepts);
  - item pictures, the `icon` kind (a dark slate square, the item in a
    three-quarter view, a cyan rim light), four each: Pulse laser S #2,
    Plasma gun S #1, Plasma charge S #4 (forged iron rods), Shield
    booster S #4, Capacitor battery S #3, metal #3, He-3 #2, chips #4
    ($2.20).
  - In the game: ships are turned nose to -z and scaled to the Kenney
    ship's length (the flight's flames and shields follow); the crater is
    sunk to its flat ring and darkened to the ground's gray; buildings
    brighten under the pointer by their color (their own glow stays).

## Shipping generated content

- **Only paid plans' outputs ship.** Free tiers (CC BY, non-commercial)
  are for experiments. Each file's recipe records the plan's terms, and
  `pith gen report` lists every generated file in a pack.
- **Steam's content survey:** generated art that ships in the game (and
  any in store material) is declared on the store page; the report gives
  the list.
- **A person's pass on hero assets** (the ships players fly, the main
  interface): purely generated work has no copyright protection in the
  US; selection and changes by people do.

## Still open

- The caps in `content/gen.json`: $100 a month and $30 a day since
  2026-10-05 (the developer: the month's $50-100 matters, not the day's;
  Meshy's credits are prepaid). They were $50 and $5 from 2026-10-04.
- Where generated binaries live once they grow (a textured ship is 5-20
  MB): committed while small, Git LFS, or a bucket.
- Recraft's written answer (above).
