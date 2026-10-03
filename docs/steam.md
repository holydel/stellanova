# Steam

Stella Nova is Steam app 1096260 ("StellaNova" in Steamworks, first named
StarIre). pith's samples use Spacewar (480) instead; this ID stays in this
repository. This file is the one place for the game's Steam state: its
builds, which one is live, and what is left to do in Steamworks.

## Builds

The developer, 2026-10-03: the builds so far are a demo, made to check how
the per-platform depots and the upload script work. **Nothing is uploaded
or set live without the developer's direct command.** Shipped builds should
come from commits, with their build IDs recorded here, but not yet for the
nearest feature.

| Build | Uploaded | What | Live |
| --- | --- | --- | --- |
| 25669850 | 2026-10-02, morning | the first upload by `scripts/steam-upload.ps1` | set live on the default branch on 2026-10-02 (Windows starts it from Steam) |
| 25670828 | 2026-10-02 11:46 | "chat, /add_bots, no automatic waves online", built from uncommitted code (roadmap step 45) | unknown (2026-10-03) |

- Which build the default branch serves now is unknown. If it is still
  25669850, its players meet an online server that sends no waves (ADR
  0011) and have no chat to call bots in.
- Both builds speak protocol 3. Protocol 4 (own-ship prediction, roadmap
  step 56) changes the server, so after it the Steam demo cannot join
  online games until the developer orders a new upload. The developer
  agreed (2026-10-03).

## The app in Steamworks

As of 2026-10-02:

- **Operating systems:** Windows (64-bit only) and Linux + SteamOS.
- **Depots:**
  - 1096261 "StarIre Content": Windows, 64-bit.
  - 1096262 "StarIre Depot SteamDeck": Linux + SteamOS, 64-bit, Steam Deck
    only (Linux desktops do not get it; "All platforms" would give it to
    them and to the Steam Frame). Which of the two it should be is still
    the developer's choice.
- **Launch options:** `stellanova.exe` on Windows, `stellanova` on Linux +
  SteamOS, both 64-bit, from the depot's root.
- **Install folder:** `StarIre` (Installation > General Installation).
- **License:** the Developers group's autogrant gives its members the
  developer comp package. Until 2026-10-02 the group autogranted only the
  DeveloperGameServer app's package, so the game was missing from the
  developer's library and Steam refused to start it ("ConnectToGlobalUser
  failed").
- **Packages:** the store package "StarIre" (369571) holds both depots;
  "StarIre Developer Comp" (369569, the developers' license) and "StarIre
  for Beta Testing" (369570, keys) hold only 1096261 (Windows).
- **Still open** in Steamworks' checklist:
  - "Package Includes Linux + SteamOS Depot" and "Store And Devcomp
    Packages Match": the developer comp package lacks depot 1096262, so a
    developer's Steam Deck installs nothing ("0 mounted depots" in
    `~/.local/share/Steam/logs/content_log.txt`, then "missing
    executable"). Add 1096262 to 369569, and to 369570 for testers.
  - Linux runtime: pith builds against Steam Linux Runtime 4.0
    (`../pith/docs/targets/steam-linux.md`); choose it under Installation
    > Linux Runtime.
- **Done** in that checklist: "Public Default Branch Includes
  'stellanova.exe'" (and `stellanova`), since build 25669850 went live on
  2026-10-02 (above).

## Uploading a build

Only on the developer's direct command (2026-10-03), for an upload and for
setting a build live.

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
   the working copy had changes. Add the build to the table under Builds.
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
