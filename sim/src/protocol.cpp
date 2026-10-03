#include <sn/sim/protocol.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sn::sim
{
namespace
{
// Bytes on the wire.
constexpr u32 CONTROLS_BYTES = 1 + 1 + 1; // turn, thrust, fire
// type, tick, wave, input, cooldown, ships, shots
constexpr u32 SNAPSHOT_HEADER_BYTES = 1 + 8 + 4 + 4 + 4 + 4 + 4;
// id, position, velocity, angle, controls, team, health, shield
constexpr u32 SHIP_BYTES = 4 + 8 + 8 + 4 + CONTROLS_BYTES + 3;
constexpr u32 SHOT_BYTES = 8 + 8 + 1;
constexpr u32 ROCK_BYTES = 8 + 4 + 4;
constexpr u32 EVENT_BYTES = 1 + 4 + 4 + 4 + 8 + 4;
static_assert(SNAPSHOT_HEADER_BYTES + MAX_SNAPSHOT_SHIPS * SHIP_BYTES <= MAX_SNAPSHOT_BYTES);

// A control in [-1, 1] as 1/127 steps; NaN as 0.
ph::i8 ToSteps(f32 value)
{
	if (std::isnan(value))
		return 0;
	return ph::i8(std::lround(std::clamp(value, -1.0f, 1.0f) * 127.0f));
}

f32 FromSteps(ph::i8 steps) { return std::max(-1.0f, f32(steps) / 127.0f); }

// Characters that show nothing, or steer the direction of what follows:
// they would let two names look the same.
bool IsInvisible(u32 code)
{
	return code == 0xAD || code == 0x34F || code == 0x61C || code == 0x115F || code == 0x1160 ||
	       code == 0x17B4 || code == 0x17B5 || (code >= 0x180B && code <= 0x180F) ||
	       (code >= 0x200B && code <= 0x200F) || (code >= 0x2028 && code <= 0x202E) ||
	       (code >= 0x2060 && code <= 0x206F) || code == 0x3164 ||
	       (code >= 0xFE00 && code <= 0xFE0F) || code == 0xFEFF || code == 0xFFA0 ||
	       (code >= 0xFFF0 && code <= 0xFFFB) || (code >= 0xE0000 && code <= 0xE0FFF);
}

// Spaces of every width.
bool IsSpace(u32 code)
{
	return code == 0x20 || code == 0xA0 || code == 0x1680 || (code >= 0x2000 && code <= 0x200A) ||
	       code == 0x202F || code == 0x205F || code == 0x3000;
}

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
		Put(ToSteps(value.turn));
		Put(ToSteps(value.thrust));
		Put(value.fire);
	}

	void Put(const HullClass& hull)
	{
		for (const f32 value :
		     {hull.acceleration, hull.reverse, hull.maxSpeed, hull.turnRate, hull.drag, hull.radius,
		      hull.bounce, hull.health, hull.shield, hull.shieldRegen, hull.shieldDelay})
			Put(value);
	}

	void Put(const WeaponClass& weapon)
	{
		for (const f32 value :
		     {weapon.interval, weapon.speed, weapon.life, weapon.radius, weapon.damage})
			Put(value);
	}

	// Its byte count (u16), then its bytes.
	void PutText(std::string_view text)
	{
		Put(ph::u16(text.size()));
		bytes.insert(bytes.end(), text.begin(), text.end());
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

	// Floats from the wire are finite, or the message is refused.
	void Get(f32& value)
	{
		Get<f32>(value);
		ok = ok && std::isfinite(value);
	}

	void Get(bool& value)
	{
		u8 byte = 0;
		Get(byte);
		ok = ok && byte <= 1;
		value = byte != 0;
	}

	void Get(HullClass& hull)
	{
		for (f32* value : {&hull.acceleration, &hull.reverse, &hull.maxSpeed, &hull.turnRate,
		                   &hull.drag, &hull.radius, &hull.bounce, &hull.health, &hull.shield,
		                   &hull.shieldRegen, &hull.shieldDelay})
			Get(*value);
	}

	void Get(WeaponClass& weapon)
	{
		for (f32* value :
		     {&weapon.interval, &weapon.speed, &weapon.life, &weapon.radius, &weapon.damage})
			Get(*value);
	}

	void Get(Vec2& value)
	{
		Get(value.x);
		Get(value.y);
	}

	void Get(ShipControls& value)
	{
		ph::i8 turn = 0;
		ph::i8 thrust = 0;
		Get(turn);
		Get(thrust);
		Get(value.fire);
		value.turn = FromSteps(turn);
		value.thrust = FromSteps(thrust);
	}

	// Text of at most `limit` bytes.
	void GetText(std::string& text, u32 limit)
	{
		ph::u16 length = 0;
		Get(length);
		ok = ok && length <= limit && length <= size - at;
		if (!ok)
			return;
		text.assign(reinterpret_cast<const char*>(data + at), length);
		at += length;
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

ShipControls Quantize(ShipControls controls)
{
	return {FromSteps(ToSteps(controls.turn)), FromSteps(ToSteps(controls.thrust)), controls.fire};
}

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
	writer.Put(message.hull);
	writer.Put(message.weapon);
	writer.Put(message.autopilot);
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
	const u32 count = std::min(message.count, MAX_INPUTS);
	writer.Put(message.last);
	writer.Put(u8(count));
	for (u32 i = 0; i < count; ++i)
		writer.Put(message.controls[i]);
	return writer.bytes;
}

std::vector<u8> Write(const Snapshot& message)
{
	Writer writer(MessageType::Snapshot);
	writer.Put(message.tick);
	writer.Put(message.wave);
	writer.Put(message.input);
	writer.Put(message.cooldown);
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

std::vector<u8> Write(const Name& message)
{
	Writer writer(MessageType::Name);
	writer.PutText(message.name);
	return writer.bytes;
}

std::vector<u8> Write(const Say& message)
{
	Writer writer(MessageType::Say);
	writer.PutText(message.text);
	return writer.bytes;
}

std::vector<u8> Write(const Chat& message)
{
	Writer writer(MessageType::Chat);
	writer.Put(message.ship);
	writer.Put(message.notice);
	writer.PutText(message.name);
	writer.PutText(message.text);
	writer.PutText(message.extra);
	return writer.bytes;
}

std::vector<u8> Write(const Refusal& message)
{
	Writer writer(MessageType::Refusal);
	writer.Put(u8(message.reason));
	writer.Put(message.version);
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
	static constexpr const char* NAMES[MessageCounts::TYPES] = {
		"other",  "hello", "welcome", "input", "snapshot",
		"events", "name",  "say",     "chat",  "refusal"};
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
	reader.Get(message.hull);
	reader.Get(message.weapon);
	reader.Get(message.autopilot);
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
	reader.Get(message.last);
	u8 count = 0;
	reader.Get(count);
	// Numbers start at 1: the oldest of them must be one too.
	if (!reader.ok || count == 0 || count > MAX_INPUTS || message.last < count)
		return false;
	message.count = count;
	for (u32 i = 0; i < count; ++i)
		reader.Get(message.controls[i]);
	return reader.Done();
}

bool Read(const u8* data, usize size, Snapshot& message)
{
	Reader reader(data, size, MessageType::Snapshot);
	reader.Get(message.tick);
	reader.Get(message.wave);
	reader.Get(message.input);
	reader.Get(message.cooldown);
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

bool Read(const u8* data, usize size, Name& message)
{
	Reader reader(data, size, MessageType::Name);
	reader.GetText(message.name, MAX_NAME_BYTES);
	return reader.Done();
}

bool Read(const u8* data, usize size, Say& message)
{
	Reader reader(data, size, MessageType::Say);
	reader.GetText(message.text, MAX_CHAT_BYTES);
	return reader.Done();
}

bool Read(const u8* data, usize size, Chat& message)
{
	Reader reader(data, size, MessageType::Chat);
	reader.Get(message.ship);
	reader.Get(message.notice);
	reader.GetText(message.name, MAX_NAME_BYTES);
	reader.GetText(message.text, MAX_CHAT_BYTES);
	reader.GetText(message.extra, MAX_CHAT_BYTES);
	return reader.Done();
}

bool Read(const u8* data, usize size, Refusal& message)
{
	Reader reader(data, size, MessageType::Refusal);
	u8 reason = 0;
	reader.Get(reason);
	reader.Get(message.version);
	if (reason == 0 || reason > LAST_REFUSAL_REASON)
		return false;
	message.reason = RefusalReason(reason);
	return reader.Done();
}

std::string CleanText(std::string_view text, u32 maxBytes)
{
	std::string out;
	for (usize at = 0; at < text.size();)
	{
		// One UTF-8 sequence: its length from the first byte, then checked.
		const u8 lead = u8(text[at]);
		const usize length = lead < 0x80             ? 1
		                     : (lead & 0xE0) == 0xC0 ? 2
		                     : (lead & 0xF0) == 0xE0 ? 3
		                     : (lead & 0xF8) == 0xF0 ? 4
		                                             : 0;
		bool valid = length > 0 && at + length <= text.size();
		u32 code = length == 1 ? lead : lead & (0xFFu >> (length + 1));
		for (usize k = 1; valid && k < length; ++k)
		{
			const u8 next = u8(text[at + k]);
			valid = (next & 0xC0) == 0x80;
			code = code << 6 | (next & 0x3F);
		}
		const u32 least = length == 2 ? 0x80 : length == 3 ? 0x800 : length == 4 ? 0x10000 : 0;
		valid = valid && code >= least && code <= 0x10FFFF && (code < 0xD800 || code > 0xDFFF);
		const bool control = code < 0x20 || (code >= 0x7F && code < 0xA0);
		if (valid && !control && !IsInvisible(code))
		{
			if (!IsSpace(code))
			{
				if (out.size() + length > maxBytes)
					break;
				out.append(text.substr(at, length));
			}
			else if (!out.empty() && out.back() != ' ')
			{
				// One plain space between words: none first, none twice.
				if (out.size() + 1 > maxBytes)
					break;
				out.push_back(' ');
			}
		}
		at += valid ? length : 1;
	}
	if (!out.empty() && out.back() == ' ')
		out.pop_back();
	return out;
}

void TakeSnapshot(const World& world, Snapshot& snapshot, ShipHandle own, bool ownShots)
{
	using Handles = ph::HandleAllocator<ShipTag, MAX_SHIPS>;
	const Ship* self = GetShip(world, own);
	const Vec2 center = self ? self->position : Vec2{};
	const auto nearer = [center](Vec2 a, Vec2 b)
	{ return Dot(a - center, a - center) < Dot(b - center, b - center); };

	snapshot.tick = world.tick;
	snapshot.count = 0;
	const auto add = [&](u32 slot)
	{
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
	};
	u32 slots[MAX_SHIPS];
	u32 total = 0;
	for (u32 slot = 0; slot < MAX_SHIPS; ++slot)
	{
		if (world.shipIds[slot] && !(self && world.shipIds[slot] == own))
			slots[total++] = slot;
	}
	if (self)
		add(Handles::Slot(own));
	const u32 taken = std::min(total, MAX_SNAPSHOT_SHIPS - snapshot.count);
	if (self)
		std::partial_sort(slots, slots + taken, slots + total, [&](u32 a, u32 b)
		                  { return nearer(world.ships[a].position, world.ships[b].position); });
	for (u32 i = 0; i < taken; ++i)
		add(slots[i]);

	const u32 room =
		(MAX_SNAPSHOT_BYTES - SNAPSHOT_HEADER_BYTES - snapshot.count * SHIP_BYTES) / SHOT_BYTES;
	const u32 shotLimit = std::min(room, MAX_SNAPSHOT_SHOTS);
	u32 shots[MAX_SHOTS];
	u32 shotTotal = 0;
	for (u32 i = 0; i < MAX_SHOTS; ++i)
	{
		const Shot& shot = world.shots[i];
		if (shot.life > 0.0f && (!self || ownShots || shot.owner != own))
			shots[shotTotal++] = i;
	}
	const u32 shotTaken = std::min(shotTotal, shotLimit);
	if (self)
		std::partial_sort(shots, shots + shotTaken, shots + shotTotal, [&](u32 a, u32 b)
		                  { return nearer(world.shots[a].position, world.shots[b].position); });
	snapshot.shotCount = 0;
	for (u32 i = 0; i < shotTaken; ++i)
	{
		const Shot& shot = world.shots[shots[i]];
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
