# 0016. Sign-in with Steam (browsers' providers later); guests unsaved; the admin page

- Status: Proposed
- Date: 2026-10-04

## Context

- ADR 0014 opens accounts with a device key: every player is a guest, and
  guests reach the leaderboard. The developer (2026-10-04): players start
  anonymously, but guests are neither kept nor ranked. A player signs in
  to save the progress, and only then is in the data and on the
  leaderboard.
- Sign-in: Steam now, for Steam builds and the standalone build Steam
  runs beside. Google, Apple and Discord for browsers were chosen, then
  put off: no credentials for them yet. The game server checks sign-ins
  itself and keeps the accounts (no backend vendor yet: ADR 0005 still
  waits for its spike).
- The developer wants an admin page. WOS Observer's Django runs on the same
  machine, with its own logins; its superusers are the developer, its staff
  are WOS Observer's community admins.
- The server is C++ with no HTTP client; providers answer over HTTPS, and
  Google's and Apple's tokens are signed (RS256).

## Decision

- **Providers prove who the player is; the game server checks each proof
  with its provider** and keeps the account as before (ADR 0014's files).
  No passwords of ours.
  - **Steam** (any build where the Steam client runs, automatically):
    pith's `platform::RequestAuthTicket("stellanova")` (Steam's ticket for
    a web API), which the server checks with Steam's Web API
    (`ISteamUserAuth/AuthenticateUserTicket`, with the publisher key). A
    publisher-banned account is refused. The persona name comes from
    `GetPlayerSummaries`.
  - **Browsers, later:** the server's checks are written and tested, off
    until their apps exist. Google: an ID token from OpenID Connect's
    implicit flow (`response_type=id_token`), its RS256 signature checked
    against Google's published keys, its issuer, audience, expiry and
    nonce. Apple: Sign in with Apple's ID token (`response_type=code
    id_token`, `response_mode=fragment`, no scopes), checked the same way.
    Discord: OAuth2's code, traded for a token with the app's secret, then
    the user. The client's part (the game page redirects to the provider
    and takes the answer from its address) is not built yet. Until then a
    browser plays as a guest.
- **The server's sign-in file** (`stellanova-server --signin FILE`, outside
  the repo; on the machine a systemd credential, readable by root alone)
  holds the providers' ids and secrets: today the Steam key. The browser
  providers it lists are what the server offers the clients (`Signed`'s
  offers, with each one's authorization URL), so a provider turns on
  without a client update. Without the file, everyone is a guest.
- **HTTPS and signatures:** libcurl and OpenSSL's libcrypto on Linux (both
  in the Steam Runtime sysroot and on the machine), WinHTTP and CNG on
  Windows (a server on the development PC, the tests). A thread of the
  checker's own makes the calls; the game's tick never waits. Only
  `stellanova-server` and the tests link them (`sn::signin`): the client's
  own local server has no sign-ins.
- **Guests live in the server's memory, only while connected.** A device's
  key with no saved account opens a guest account at once: the whole loop,
  battles included, never written to the data folder, never on the
  leaderboard. When its connection goes (and its battle is over), the
  account is gone. The hub tells a guest that nothing is saved. (A local
  practice campaign saves its guest: `HubDesc::keepGuests`.) A guest may
  sign in on its connection (a second Login, with the proof: Steam came up
  late), and keeps its progress; a browser's sign-in must therefore not
  leave the page (a popup), or the guest is gone before it signs in.
- **Accounts** gain their identities (provider, id, the name it gave), the
  extra device keys handed out at sign-in, and two flags for the admin:
  banned and hidden. A Login carries the device's key and, after a
  sign-in, its proof:
  - the identity already has an account: that account wins. When the
    device's key does not open it, the server gives the device a new key
    for it, and a guest's progress on that device is dropped;
  - otherwise, the device's key opens a guest account: the identity joins
    it, and it is saved for the first time, progress and all;
  - otherwise, a new account for the identity, with a new key.

  The server answers each Login with `Signed`: the key to keep, the
  account's sign-in (none for a guest), why a sign-in failed, and the
  sign-ins it offers. An identity's account and a handed-out key are found
  through small link files beside the accounts (`links/`).
- **The leaderboard** lists only saved accounts with a sign-in that are not
  hidden or banned.
- **Pilot names:** Steam's persona name; otherwise "Pilot N". Google's name
  (often a real one) is never shown.
- **The admin page** (`site/stellanova-admin.html`) talks to a second
  listener of the server (`stellanova-server --admin ws:127.0.0.1:27081`),
  which only Caddy reaches, at `/stellanova/admin/ws`. Caddy lets the page
  and its connection through only for WOS Observer's superusers: it asks
  WOS Observer's Django for `/admin/auth/group/` (`forward_auth`), which
  answers 200 to superusers alone (403 to staff, a redirect to the login
  otherwise). WOS Observer's code does not change. Every admin operation
  goes to the server's log.
- **Protocol 6:** Login gains the proof; `Signed` is new.

## The admin connection

One JSON object per WebSocket message, both ways. A request has `op` and
may have `tag`, which the answer repeats. A failure answers
`{"op": "error", "tag", "error": "<what>"}`.

| Request | Answer |
| --- | --- |
| `{"op": "status"}` | `{"op": "status", "protocol", "catalog", "started", "now", "accounts", "signedIn", "battles", "skirmish", "online": [{"peer", "name", "account", "provider", "where"}], "leaderboard": [{"rank", "account", "name", "ring", "at", "provider"}]}` |
| `{"op": "accounts", "query", "filter", "offset", "limit"}` | `{"op": "accounts", "total", "offset", "accounts": [{"account", "name", "providers", "deepest", "battles", "kills", "created", "updated", "banned", "hidden", "online", "saved"}]}` |
| `{"op": "account", "account"}` | `{"op": "account", "account", "online", "saved", "devices", "data": {...}}` |
| `{"op": "grant", "account", "resources": {"metal", "he3", "chips"}}` | the account |
| `{"op": "ban", "account", "banned"}` | the account |
| `{"op": "hide", "account", "hidden"}` | the account |
| `{"op": "rename", "account", "name"}` | the account |
| `{"op": "signout", "account"}` | the account |
| `{"op": "unlink", "account", "provider"}` | the account |
| `{"op": "keep", "account"}` (a guest; the addendum of 2026-10-06) | the account |
| `{"op": "delete", "account"}` | `{"op": "deleted", "account"}` |

- `status`: `where` is `hub`, `battle` or `skirmish`; `provider` is `""`
  for a guest; the leaderboard's first 20; `catalog` is the catalog's
  hash as 8 hex digits; `accounts` counts the saved accounts, `signedIn`
  those of them with a sign-in.
- `accounts`: `query` matches a name, the start of an id, or an identity's
  id; `filter` is `all`, `signed`, `guests`, `banned` or `hidden`; newest
  `updated` first; `limit` is 50 by default and 200 at most. Each item is a
  summary (`providers` lists the account's sign-ins' provider names, one
  sign-in a provider at most). Connected guests are listed too, with
  `saved` false.
- `account`: `data` is the account's JSON as the client gets it (no keys:
  `sim::ToJson`; `banned` and `hidden` are in it when true; stacks are
  `{id: count}`); `devices` counts the extra keys.
- `grant` adds the amounts (negative ones take away, down to 0). `ban`
  disconnects the account's client. `signout` drops the extra keys: other
  devices must sign in again. `unlink` removes one sign-in. `delete`
  removes a saved account and its links (its file moves to `deleted/` in
  the data folder), and is refused while its player is connected
  (`"error": "online"`).
- Errors are short English words for the admin: `unknown account`,
  `unknown op`, `online`, `an empty name`, `an amount out of range`, `no
  such sign-in`, `its last sign-in: delete the account instead`, `not a
  JSON object`. A name is cleaned as players' names are (32 bytes,
  printable).
- Times are seconds since 1970.
- The page's `?ws=` (testing against a server on this machine) takes only
  `ws://127.0.0.1` or `ws://localhost`. The log does not know which
  superuser acted: Caddy passes no name (later, a header from Django).

