# 0014. Accounts and the solo loop on our game server, until a backend

- Status: Proposed
- Date: 2026-10-04

## Context

- The solo loop (`docs/solo-loop.md`) keeps a colony, an inventory,
  ships, map progress and a leaderboard per account, and grows while the
  game is closed.
- ADR 0002: only our dedicated servers grant persistent rewards; solo
  battles with rewards are played online.
- `CLAUDE.md`: backend services are third-party only. ADR 0005 (brainCloud)
  waits for a spike, and nothing about a vendor is settled.
- The developer (2026-10-04): until then, the game server keeps the
  accounts in files.
- The web build keeps no files between visits (its data folder is in
  memory).

## Decision

- **Accounts are JSON files** in the server's data folder
  (`stellanova-server --data DIR`), one per account, behind
  `sn::server::AccountStore` (load, save, list). The vendor, once chosen,
  replaces the store; the rules stay. Writes go to a new file, then a
  rename, so a crash never leaves half an account.
- **Identity is a guest key per device** (amended by ADR 0016: guests are
  no longer saved; a sign-in with Steam saves the account, and only
  signed-in accounts are ranked): 128 random bits, made by the
  client on its first run and kept in `account.txt`. A new pith call keeps
  small files between runs: files in the data folder, or the browser's
  localStorage on the web (`os::ReadSavedFile`, `os::WriteSavedFile`). The
  server names the account file after a hash of the key, and keeps the key
  inside to compare. Steam and the other stores' identities come with the
  vendor.
- **The rules are shared code** (`sim/account.h`, `sim/starmap.h`): plain
  functions on plain data. They cover production from the clock, upgrade
  costs, building ships and modules, fitting, the map's nodes, launch
  costs and loot. The server applies them on its own clock; the client uses
  them only to show previews.
- **The protocol:**
  - Login (the key and a name) is answered by Profile: the account as JSON
    text, with the server's time.
  - Request (a JSON operation: upgrade, build, fit, load, repair, launch)
    is answered by a Profile, or a Notice saying why not.
  - A launch starts a battle: Welcome, snapshots and events as before.
    Then Result, and a new Profile.
- **The server hosts several matches:** the shared skirmish (the menu's
  Online, with chat and bots) and a battle per launch. A player is in the
  hub (no match) or in one match.
- **Leaderboard:** after a deeper ring is cleared, the server writes
  `leaderboard.json` (the top 100: name, deepest ring, when) to a folder
  the website serves.
- **Local games** keep their accounts in the client's data folder: a
  practice account, apart from the online one (ADR 0002). The menu's
  campaign plays online; `--local` plays it on this machine (development).

## Consequences

- The online server becomes stateful: its data folder needs backups, and
  an update restarts battles in progress (they end as "Return").
- The server checks every request against the rules, rate-limits them,
  and caps account files (256 KB) and accounts (10 000) for now.
- A lost `account.txt` (a new device, cleared browser data) loses the
  account. Linking comes with the vendor.
- `CLAUDE.md`'s "third-party only" now has this exception until the vendor
  replaces the store.
