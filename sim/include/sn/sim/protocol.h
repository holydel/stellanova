#pragma once

#include <sn/sim/catalog.h>
#include <sn/sim/world.h>

#include <string>
#include <string_view>
#include <vector>

// What the client and the server say to each other (docs/adr/0007-local-server.md,
// 0012-prediction-and-protocol-4.md, 0013-ships-modules-damage.md,
// 0014-accounts-on-the-game-server.md, 0016-sign-in-and-admin.md), as bytes for pith's net
// messages: a type byte, then the fields, little-endian.
//
// A client says Hello, with what it comes for. For the skirmish it gets its
// ship, the match's rules and the field in Welcome at once (or a Refusal).
// For the hub (the solo loop) it says Login (with a sign-in's proof, after
// one) and gets Signed (the key to keep, how the account signs in, the
// sign-ins offered), then its account in Profile;
// its Requests change the account (each answered by a Profile, or a Chat
// notice saying why not), and a launch starts a battle with a Welcome; a
// battle's end comes as a Result, then a Profile.
//
// In a match it sends its controls (Input), a tick's worth each, numbered,
// and after each tick the server sends the tick's Events (reliable) and
// each player a Snapshot of its own: its ship first, the number of its last
// controls applied, then the nearest ships and shots (ADR 0003). Players
// Say chat lines and /commands; the server sends Chat. An end that does not
// know a message type ignores it. Floats from the wire must be finite, or
// the message is refused.
namespace sn::sim
{
using ph::usize;

constexpr u32 PROTOCOL_VERSION = 6;

enum class MessageType : u8
{
	Hello = 1, // client: reliable
	Welcome,   // server: reliable: a match begins
	Input,     // client: unreliable, each tick
	Snapshot,  // server: unreliable, each tick
	Events,    // server: reliable, after a tick where something happened
	Name,      // client: reliable: the player's name, after Hello or later
	Say,       // client: reliable: a chat line, or a /command
	Chat,      // server: reliable: a player's line, or the server's notice
	Refusal,   // server: reliable, instead of Welcome: why not
	Login,     // client: reliable: its account's key, in the hub
	Profile,   // server: reliable: the account, as JSON
	Request,   // client: reliable: an operation on the account, as JSON
	Result,    // server: reliable: how a battle ended, as JSON
	Signed,    // server: reliable: after Login, the account's sign-in
};

// Names and chat lines, UTF-8: at most this many bytes.
constexpr u32 MAX_NAME_BYTES = 32;
constexpr u32 MAX_CHAT_BYTES = 200;
// An account key: hex digits.
constexpr u32 KEY_BYTES = 32;
constexpr u32 MAX_REQUEST_BYTES = 1024;
// A sign-in's proof: a Steam ticket as hex digits, an ID token, a code.
constexpr u32 MAX_PROOF_BYTES = 8192;
constexpr u32 MAX_PROVIDER_BYTES = 16;
constexpr u32 MAX_NONCE_BYTES = 64;
constexpr u32 MAX_OFFER_BYTES = 1024;
constexpr u32 MAX_OFFERS = 8;
constexpr u32 MAX_PROFILE_BYTES = 256 * 1024;

// What a client comes for.
enum class Joining : u8
{
	Skirmish, // the shared match, at once
	Hub,      // its account: Login next
};

struct Hello
{
	u32 version = PROTOCOL_VERSION;
	u32 catalog = GetCatalog().hash; // the server's must be the same
	Joining joining = Joining::Skirmish;
};

enum class MatchKind : u8
{
	Skirmish,
	Battle, // a node of the star map, the player's fleet against its enemies
};

// A crate as the client keeps it: its index in the world's pool.
struct CrateState
{
	u32 index = 0;
	Vec2 position;
};

struct Welcome
{
	u32 ship = 0; // ShipId of the client's ship
	u64 tick = 0;
	f32 respawn = 0.0f; // s from a ship's end to its return (skirmish)
	MatchKind kind = MatchKind::Skirmish;
	u32 ring = 0; // a battle's
	// How the client's ship flies, for its prediction.
	HullClass hull;
	bool autopilot = false; // the server flies it (MatchDesc::autopilot): no prediction
	// Its slots' catalog ids ("" for an empty slot), for the HUD.
	std::vector<std::string> modules;
	std::vector<Rock> rocks; // the field as it is: Events say what changes
	std::vector<CrateState> crates;
};

// Controls travel as small integers (turn and thrust in 1/127 steps), so
// that what the server applies is exactly what the client predicted, and
// no NaN can come from the wire. What a client's controls become there.
ShipControls Quantize(ShipControls controls);

// The client's controls, one set a tick of its own, numbered from 1. Each
// message repeats the newest few not yet applied, oldest first, so that a
// lost one costs nothing. With them, its turrets' chosen target.
constexpr u32 MAX_INPUTS = 8;
struct Input
{
	u32 last = 0;  // the number of the newest
	u32 count = 0; // 1 to MAX_INPUTS: `controls[count - 1]` is number `last`
	ShipControls controls[MAX_INPUTS];
	u32 target = 0; // ShipId; 0 for none
};

// Why a server turned a client away.
enum class RefusalReason : u8
{
	Version = 1, // another protocol version or catalog: `version` is the server's
	Full,        // the match has its most players
};
constexpr u8 LAST_REFUSAL_REASON = u8(RefusalReason::Full);

struct Refusal
{
	RefusalReason reason = RefusalReason::Version;
	u32 version = PROTOCOL_VERSION;
};

struct Name
{
	std::string name;
};

struct Say
{
	std::string text;
};

// A player's chat line (`ship`, `name`, `text`), or the server's notice:
// then `text` is a strings-table key ("chat.joined"), with `name` and
// `extra` for its "{}"s.
struct Chat
{
	u32 ship = 0; // the speaker's ShipId; 0 for the server
	bool notice = false;
	std::string name;
	std::string text;
	std::string extra;
};

struct Login
{
	std::string key;  // KEY_BYTES hex digits
	std::string name; // to show on the leaderboard
	// After a sign-in: "steam", "google", "apple" or "discord", and its proof
	// (a ticket, an ID token, a code); the nonce the browser sent with it.
	std::string provider;
	std::string proof;
	std::string nonce;
};

// A sign-in the server offers: where a browser starts it (without its
// state and nonce, which the client adds).
struct SignInOffer
{
	std::string provider;
	std::string url;
};

// The answer to a Login, before its Profile.
struct Signed
{
	std::string key;      // the key to keep from now on (it may be new)
	std::string provider; // the account's sign-in; "" for a guest
	std::string error;    // why a sign-in failed: a strings key; "" when it did not
	std::vector<SignInOffer> offers;
};

// JSON texts: an account, an operation, a battle's end.
struct Profile
{
	std::string json;
};

struct Request
{
	std::string json;
};

struct Result
{
	std::string json;
};

// A ship as snapshots carry it.
constexpr u8 NO_BEAM = 0xff;
struct ShipState
{
	u32 id = 0; // ShipId
	Vec2 position;
	Vec2 velocity;
	f32 angle = 0.0f;
	ShipControls controls; // for the client's looks (banking, engine sound)
	u8 team = PLAYERS;
	// Shares of the hull's, 0 to 255; health 0 is a wreck, never a ship
	// with a little health left.
	u8 health = 0;
	u8 shield = 0;
	u8 capacitor = 0;
	u8 turret = 0;     // its first turret's angle from the nose, in 1/256 turns
	u8 beam = NO_BEAM; // the index in this snapshot of the ship its beam burns
};

struct ShotState
{
	Vec2 position;
	Vec2 velocity; // to draw it between snapshots
	u8 team = PLAYERS;
};

// The recipient's own ship, for its HUD and its prediction.
constexpr u8 MODULE_WORKING = 1;
constexpr u8 MODULE_OFFLINE = 2;
struct OwnState
{
	f32 capacitor = 0.0f;         // GJ
	f32 cargoUsed = 0.0f;         // m^3
	f32 cargoMass = 0.0f;         // t
	u32 ammo = 0;                 // charges in the hold
	ph::u16 loaded = 0;           // its first gun's magazine
	f32 reloading = 0.0f;         // s left of its reload
	u8 modules[MAX_MODULES] = {}; // each slot's health share
	u8 flags[MAX_MODULES] = {};   // MODULE_WORKING, MODULE_OFFLINE
};

// A snapshot fits one unreliable message (net::MAX_UNRELIABLE_BYTES): ships
// first, then as many shots as there is room for.
constexpr u32 MAX_SNAPSHOT_BYTES = 1200;
constexpr u32 MAX_SNAPSHOT_SHIPS = 32; // the nearest, until snapshots go by interest (ADR 0003)
constexpr u32 MAX_SNAPSHOT_SHOTS = 64;

struct Snapshot
{
	u64 tick = 0;
	u32 wave = 0; // the match's: the enemies' wave, 0 before the first
	// The recipient's own: the number of its last controls applied (0 for
	// none yet), and its ship's state.
	u32 input = 0;
	OwnState own;
	u32 count = 0;
	ShipState ships[MAX_SNAPSHOT_SHIPS];
	u32 shotCount = 0;
	ShotState shots[MAX_SNAPSHOT_SHOTS];

