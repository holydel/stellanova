#include <sn/sim/protocol.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sn::sim
{
namespace
{
// Bytes on the wire.
constexpr u32 CONTROLS_BYTES = 1 + 1; // turn, thrust
// capacitor, cargo used, cargo mass, ammo, loaded, reloading, modules, flags
constexpr u32 OWN_BYTES = 4 + 4 + 4 + 4 + 2 + 4 + MAX_MODULES + MAX_MODULES;
// type, tick, wave, input, own, ships, shots
constexpr u32 SNAPSHOT_HEADER_BYTES = 1 + 8 + 4 + 4 + OWN_BYTES + 4 + 4;
// id, position, velocity, angle, controls, team, health, shield, capacitor,
// turret, beam
constexpr u32 SHIP_BYTES = 4 + 8 + 8 + 4 + CONTROLS_BYTES + 6;
constexpr u32 SHOT_BYTES = 8 + 8 + 1;
constexpr u32 ROCK_BYTES = 8 + 4 + 4;
constexpr u32 CRATE_BYTES = 4 + 8;
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

// A hull's numbers, in the order they travel.
template <typename Hull, typename Visit>
void ForEachHullValue(Hull& hull, Visit visit)
{
	for (auto* value :
	     {&hull.mass,           &hull.thrust,    &hull.thrustBack,  &hull.thrustTurn,
	      &hull.maxSpeed,       &hull.drag,      &hull.radius,      &hull.bounce,
	      &hull.health,         &hull.shield,    &hull.shieldRegen, &hull.shieldThreshold,
	      &hull.resist[0],      &hull.resist[1], &hull.resist[2],   &hull.resist[3],
	      &hull.reactor,        &hull.capacitor, &hull.scanner,     &hull.cargo,
	      &hull.cargoMassFactor})
		visit(*value);
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
	}

	void Put(const HullClass& hull)
	{
		ForEachHullValue(hull, [this](f32 value) { Put(value); });
	}

	// Its byte count (u16), then its bytes.
	void PutText(std::string_view text)
	{
		Put(ph::u16(text.size()));
		bytes.insert(bytes.end(), text.begin(), text.end());
	}

	// Its byte count (u32), then its bytes.
	void PutLongText(std::string_view text)
	{
		Put(u32(text.size()));
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
		ForEachHullValue(hull, [this](f32& value) { Get(value); });
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
		value.turn = FromSteps(turn);
		value.thrust = FromSteps(thrust);
	}

	// Text of at most `limit` bytes.
	void GetText(std::string& text, u32 limit)
	{
		ph::u16 length = 0;
		Get(length);
		Take(text, length, limit);
	}

	void GetLongText(std::string& text, u32 limit)
	{
		u32 length = 0;
		Get(length);
		Take(text, length, limit);
	}

	// A count of items of `bytes` each at least: false when more than
	// `limit`, or more than the message holds.
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

private:
	void Take(std::string& text, u32 length, u32 limit)
	{
		ok = ok && length <= limit && length <= size - at;
		if (!ok)
			return;
		text.assign(reinterpret_cast<const char*>(data + at), length);
		at += length;
	}
};

std::vector<u8> WriteText(MessageType type, std::string_view text)
{
	Writer writer(type);
	writer.PutLongText(text);
	return writer.bytes;
}

bool ReadText(const u8* data, usize size, MessageType type, std::string& text, u32 limit)
{
	Reader reader(data, size, type);
	reader.GetLongText(text, limit);
	return reader.Done();
}
} // namespace

ShipControls Quantize(ShipControls controls)
{
	return {FromSteps(ToSteps(controls.turn)), FromSteps(ToSteps(controls.thrust))};
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
	writer.Put(message.catalog);
	writer.Put(u8(message.joining));
	return writer.bytes;
}

