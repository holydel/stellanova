#pragma once

#include <sn/sim/world.h>

#include <string>
#include <string_view>
#include <vector>

// What the client and the server say to each other (docs/adr/0007-local-server.md,
// 0012-prediction-and-protocol-4.md), as bytes for pith's net messages: a
// type byte, then the fields, little-endian. A client says Hello (and its
// Name) and gets its ship, the match's rules and the field's rocks in
// Welcome, or a Refusal; then it sends its controls (Input), a tick's worth
// each, numbered, and after each tick the server sends the tick's Events
// (reliable) and each player a Snapshot of its own: its ship first, the
// number of its last controls applied, then the nearest ships and shots
// (ADR 0003). Players Say chat lines and /commands; the server sends Chat.
// An end that does not know a message type ignores it. Floats from the
// wire must be finite, or the message is refused.
namespace sn::sim
{
using ph::usize;

constexpr u32 PROTOCOL_VERSION = 4;

enum class MessageType : u8
{
	Hello = 1, // client: reliable
	Welcome,   // server: reliable
	Input,     // client: unreliable, each tick
	Snapshot,  // server: unreliable, each tick
	Events,    // server: reliable, after a tick where something happened
	Name,      // client: reliable: the player's name, after Hello or later
	Say,       // client: reliable: a chat line, or a /command
	Chat,      // server: reliable: a player's line, or the server's notice
	Refusal,   // server: reliable, instead of Welcome: why not
};

// Names and chat lines, UTF-8: at most this many bytes.
constexpr u32 MAX_NAME_BYTES = 32;
constexpr u32 MAX_CHAT_BYTES = 200;

struct Hello
{
	u32 version = PROTOCOL_VERSION;
};

struct Welcome
{
	u32 ship = 0; // ShipId of the client's ship
	u64 tick = 0;
	f32 respawn = 0.0f; // s from a ship's end to its return
	// How the client's ship flies and fires, for its prediction.
	HullClass hull;
	WeaponClass weapon;
	bool autopilot = false;  // the server flies it (MatchDesc::autopilot): no prediction
	std::vector<Rock> rocks; // the field as it is: Events say what changes
};

// Controls travel as small integers (turn and thrust in 1/127 steps), so
// that what the server applies is exactly what the client predicted, and
// no NaN can come from the wire. What a client's controls become there.
ShipControls Quantize(ShipControls controls);

// The client's controls, one set a tick of its own, numbered from 1. Each
// message repeats the newest few not yet applied, oldest first, so that a
// lost one costs nothing.
constexpr u32 MAX_INPUTS = 8;
struct Input
{
	u32 last = 0;  // the number of the newest
	u32 count = 0; // 1 to MAX_INPUTS: `controls[count - 1]` is number `last`
	ShipControls controls[MAX_INPUTS];
};

// Why a server turned a client away.
enum class RefusalReason : u8
{
	Version = 1, // another protocol version: `version` is the server's
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

// A ship as snapshots carry it.
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
};

struct ShotState
{
	Vec2 position;
	Vec2 velocity; // to draw it between snapshots
	u8 team = PLAYERS;
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
	// none yet), and its gun's cooldown, for its prediction.
	u32 input = 0;
	f32 cooldown = 0.0f;
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
	u32 rock = NO_ROCK;
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

// The type of a message's bytes; 0 when they are empty.
MessageType TypeOf(const u8* data, usize size);

// Messages by type, for diagnostics: how many, and their bytes. Indexed by
// MessageType's values; 0 counts any other byte.
struct MessageCounts
{
	static constexpr u32 TYPES = 10;
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

// Text from a player, fit to show: valid UTF-8, without control characters
// or invisible ones (zero-width, direction marks and overrides), every kind
// of space as one plain space, none at either end, at most `maxBytes` (cut
// between characters).
std::string CleanText(std::string_view text, u32 maxBytes);

// The world's ships, up to MAX_SNAPSHOT_SHIPS, then its shots while the
// snapshot stays within MAX_SNAPSHOT_BYTES. For a player (`own`): its ship
// first, then the ships and shots nearest to it; its own shots only with
// `ownShots` (a client that predicts draws them itself). Without a player:
// in slot order. The match's fields (`wave`, `input`, `cooldown`) are the
// server's to fill.
void TakeSnapshot(const World& world, Snapshot& snapshot, ShipHandle own = {},
                  bool ownShots = false);
// A value's share of its whole, as snapshots carry it: 0 only for none.
u8 Share(f32 value, f32 whole);
// The world's last tick as an Events message.
void TakeEvents(const World& world, Events& events);
} // namespace sn::sim