	const ShipState* Find(u32 id) const;
};

// The events of one tick, with ships as ShipIds (0: none).
struct EventState
{
	EventType type = EventType::Fired;
	u32 ship = 0;
	u32 other = 0;
	u32 index = NO_INDEX;
	Vec2 position;
	f32 strength = 0.0f;
};

struct Events
{
	u64 tick = 0;
	std::vector<EventState> events;
};

// A ship's handle as one number, for the wire.
u32 ShipId(ShipHandle ship);
ShipHandle ShipFromId(u32 id);

std::vector<u8> Write(const Hello& message);
std::vector<u8> Write(const Welcome& message);
std::vector<u8> Write(const Input& message);
std::vector<u8> Write(const Snapshot& message);
std::vector<u8> Write(const Events& message);
std::vector<u8> Write(const Name& message);
std::vector<u8> Write(const Say& message);
std::vector<u8> Write(const Chat& message);
std::vector<u8> Write(const Refusal& message);
std::vector<u8> Write(const Login& message);
std::vector<u8> Write(const Profile& message);
std::vector<u8> Write(const Request& message);
std::vector<u8> Write(const Result& message);
std::vector<u8> Write(const Signed& message);

// The type of a message's bytes; 0 when they are empty.
MessageType TypeOf(const u8* data, usize size);

// Messages by type, for diagnostics: how many, and their bytes. Indexed by
// MessageType's values; 0 counts any other byte.
struct MessageCounts
{
	static constexpr u32 TYPES = 15;
	u64 count[TYPES] = {};
	u64 bytes[TYPES] = {};

