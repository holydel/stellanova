#pragma once

#include <sn/sim/world.h>

#include <vector>

// What the client and the server say to each other (docs/adr/0007-local-server.md),
// as bytes for pith's net messages: a type byte, then the fields,
// little-endian. A client says Hello and gets its ship in Welcome; then it
// sends its controls (Input) and the server answers each tick with a
// Snapshot of the ships (ADR 0003).
namespace sn::sim
{
using ph::u8;
using ph::usize;

constexpr u32 PROTOCOL_VERSION = 1;

enum class MessageType : u8
{
	Hello = 1, // client: reliable
	Welcome,   // server: reliable
	Input,     // client: unreliable, the newest wins
	Snapshot,  // server: unreliable, each tick
};

struct Hello
{
	u32 version = PROTOCOL_VERSION;
};

struct Welcome
{
	u32 ship = 0; // ShipId of the client's ship
	u64 tick = 0;
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
};

constexpr u32 MAX_SNAPSHOT_SHIPS = 32; // until snapshots go by interest (ADR 0003)

struct Snapshot
{
	u64 tick = 0;
	u32 count = 0;
	ShipState ships[MAX_SNAPSHOT_SHIPS];

	const ShipState* Find(u32 id) const;
};

// A ship's handle as one number, for the wire.
u32 ShipId(ShipHandle ship);
ShipHandle ShipFromId(u32 id);

std::vector<u8> Write(const Hello& message);
std::vector<u8> Write(const Welcome& message);
std::vector<u8> Write(const Input& message);
std::vector<u8> Write(const Snapshot& message);

// The type of a message's bytes; 0 when they are empty.
MessageType TypeOf(const u8* data, usize size);
// False when the bytes are not that message, or are cut short.
bool Read(const u8* data, usize size, Hello& message);
bool Read(const u8* data, usize size, Welcome& message);
bool Read(const u8* data, usize size, Input& message);
bool Read(const u8* data, usize size, Snapshot& message);

// The world's ships, up to MAX_SNAPSHOT_SHIPS.
void TakeSnapshot(const World& world, Snapshot& snapshot);
} // namespace sn::sim
