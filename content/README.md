# Content

What the game ships, and where it comes from. `initial.json` is the pack
manifest (`stellanova.pak`), built with pith's `pith pak build` (pith's
`content/README.md` explains manifests and sources). `sources.json` pins
the downloads; CMake fetches them into pith's shared cache.

## Text

`strings/<language>.txt`: everything the player reads, as `key = text`
lines. Code asks for keys (`Strings::Get`), never for words. English is the
only language so far; a translation is a copy of `en.txt` with the same
keys.

## Sources and credits

| Name | What | Terms | The game must show |
| --- | --- | --- | --- |
| `starmap` | NASA SVS Deep Star Maps 2020: the sky | free to use with credit | "NASA/Goddard Space Flight Center Scientific Visualization Studio. Gaia DR2: ESA/Gaia/DPAC" |
| `noto_sans`, `noto_sans_bold` | Noto Sans Regular and Bold: the interface's text | SIL Open Font License 1.1 | nothing on screen, but the OFL text must ship with the fonts (before a release: add it to the pack or the install) |
| `kenney_space` | Kenney's Space Kit: the ship and the meteors (glTF, baked into the pack's meshes) | CC0 1.0 | nothing (credit welcome: "Kenney, www.kenney.nl") |
| `kenney_scifi` | Kenney's Sci-Fi Sounds: interface sounds, the engine and the ambience | CC0 1.0 | nothing (credit welcome: "Kenney, www.kenney.nl") |

The menu's Credits page shows them (`credits.*` in `strings/en.txt`).