	void Add(const u8* data, usize size);
};

// "hello", "welcome"...: a type's name, by MessageCounts' index ("other" for 0).
const char* MessageName(u32 index);
// False when the bytes are not that message, or are cut short.
bool Read(const u8* data, usize size, Hello& message);
bool Read(const u8* data, usize size, Welcome& message);
bool Read(const u8* data, usize size, Input& message);
bool Read(const u8* data, usize size, Snapshot& message);
bool Read(const u8* data, usize size, Events& message);
bool Read(const u8* data, usize size, Name& message);
bool Read(const u8* data, usize size, Say& message);
bool Read(const u8* data, usize size, Chat& message);
bool Read(const u8* data, usize size, Refusal& message);
bool Read(const u8* data, usize size, Login& message);
bool Read(const u8* data, usize size, Profile& message);
bool Read(const u8* data, usize size, Request& message);
bool Read(const u8* data, usize size, Result& message);
bool Read(const u8* data, usize size, Signed& message);

// Text from a player, fit to show: valid UTF-8, without control characters
// or invisible ones (zero-width, direction marks and overrides), every kind
// of space as one plain space, none at either end, at most `maxBytes` (cut
// between characters).
std::string CleanText(std::string_view text, u32 maxBytes);

// The world's ships, up to MAX_SNAPSHOT_SHIPS, then its shots while the
// snapshot stays within MAX_SNAPSHOT_BYTES. For a player (`own`): its ship
// first and its own state, then the ships and shots nearest to it. Without
// a player: in slot order. The match's fields (`wave`, `input`) are the
// server's to fill.
void TakeSnapshot(const World& world, Snapshot& snapshot, ShipHandle own = {});
// A value's share of its whole, as snapshots carry it: 0 only for none.
u8 Share(f32 value, f32 whole);
// An angle as 1/256 turns, and back.
u8 AngleToByte(f32 angle);
f32 ByteToAngle(u8 turns);
// The world's last tick as an Events message.
void TakeEvents(const World& world, Events& events);
} // namespace sn::sim