## Consequences

- The developer creates a publisher Web API key in Steamworks; it goes in
  the server's sign-in file (`docs/web-test-deploy.md`). Browser
  sign-ins, when wanted: an OAuth client at Google, a Services ID at Apple
  (the Apple Developer Program), an application at Discord, each with the
  game page as its redirect URI; then the client's redirect.
- A browser player can try the game but keeps nothing, until a browser
  sign-in exists.
- Device keys are still bearer secrets: whoever copies a device's saved
  key opens its account. The admin's `signout` drops handed-out keys.
- Accounts saved before this ADR (guests then) stay in the data folder,
  off the leaderboard; the admin page lists and deletes them.
- WOS Observer's superusers can administer Stella Nova (today, the
  developer alone). Making someone a superuser there now means this too.
- A backend vendor, once chosen, takes the identities over: they are
  provider ids, not ours.

## Addendum (2026-10-06): kept guests, until phones can sign in

- Android's sign-in will be Google Play Games: the server trades the
  client's one-time code with Google for the verified player id, as it
  checks Steam's tickets. Android's own device id (ANDROID_ID) was turned
  down as an identity: the server cannot check it, and it changes with the
  app's signing key and a factory reset.
- Until the developer's Play Console account is verified, the developer
  wants to keep his phone's progress. **The admin keeps a guest:**
  - a connected guest's account gets the `kept` flag (`Account::kept`);
  - the server saves it like a signed-in account (`Server::Saves`), so its
    device's key opens it from then on (a guest's account id comes from
    its key);
  - it stays off the leaderboard;
  - the hub hears "device" as its sign-in ("Signed in with this device").
- **On the admin page:** an account with no sign-in has Keep this guest
  (op `keep`); kept accounts show a "kept guest" badge.
- **A sign-in later joins it**, as it joins any guest: the identity is
  added, the flag goes, and nothing is lost. Tested in `hub_test.cpp`.
- **The device key is still a bearer secret:** whoever copies it from the
  phone opens the account, and uninstalling the app loses it. Kept guests
  are for the developer's own devices while nobody else has the build.
  Drop the feature once phones sign in.
