#include <sn/sim/protocol.h>

#include <algorithm>
#include <cstring>

namespace sn::sim
{
namespace
{
// Bytes on the wire.
constexpr u32 SNAPSHOT_HEADER_BYTES = 1 + 8 + 4 + 4 + 4; // type, tick, wave, ships, shots
// id, position, velocity, angle, controls, team, health, shield
constexpr u32 SHIP_BYTES = 4 + 8 + 8 + 4 + 9 + 3;
constexpr u32 SHOT_BYTES = 8 + 8 + 1;
constexpr u32 ROCK_BYTES = 8 + 4 + 4;
constexpr u32 EVENT_BYTES = 1 + 4 + 4 + 4 + 8 + 4;
static_assert(SNAPSHOT_HEADER_BYTES + MAX_SNAPSHOT_SHIPS * SHIP_BYTES <= MAX_SNAPSHOT_BYTES);

// Every target pith runs on is little-endian, so fields go as they are.
class Writer
{
public:
	explicit Writer(MessageType type) { Put(u8(type)); }

	template <typename T>
	void Put(T value)
	{
		const auto* begin = reinterpret_cast<const u8*>(&value);
		bytes.insert(bytes.end(), begin, begin + sizeof(T));
	}

	void Put(bool value) { Put(u8(value ? 1 : 0)); }

	void Put(Vec2 value)
	{
		Put(value.x);
		Put(value.y);
	}

	void Put(ShipControls value)
	{
		Put(value.turn);
		Put(value.thrust);
		Put(value.fire);
	}

	std::vector<u8> bytes;
};

class Reader
{
public:
	Reader(const u8* data, usize size, MessageType type)
		: data(data), size(size), ok(TypeOf(data, size) == type), at(1)
	{
	}

	template <typename T>
	void Get(T& value)
	{
		if (!ok || at + sizeof(T) > size)
		{
			ok = false;
			return;
		}
		std::memcpy(&value, data + at, sizeof(T));
		at += sizeof(T);
	}

	void Get(bool& value)
	{
		u8 byte = 0;
		Get(byte);
		ok = ok && byte <= 1;
		value = byte != 0;
	}

	void Get(Vec2& value)
	{
		Get(value.x);
		Get(value.y);
	}

	void Get(ShipControls& value)
	{
		Get(value.turn);
		Get(value.thrust);
		Get(value.fire);
	}

	// A count of items of `bytes` each: false when more than `limit`, or more
	// than the message holds.
	bool Count(u32& count, u32 limit, u32 bytes)
	{
		Get(count);
		ok = ok && count <= limit && usize(count) * bytes <= size - at;
		return ok;
	}

	// Every byte read, none left over.
	bool Done() const { return ok && at == size; }