std::vector<u8> Write(const Welcome& message)
{
	Writer writer(MessageType::Welcome);
	writer.Put(message.ship);
	writer.Put(message.tick);
	writer.Put(message.respawn);
	writer.Put(u8(message.kind));
	writer.Put(message.ring);
	writer.Put(message.hull);
	writer.Put(message.autopilot);
	writer.Put(u8(std::min<usize>(message.modules.size(), MAX_MODULES)));
	for (usize i = 0; i < std::min<usize>(message.modules.size(), MAX_MODULES); ++i)
		writer.PutText(message.modules[i]);
	writer.Put(u32(message.rocks.size()));
	for (const Rock& rock : message.rocks)
	{
		writer.Put(rock.position);
		writer.Put(rock.radius);
		writer.Put(rock.health);
	}
	writer.Put(u32(message.crates.size()));
	for (const CrateState& crate : message.crates)
	{
		writer.Put(crate.index);
		writer.Put(crate.position);
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
	writer.Put(message.target);
	return writer.bytes;
}

std::vector<u8> Write(const Snapshot& message)
{
	Writer writer(MessageType::Snapshot);
	writer.Put(message.tick);
	writer.Put(message.wave);
	writer.Put(message.input);
	const OwnState& own = message.own;
	writer.Put(own.capacitor);
	writer.Put(own.cargoUsed);
	writer.Put(own.cargoMass);
	writer.Put(own.ammo);
	writer.Put(own.loaded);
	writer.Put(own.reloading);
	for (const u8 module : own.modules)
		writer.Put(module);
	for (const u8 flags : own.flags)
		writer.Put(flags);
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
		writer.Put(ship.capacitor);
		writer.Put(ship.turret);
		writer.Put(ship.beam);
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
		writer.Put(event.index);
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

std::vector<u8> Write(const Login& message)
{
	Writer writer(MessageType::Login);
	writer.PutText(message.key);
	writer.PutText(message.name);
	writer.PutText(message.provider);
	writer.PutText(message.proof);
	writer.PutText(message.nonce);
	return writer.bytes;
}

std::vector<u8> Write(const Profile& message)
{
	return WriteText(MessageType::Profile, message.json);
}

std::vector<u8> Write(const Request& message)
{
	return WriteText(MessageType::Request, message.json);
}

std::vector<u8> Write(const Result& message)
{
	return WriteText(MessageType::Result, message.json);
}

std::vector<u8> Write(const Signed& message)
{
	Writer writer(MessageType::Signed);
	writer.PutText(message.key);
	writer.PutText(message.provider);
	writer.PutText(message.error);
	writer.Put(u32(message.offers.size()));
	for (const SignInOffer& offer : message.offers)
	{
		writer.PutText(offer.provider);
		writer.PutText(offer.url);
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
	static constexpr const char* NAMES[MessageCounts::TYPES] = {
		"other", "hello",   "welcome", "input",   "snapshot", "events", "name",  "say",
		"chat",  "refusal", "login",   "profile", "request",  "result", "signed"};
	return index < MessageCounts::TYPES ? NAMES[index] : NAMES[0];
}

bool Read(const u8* data, usize size, Hello& message)
{
	Reader reader(data, size, MessageType::Hello);
	reader.Get(message.version);
	// An older client stops here: its version says enough.
	if (reader.ok && reader.at == size)
		return true;
	reader.Get(message.catalog);
	u8 joining = 0;
	reader.Get(joining);
	message.joining = Joining(joining);
	return reader.Done() && joining <= u8(Joining::Hub);
}

bool Read(const u8* data, usize size, Welcome& message)
{
	Reader reader(data, size, MessageType::Welcome);
	reader.Get(message.ship);
	reader.Get(message.tick);
	reader.Get(message.respawn);
	u8 kind = 0;
	reader.Get(kind);
	message.kind = MatchKind(kind);
	reader.Get(message.ring);
	reader.Get(message.hull);
	reader.Get(message.autopilot);
	u8 modules = 0;
	reader.Get(modules);
	if (!reader.ok || kind > u8(MatchKind::Battle) || modules > MAX_MODULES)
		return false;
	message.modules.resize(modules);
	for (std::string& module : message.modules)
		reader.GetText(module, MAX_NAME_BYTES);
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
	if (!reader.Count(count, MAX_CRATES, CRATE_BYTES))
		return false;
	message.crates.resize(count);
	for (CrateState& crate : message.crates)
	{
		reader.Get(crate.index);
		reader.Get(crate.position);
		reader.ok = reader.ok && crate.index < MAX_CRATES;
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
	reader.Get(message.target);
	return reader.Done();
}

bool Read(const u8* data, usize size, Snapshot& message)
{
	Reader reader(data, size, MessageType::Snapshot);
	reader.Get(message.tick);
	reader.Get(message.wave);
	reader.Get(message.input);
	OwnState& own = message.own;
	reader.Get(own.capacitor);
	reader.Get(own.cargoUsed);
	reader.Get(own.cargoMass);
	reader.Get(own.ammo);
	reader.Get(own.loaded);
	reader.Get(own.reloading);
	for (u8& module : own.modules)
		reader.Get(module);
	for (u8& flags : own.flags)
		reader.Get(flags);
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
		reader.Get(ship.capacitor);
		reader.Get(ship.turret);
		reader.Get(ship.beam);
	}
	for (u32 i = 0; i < message.count; ++i)
	{
		if (message.ships[i].beam != NO_BEAM && message.ships[i].beam >= message.count)
			return false;
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
		reader.Get(event.index);
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

bool Read(const u8* data, usize size, Login& message)
{
	Reader reader(data, size, MessageType::Login);
	reader.GetText(message.key, KEY_BYTES);
	reader.GetText(message.name, MAX_NAME_BYTES);
	reader.GetText(message.provider, MAX_PROVIDER_BYTES);
	reader.GetText(message.proof, MAX_PROOF_BYTES);
	reader.GetText(message.nonce, MAX_NONCE_BYTES);
	return reader.Done();
}

bool Read(const u8* data, usize size, Profile& message)
{
	return ReadText(data, size, MessageType::Profile, message.json, MAX_PROFILE_BYTES);
}

bool Read(const u8* data, usize size, Request& message)
{
	return ReadText(data, size, MessageType::Request, message.json, MAX_REQUEST_BYTES);
}

bool Read(const u8* data, usize size, Result& message)
{
	return ReadText(data, size, MessageType::Result, message.json, MAX_PROFILE_BYTES);
}

bool Read(const u8* data, usize size, Signed& message)
{
	Reader reader(data, size, MessageType::Signed);
	reader.GetText(message.key, KEY_BYTES);
	reader.GetText(message.provider, MAX_PROVIDER_BYTES);
	reader.GetText(message.error, MAX_NAME_BYTES);
	u32 count = 0;
	if (!reader.Count(count, MAX_OFFERS, 4))
		return false;
	message.offers.resize(count);
	for (SignInOffer& offer : message.offers)
	{
		reader.GetText(offer.provider, MAX_PROVIDER_BYTES);
		reader.GetText(offer.url, MAX_OFFER_BYTES);
	}
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

u8 AngleToByte(f32 angle)
{
	const f32 turns = WrapAngle(angle) / (2.0f * ph::PI);
	return u8(ph::i32(std::lround(turns * 256.0f)) & 0xff);
}

f32 ByteToAngle(u8 turns) { return WrapAngle(f32(turns) * (2.0f * ph::PI / 256.0f)); }

void TakeSnapshot(const World& world, Snapshot& snapshot, ShipHandle own)
{
	using Handles = ph::HandleAllocator<ShipTag, MAX_SHIPS>;
	const Ship* self = GetShip(world, own);
	const Vec2 center = self ? self->position : Vec2{};
	const auto nearer = [center](Vec2 a, Vec2 b)
	{ return Dot(a - center, a - center) < Dot(b - center, b - center); };

	snapshot.tick = world.tick;
	snapshot.count = 0;
	snapshot.own = {};
	if (self)
	{
		OwnState& state = snapshot.own;
		state.capacitor = self->capacitor;
		state.cargoUsed = self->cargoUsed;
		state.cargoMass = self->cargoMass;
		state.ammo = self->ammo;
		bool gun = false;
		for (u32 i = 0; i < self->moduleCount; ++i)
		{
			const Module& module = self->modules[i];
			state.modules[i] = Share(module.health, module.type.health);
			state.flags[i] =
				u8((module.working ? MODULE_WORKING : 0) | (module.offline ? MODULE_OFFLINE : 0));
			if (module.type.kind == ModuleKind::Plasma && !gun)
			{
				gun = true;
				state.loaded = ph::u16(std::min(module.loaded, 0xffffu));
				state.reloading = module.reloading;
			}
		}
	}
	// Each included ship's slot, to find beams' targets among them.
	u32 included[MAX_SNAPSHOT_SHIPS];
	const auto add = [&](u32 slot)
	{
		const Ship& ship = world.ships[slot];
		included[snapshot.count] = slot;
		ShipState& state = snapshot.ships[snapshot.count++];
		state.id = ShipId(world.shipIds[slot]);
		state.position = ship.position;
		state.velocity = ship.velocity;
		state.angle = ship.angle;
		state.controls = ship.controls;
		state.team = ship.team;
		state.health = Share(ship.health, ship.hull.health);
		state.shield = Share(ship.shield, ship.hull.shield);
		state.capacitor = Share(ship.capacitor, ship.hull.capacitor);
		state.turret = 0;
		state.beam = NO_BEAM;
		for (u32 i = 0; i < ship.moduleCount; ++i)
		{
			if (ship.modules[i].type.external)
			{
				state.turret = AngleToByte(ship.modules[i].angle);
				break;
			}
		}
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
	for (u32 i = 0; i < snapshot.count; ++i)
	{
		const Ship& ship = world.ships[included[i]];
		for (u32 m = 0; m < ship.moduleCount; ++m)
		{
			const Module& module = ship.modules[m];
			if (module.type.kind != ModuleKind::Laser || !module.working)
				continue;
			for (u32 j = 0; j < snapshot.count; ++j)
			{
				if (world.shipIds[included[j]] == module.target)
					snapshot.ships[i].beam = u8(j);
			}
			break;
		}
	}

	const u32 room =
		(MAX_SNAPSHOT_BYTES - SNAPSHOT_HEADER_BYTES - snapshot.count * SHIP_BYTES) / SHOT_BYTES;
	const u32 shotLimit = std::min(room, MAX_SNAPSHOT_SHOTS);
	u32 shots[MAX_SHOTS];
	u32 shotTotal = 0;
	for (u32 i = 0; i < MAX_SHOTS; ++i)
	{
		if (world.shots[i].life > 0.0f)
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
		state.index = event.index;
		state.position = event.position;
		state.strength = event.strength;
	}
}
} // namespace sn::sim
