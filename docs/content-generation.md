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
  Restart VS Code afterwards so the tools see it. Typing keys into a
  terminal leaves them in its history.
- **Never paste a key into the chat** or into a file in a repository.
  `pith gen providers` (step 48) shows which keys are set, without them.

## Order

Roadmap steps 47-54, after the game's core loop (2026-10-03); pith's side
is its plan's "Order of work".

1. **As references come in:** the developer's references into `refs/`;
   Claude's palette board and notes (`pith image palette` is done).
2. **Step 48, the plumbing:** `pith gen` with Gemini, OpenAI and Recraft,
   recipes and budget limits. Built and tested against a fake provider;
   the first real run needs the keys above.
3. **Three style directions** through `pith gen`; the developer chooses;
   the style bible and its anchors.
4. **Step 49, the hub v1:** generate a model, review it in the editor
   (rotate, zoom, sliders), accept it into the pack, see it in the running
   game; design sheets and their forms.
5. **Steps 50-54:** icons, the interface, ship models, space objects,
   trailer clips.

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

- The starting caps: $50 a month and $5 a day, proposed on 2026-10-03.
- Where generated binaries live once they grow (a textured ship is 5-20
  MB): committed while small, Git LFS, or a bucket.
- Recraft's written answer (above).