	const u8* data;
	usize size;
	bool ok;
	usize at;
};
} // namespace

u32 ShipId(ShipHandle ship) { return (ship.generation << 16) | (ship.index & 0xffffu); }

ShipHandle ShipFromId(u32 id) { return {id & 0xffffu, id >> 16}; }

const ShipState* Snapshot::Find(u32 id) const
{
	for (u32 i = 0; i < count; ++i)
	{
		if (ships[i].id == id)
			return &ships[i];
	}
	return nullptr;
}

std::vector<u8> Write(const Hello& message)
{
	Writer writer(MessageType::Hello);
	writer.Put(message.version);
	return writer.bytes;
}

std::vector<u8> Write(const Welcome& message)
{
	Writer writer(MessageType::Welcome);
	writer.Put(message.ship);
	writer.Put(message.tick);
	writer.Put(message.respawn);
	writer.Put(u32(message.rocks.size()));
	for (const Rock& rock : message.rocks)
	{
		writer.Put(rock.position);
		writer.Put(rock.radius);
		writer.Put(rock.health);
	}
	return writer.bytes;
}

std::vector<u8> Write(const Input& message)
{
	Writer writer(MessageType::Input);
	writer.Put(message.tick);
	writer.Put(message.controls);
	return writer.bytes;
}

std::vector<u8> Write(const Snapshot& message)
{
	Writer writer(MessageType::Snapshot);
	writer.Put(message.tick);
	writer.Put(message.wave);
	writer.Put(message.count);
	for (u32 i = 0; i < message.count; ++i)
	{
		const ShipState& ship = message.ships[i];
		writer.Put(ship.id);
		writer.Put(ship.position);
		writer.Put(ship.velocity);
		writer.Put(ship.angle);
		writer.Put(ship.controls);
		writer.Put(ship.team);
		writer.Put(ship.health);
		writer.Put(ship.shield);
	}
	writer.Put(message.shotCount);
	for (u32 i = 0; i < message.shotCount; ++i)
	{
		writer.Put(message.shots[i].position);
		writer.Put(message.shots[i].velocity);
		writer.Put(message.shots[i].team);
	}
	return writer.bytes;
}

std::vector<u8> Write(const Events& message)
{
	Writer writer(MessageType::Events);
	writer.Put(message.tick);
	writer.Put(u32(message.events.size()));
	for (const EventState& event : message.events)
	{
		writer.Put(u8(event.type));
		writer.Put(event.ship);
		writer.Put(event.other);
		writer.Put(event.rock);
		writer.Put(event.position);
		writer.Put(event.strength);
	}
	return writer.bytes;
}

MessageType TypeOf(const u8* data, usize size)
{
	return data && size ? MessageType(data[0]) : MessageType(0);
}

void MessageCounts::Add(const u8* data, usize size)
{
	const u32 type = u32(TypeOf(data, size));
	const u32 index = type < TYPES ? type : 0;
	++count[index];
	bytes[index] += size;
}

const char* MessageName(u32 index)
{
	static constexpr const char* NAMES[MessageCounts::TYPES] = {"other", "hello",    "welcome",
	                                                            "input", "snapshot", "events"};
	return index < MessageCounts::TYPES ? NAMES[index] : NAMES[0];
}

bool Read(const u8* data, usize size, Hello& message)
{
	Reader reader(data, size, MessageType::Hello);
	reader.Get(message.version);
	return reader.Done();
}

bool Read(const u8* data, usize size, Welcome& message)
{
	Reader reader(data, size, MessageType::Welcome);
	reader.Get(message.ship);
	reader.Get(message.tick);
	reader.Get(message.respawn);
	u32 count = 0;
	if (!reader.Count(count, MAX_ROCKS, ROCK_BYTES))
		return false;
	message.rocks.resize(count);
	for (Rock& rock : message.rocks)
	{
		reader.Get(rock.position);
		reader.Get(rock.radius);
		reader.Get(rock.health);
	}
	return reader.Done();
}

bool Read(const u8* data, usize size, Input& message)
{
	Reader reader(data, size, MessageType::Input);
	reader.Get(message.tick);
	reader.Get(message.controls);
	return reader.Done();
}

bool Read(const u8* data, usize size, Snapshot& message)
{
	Reader reader(data, size, MessageType::Snapshot);
	reader.Get(message.tick);
	reader.Get(message.wave);
	if (!reader.Count(message.count, MAX_SNAPSHOT_SHIPS, SHIP_BYTES))
		return false;
	for (u32 i = 0; i < message.count; ++i)
	{
		ShipState& ship = message.ships[i];
		reader.Get(ship.id);
		reader.Get(ship.position);
		reader.Get(ship.velocity);
		reader.Get(ship.angle);
		reader.Get(ship.controls);
		reader.Get(ship.team);
		reader.Get(ship.health);
		reader.Get(ship.shield);
	}
	if (!reader.Count(message.shotCount, MAX_SNAPSHOT_SHOTS, SHOT_BYTES))
		return false;
	for (u32 i = 0; i < message.shotCount; ++i)
	{
		reader.Get(message.shots[i].position);
		reader.Get(message.shots[i].velocity);
		reader.Get(message.shots[i].team);
	}
	return reader.Done();
}

bool Read(const u8* data, usize size, Events& message)
{
	Reader reader(data, size, MessageType::Events);
	reader.Get(message.tick);
	u32 count = 0;
	if (!reader.Count(count, MAX_EVENTS, EVENT_BYTES))
		return false;
	message.events.resize(count);
	for (EventState& event : message.events)
	{
		u8 type = 0;
		reader.Get(type);
		event.type = EventType(type);
		reader.Get(event.ship);
		reader.Get(event.other);
		reader.Get(event.rock);
		reader.Get(event.position);
		reader.Get(event.strength);
		if (type > LAST_EVENT_TYPE)
			return false;
	}
	return reader.Done();
}

void TakeSnapshot(const World& world, Snapshot& snapshot)
{
	snapshot.tick = world.tick;
	snapshot.count = 0;
	for (u32 slot = 0; slot < MAX_SHIPS && snapshot.count < MAX_SNAPSHOT_SHIPS; ++slot)
	{
		if (!world.shipIds[slot])
			continue;
		const Ship& ship = world.ships[slot];
		ShipState& state = snapshot.ships[snapshot.count++];
		state.id = ShipId(world.shipIds[slot]);
		state.position = ship.position;
		state.velocity = ship.velocity;
		state.angle = ship.angle;
		state.controls = ship.controls;
		state.team = ship.team;
		state.health = Share(ship.health, ship.hull.health);
		state.shield = Share(ship.shield, ship.hull.shield);
	}
	const u32 room =
		(MAX_SNAPSHOT_BYTES - SNAPSHOT_HEADER_BYTES - snapshot.count * SHIP_BYTES) / SHOT_BYTES;
	const u32 shotLimit = std::min(room, MAX_SNAPSHOT_SHOTS);
	snapshot.shotCount = 0;
	for (const Shot& shot : world.shots)
	{
		if (snapshot.shotCount == shotLimit)
			break;
		if (shot.life > 0.0f)
			snapshot.shots[snapshot.shotCount++] = {shot.position, shot.velocity, shot.team};
	}
}

u8 Share(f32 value, f32 whole)
{
	if (value <= 0.0f || whole <= 0.0f)
		return 0;
	return u8(std::clamp(value / whole * 255.0f + 0.5f, 1.0f, 255.0f));
}

void TakeEvents(const World& world, Events& events)
{
	events.tick = world.tick;
	events.events.resize(world.eventCount);
	for (u32 i = 0; i < world.eventCount; ++i)
	{
		const Event& event = world.events[i];
		EventState& state = events.events[i];
		state.type = event.type;
		state.ship = event.ship ? ShipId(event.ship) : 0;
		state.other = event.other ? ShipId(event.other) : 0;
		state.rock = event.rock;
		state.position = event.position;
		state.strength = event.strength;
	}
}
} // namespace sn::sim
