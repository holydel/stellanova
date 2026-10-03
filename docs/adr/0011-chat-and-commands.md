# 0011. Chat and commands: lines for everyone, bots on request

- Status: Accepted (2026-10-03, by the developer)
- Date: 2026-10-02

## Context

- The developer (2026-10-02), after two ships met online (one from Steam,
  one from the website): "lets add the chat window. lets remove bots. lets
  add chat commands like /add_bots so i can spawn bots by myself".
- ADR 0010: one online match for everyone, which until now sent waves of
  bots as soon as anyone came.
- ADR 0006: no text in the code; the strings tables hold it.
- Players come from Steam (with a name), from browsers and from phones
  (without one). Phones have no keyboard until the app asks for the
  on-screen one, which pith could not do.

## Decision

- **Protocol** (`sim/protocol.h`), three message types within version 3
  (an end that does not know a type ignores it, so older clients and
  servers still play together):
  - `Name` (client, after Hello): Steam's name for the player, if any.
  - `Say` (client): a line, or a command when it starts with `/`.
  - `Chat` (server): a player's line (speaker, name, text), or the server's
    notice: a strings-table key with up to two values for its `{}`s, so
    that the client shows it in its own language.
  - Text is UTF-8: names up to 32 bytes, lines up to 200. `CleanText`
    drops invalid sequences and control characters, trims spaces and cuts
    between characters, on both ends.
- **The server** gives each player a name: its `Name`, else "Pilot N".
  - It tells everyone who joined and who left.
  - A player may say five lines at once, then one a second; more gets
    "Slow down".
  - It logs lines and commands.
- **Commands:**
  - `/add_bots [count]`: bots (3 by default) 80-100 m from the player who
    asked, as many as fit in a snapshot (32 ships with the players);
  - `/remove_bots`;
  - `/waves on`, `/waves off`: the skirmish's waves;
  - `/who`;
  - `/help`.
  - What a command does goes to everyone ("Ann called in 3 bots"); an
    unknown one only to whoever typed it.
- **No more waves online:** `stellanova-server` starts without them
  (`--waves` brings them back from the start). The local skirmish keeps
  its waves; commands work there too.
- **The client:**
  - Enter opens the chat; so does a Chat button, which shows once a finger
    has touched the screen. Enter sends, Esc (or Back) cancels.
  - While typing, keys do not fly the ship.
  - The last six lines show above the controls' hint and fade after 12 s,
    unless the chat is open. Our own lines are green, the server's blue.
  - The panel moves above the on-screen keyboard.
  - The font gained the Russian, Ukrainian and Kazakh letters and common
    typographic punctuation: 101 glyphs, 3.8 KB more in the pack.
- **pith** (its ADR 0027's addendum): `StartTextInput`, `StopTextInput`,
  `IsTextInputActive` and `GetKeyboardInset`. On Android they show
  GameActivity's keyboard and turn its edits into the TextInput and
  Backspace events other OSes send; its Send key is Return.

## Consequences

- Tested: lines reach every player with their speaker, commands call in and
  send away bots, unknown commands answer only their sender, floods are
  held back, leaving is told, and text is cleaned and cut between
  characters (`server_test.cpp`). On Windows, typed Latin, Cyrillic and
  Kazakh text shows in the panel.
- The phone's keyboard path is built, not yet tried on a phone.
- Anyone online can call in 30 bots: fine among testers, a vote or a host
  later.
- **Not yet:** names chosen in the game (Settings), muting or blocking, a
  profanity filter, chat on the Steam Deck through Steam's keyboard
  (`ShowFloatingGamepadTextInput`; Steam+X works meanwhile), chat on phone
  browsers.
