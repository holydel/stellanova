# Sounds

The developer's picks of 2026-10-05, from an audition of large packs whose
licenses let a commercial game ship them. Each file here is made from its
source: decoded, its start trimmed to the first sound, cut to its length
with a fade, peak-normalized (one-shots) or given a crossfaded seam and an
even level (loops), and saved as Ogg Vorbis (quality 5, 48 kHz). The
`content/initial.json` entries of the same names pack them.

| File | Plays | Source | Pack | License |
| --- | --- | --- | --- | --- |
| `ui_move.ogg` | moving and picking in menus | FUI Navigation Swipe-2 | Shapeforms Audio, Future UI (Sonniss GDC 2023) | Sonniss |
| `ui_back.ogg` | going back | back_002 | Kenney, Interface Sounds | CC0 |
| `fire.ogg` | a shot, ours and the enemies' (0.9 s) | Energy Gun shot_single_2 | SoundMorph, Future Weapons 2 (Sonniss GDC 2016) | Sonniss |
| `shield_hit.ogg` | a hit on a shield | heavy hit | RYK-Sounds, Laser Guns (Sonniss GDC 2023) | Sonniss |
| `hull_hit.ogg` | a hit on a hull (1.2 s) | IMPACT METAL Short Slam_03 | SmartSoundFX, Impacts, Hits & Whooshes (Sonniss GDC 2020) | Sonniss |
| `explode.ogg` | a ship breaking apart (2.2 s) | EXPLReal Medium Realistic Explosion 15 | David Dumais, Explosion SFX Pack (Sonniss GDC 2024) | Sonniss |
| `warp.ogg` | ships arriving | Vehicle5_BlastOff1 | David Dumais Audio, Sci-fi Flyby (Sonniss GDC 2020) | Sonniss |
| `thruster.ogg` | the engine, looped (4 s from 2.5 s) | Space rocket full power turbine (#1720) | Mixkit | Mixkit |
| `ambience.ogg` | the menu, looped (30 s from 6 s, stereo) | Space ship hum (#2136) | Mixkit | Mixkit |

The rest still come from Kenney's Sci-Fi Sounds (`content/sources.json`):
ui_confirm, hit, bump and blast.

## Licenses

- **Sonniss #GameAudioGDC** (license v2.0, 2026-08-27): free for personal
  and commercial projects, no attribution. The sounds may ship inside the
  game (its packs, the web build included) but never be passed on as sound
  effects: not in a library, an asset pack, pith or its samples, nor any
  public repository. Never sent to AI or generation tools. This repository
  is private: they stay here.
- **Mixkit** (Mixkit Sound Effects Free License): free in video games, no
  attribution; not to be redistributed on their own.
- **Kenney**: CC0.
