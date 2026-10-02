#pragma once

#include <sn/sim/world.h>

#include <vector>

// What the client and the server say to each other (docs/adr/0007-local-server.md),
// as bytes for pith's net messages: a type byte, then the fields,
// little-endian. A client says Hello and gets its ship, the match's rules
// and the field's rocks in Welcome; then it sends its controls (Input), and
// after each tick the server sends the tick's Events (reliable) and a
// Snapshot of the match, the ships and the shots (ADR 0003).
namespace sn::sim
{
using ph::usize;

constexpr u32 PROTOCOL_VERSION = 3;

enum class MessageType : u8
{
	Hello = 1, // client: reliable
	Welcome,   // server: reliable
	Input,     // client: unreliable, the newest wins
	Snapshot,  // server: unreliable, each tick
	Events,    // server: reliable, after a tick where something happened
};

struct Hello
{
	u32 version = PROTOCOL_VERSION;
};

struct Welcome
{
	u32 ship = 0; // ShipId of the client's ship
	u64 tick = 0;
	f32 respawn = 0.0f;      // s from a ship's end to its return
	std::vector<Rock> rocks; // the field as it is: Events say what changes
};

struct Input
{
	u64 tick = 0; // the newest snapshot the client had
	ShipControls controls;
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
constexpr u32 MAX_SNAPSHOT_SHIPS = 32; // until snapshots go by interest (ADR 0003)
constexpr u32 MAX_SNAPSHOT_SHOTS = 64;

struct Snapshot
{
	u64 tick = 0;
	u32 wave = 0; // the match's: the enemies' wave, 0 before the first
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

// The type of a message's bytes; 0 when they are empty.
MessageType TypeOf(const u8* data, usize size);

// Messages by type, for diagnostics: how many, and their bytes. Indexed by
// MessageType's values; 0 counts any other byte.
struct MessageCounts
{
	static constexpr u32 TYPES = 6;
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

// The world's ships, up to MAX_SNAPSHOT_SHIPS, then its shots while the
// snapshot stays within MAX_SNAPSHOT_BYTES. The match's fields (`wave`) are
// the server's to fill.
void TakeSnapshot(const World& world, Snapshot& snapshot);
// A value's share of its whole, as snapshots carry it: 0 only for none.
u8 Share(f32 value, f32 whole);
// The world's last tick as an Events message.
void TakeEvents(const World& world, Events& events);
} // namespace sn::sim
