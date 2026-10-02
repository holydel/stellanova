# Steam

Stella Nova is Steam app 1096260 ("StellaNova" in Steamworks, first named
StarIre). pith's samples use Spacewar (480) instead; this ID stays in this
repository.

## The app in Steamworks

As of 2026-10-02:

- **Operating systems:** Windows (64-bit only) and Linux + SteamOS.
- **Depots:**
  - 1096261 "StarIre Content": Windows, 64-bit.
  - 1096262 "StarIre Depot SteamDeck": Linux + SteamOS, 64-bit, Steam Deck
    only (Linux desktops do not get it; "All platforms" would give it to
    them and to the Steam Frame).
- **Launch options:** `stellanova.exe` on Windows, `stellanova` on Linux +
  SteamOS, both 64-bit, from the depot's root.
- **Install folder:** `StarIre` (Installation > General Installation).
- **License:** the Developers group's autogrant gives its members the
  developer comp package. Until 2026-10-02 the group autogranted only the
  DeveloperGameServer app's package, so the game was missing from the
  developer's library and Steam refused to start it ("ConnectToGlobalUser
  failed").
- **Still open** in Steamworks' checklist:
  - "Package Includes Linux + SteamOS Depot" and "Store And Devcomp
    Packages Match": the store package lacks depot 1096262. Add it there.
  - "Public Default Branch Includes 'stellanova.exe'" (and `stellanova`):
    the default branch still has StarIre's old build. Set a build of
    `scripts/steam-upload.ps1` live on it (below). The first, 25669850,
    went up on 2026-10-02.
  - Linux runtime: pith builds against Steam Linux Runtime 4.0
    (`../pith/docs/targets/steam-linux.md`); choose it under Installation
    > Linux Runtime.

## Uploading a build

`scripts/steam-upload.ps1` builds both Retail builds and uploads them with
SteamCMD (from the Steamworks SDK in `../pith/third_party/private/`, never
committed), as the build account `starire` (Edit App Metadata and Publish
App Changes in the Developers group).

1. **Once:** log the account in by hand. SteamCMD asks for its password and
   a Steam Guard code from its mailbox, then remembers the login in the
   SDK's `tools/ContentBuilder/builder/config/`:

   ```powershell
   & 'D:\Projects\pith\third_party\private\steamworks_sdk_165\sdk\tools\ContentBuilder\builder\steamcmd.exe' +login starire +quit
   ```

2. **Each build:**

   ```powershell
   scripts/steam-upload.ps1                      # build, upload
   scripts/steam-upload.ps1 -Preview             # build, check, upload nothing
   scripts/steam-upload.ps1 -Description 'bots at half speed' -SetLive beta
   ```

   It prints the build's ID. The description says the commit, and whether
   the working copy had changes.
3. **Set it live:** SteamCMD cannot set the default branch live: in
   Steamworks, SteamPipe > Builds, pick the default branch beside the build,
   Preview Change, then Set Build Live Now. `-SetLive <branch>` does it for
   a beta branch made there first.
4. **Get it:** the Steam client updates the game (restart Steam if it does
   not see the change); on the Deck too.

What each depot holds (`build/steam/content/`, copied fresh each time):

| Depot | Files |
| --- | --- |
| 1096261 (Windows) | `stellanova.exe`, `stellanova.pak`, `steam_api64.dll` |
| 1096262 (Linux + SteamOS) | `stellanova`, `stellanova.pak`, `libsteam_api.so` |

The game is built for Vulkan alone, so the Retail folder's other DLLs
(Dawn, DXC) stay out. The app build script and SteamCMD's logs go to
`build/steam/`.

## When Steam starts the game

Steam sets `SteamAppId`, and pith connects as that app (the log says
"platform: Steam, app 1096260, user ..."). Started outside Steam, the game
connects as 1096260 too, which works once the account owns the app.
