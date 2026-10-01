#include <sn/sim/protocol.h>

#include <cstring>

namespace sn::sim
{
namespace
{
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

	void Put(Vec2 value)
	{
		Put(value.x);
		Put(value.y);
	}

	void Put(ShipControls value)
	{
		Put(value.turn);
		Put(value.thrust);
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

	void Get(Vec2& value)
	{
		Get(value.x);
		Get(value.y);
	}

	void Get(ShipControls& value)
	{
		Get(value.turn);
		Get(value.thrust);
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
	writer.Put(message.count);
	for (u32 i = 0; i < message.count; ++i)
	{
		const ShipState& ship = message.ships[i];
		writer.Put(ship.id);
		writer.Put(ship.position);
		writer.Put(ship.velocity);
		writer.Put(ship.angle);
		writer.Put(ship.controls);
	}
	return writer.bytes;
}

MessageType TypeOf(const u8* data, usize size)
{
	return data && size ? MessageType(data[0]) : MessageType(0);
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
	reader.Get(message.count);
	if (message.count > MAX_SNAPSHOT_SHIPS)
		return false;
	for (u32 i = 0; i < message.count; ++i)
	{
		ShipState& ship = message.ships[i];
		reader.Get(ship.id);
		reader.Get(ship.position);
		reader.Get(ship.velocity);
		reader.Get(ship.angle);
		reader.Get(ship.controls);
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
	}
}
} // namespace sn::sim
