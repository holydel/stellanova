# References for the art style

Pictures from other games, films and concept art that show what Stella Nova
should look like. They become words and numbers in `docs/art-style.md`
(a palette, shapes, light), never pixels in the game. Everything here but
this file stays out of git.

## What goes where

| Folder | What | Look at |
| --- | --- | --- |
| `mood/` | the overall feeling: light, contrast, color | how dark space is, haze and glow |
| `ui/` | HUDs, menus, film interfaces | lines, shapes, fonts, glow, density |
| `ships/` | ships, best seen from above (the game's view) | silhouettes, proportions, materials, lights |
| `icons/` | item, module and ability icons | line or solid, detail, frames, color |
| `space/` | planets, stars, nebulae, galaxies, comets | realistic, painterly or stylized |
| `no/` | what Stella Nova should not look like | as useful as the rest |

- 5 to 15 pictures a folder: the strongest only.
- Say what you like in each, in the file name (`ui/oblivion-thin-lines.jpg`)
  or in a `notes.md` in the folder ("the orange against teal", "only the
  shape").
- Any format: PNG and JPEG are read directly; Claude converts WebP and AVIF
  (what websites often save).
- Screenshots can also be pasted straight into the chat with Claude.

## Where to find them

- Screenshots of games you like, your own or from press kits.
- Game interfaces: Game UI Database (gameuidatabase.com), Interface In
  Game (interfaceingame.com).
- Film stills: FILMGRAB (film-grab.com).
- Film interfaces: HUDS+GUIS (hudsandguis.com).
- Concept art: ArtStation (search "spaceship top down", "sci-fi HUD").
- Pinterest boards. PureRef (pureref.com) lays out a board, if you like
  arranging them (commercial work needs its paid license: $49 once for a
  small business).
- To pull a palette from a single picture yourself: Adobe Color's "Extract
  theme" (color.adobe.com).

## What happens next

1. `pith image palette refs --board refs/board.png` finds each picture's
   base tones and accents and the palette of all of them.
2. Claude describes the shapes, light and interfaces, and makes three style
   directions from text and the palette only (one page each: a fighter from
   above, a module icon, a piece of HUD, a planet).
3. You pick one, or mix two; it becomes the style guide, and its approved
   images become the anchors every generated asset is checked against.

Only our own images are sent to generation services, never these: other
people's work must not end up inside shipped content.
