#include <sn/server/server.h>

#include <ph/core/log.h>
#include <ph/core/profile.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace sn::server
{
using namespace ph;

static_assert(sim::MAX_SNAPSHOT_BYTES <= net::MAX_UNRELIABLE_BYTES);

namespace
{
// The bots' fighters, weaker and slower than a player's ship: placeholders
// until blueprints (docs/vision.md).
sim::HullClass BotHull()
{
	sim::HullClass hull;
	hull.acceleration = 13.0f;
	hull.maxSpeed = 16.0f; // a player's ship flies 40
	hull.turnRate = 2.6f;
	hull.health = 4.0f;
	hull.shield = 3.0f;
	hull.shieldRegen = 1.0f;
	hull.shieldDelay = 3.0f;
	return hull;
}

sim::WeaponClass BotWeapon()
{
	sim::WeaponClass weapon;
	weapon.interval = 0.6f;
	weapon.speed = 35.0f; // slow enough to dodge
	weapon.life = 2.4f;   // as far as faster shots go: 84 m
	return weapon;
}

// Bots aim better wave after wave.
bots::PilotSkill SkillFor(u32 wave)
{
	bots::PilotSkill skill;
	skill.aimError = std::max(0.03f, 0.12f - 0.015f * f32(std::max(wave, 1u) - 1));
	return skill;
}

constexpr f32 CHAT_BURST = 5.0f; // lines a player may say at once
constexpr f32 CHAT_RATE = 1.0f;  // more lines a second
constexpr u32 DEFAULT_BOTS = 3;  // /add_bots without a count
// Newcomers: the time to say Hello, and a refused one's for its Refusal to go out.
constexpr f32 HELLO_SECONDS = 10.0f;
constexpr f32 REFUSED_SECONDS = 1.0f;
// A player's controls kept before they are applied, one set a tick; more
// than MAX_LEAD waiting (a burst after a stall) and the oldest go, so that
// its ship never lags its controls by long.
constexpr u32 MAX_CLIENT_MESSAGE = 2048; // bytes: the largest, a chat line, is about 200
constexpr u32 MAX_QUEUED = 8;
constexpr u32 MAX_LEAD = 3;
// Controls that stop coming (a page in the background, a stalled link) let
// go of the stick after this many ticks, rather than flying on.
constexpr u32 IDLE_TICKS = 15;

// Names clash without regard to the case of Latin letters.
bool SameName(std::string_view a, std::string_view b)
{
	return a.size() == b.size() &&
	       std::equal(a.begin(), a.end(), b.begin(),
	                  [](char x, char y) { return std::tolower(u8(x)) == std::tolower(u8(y)); });
}

// "/add_bots 3": the command's name ("add_bots") and its argument ("3").
void SplitCommand(std::string_view line, std::string_view& name, std::string_view& argument)
{
	line.remove_prefix(1);
	const usize space = line.find(' ');
	name = line.substr(0, space);
	argument = space == std::string_view::npos ? std::string_view() : line.substr(space + 1);
	while (!argument.empty() && argument.front() == ' ')
		argument.remove_prefix(1);
}

// A count of 1 to 999; 0 for anything else.
u32 ParseCount(std::string_view text)
{
	u32 count = 0;
	for (const char c : text)
	{
		if (c < '0' || c > '9' || count > 99)
			return 0;
		count = count * 10 + u32(c - '0');
	}
	return count;
}
} // namespace

bool Server::Start(const char* address, const MatchDesc& desc)
{
	Stop();
	listener = net::Listen(address);
	if (!listener)
		return false;
	// Clients send controls, names and chat lines: a few hundred bytes. What
	// a peer can make the server hold follows this (pith ADR 0036).
	net::ServerLimits limits;
	limits.maxIncomingReliableBytes = MAX_CLIENT_MESSAGE;
	net::SetLimits(listener, limits);
	match = desc;
	started = desc;
	newcomers.clear();
	world = std::make_unique<sim::World>();
	sim::MakeAsteroidField(*world, match.field);
	wave = 0;
	untilWave = match.waveDelay;
	seed = match.seed;
	sinceTick = 0.0f;
	sent = {};
	received = {};
	PH_LOG_INFO("server: listening at %s; %u rocks%s", address, world->rockCount,
	            match.waves ? "; waves" : "");
	return true;
}

void Server::Stop()
{
	if (!listener)
		return;
	net::Close(listener);
	listener = {};
	world.reset();
	players.clear();
	newcomers.clear();
	enemies.clear();
	wave = 0;
}

void Server::Update(f32 dt)
{
	if (!listener)
		return;
	net::Event event;
	while (net::Poll(listener, event))
	{
		if (event.type == net::EventType::Connected)
			newcomers.push_back({event.peer});
		else if (event.type == net::EventType::Message)
			OnMessage(event.peer, event.data, event.size);
		else if (event.type == net::EventType::Disconnected)
		{
			std::erase_if(newcomers, [&event](const Newcomer& n) { return n.peer == event.peer; });
			Leave(event.peer);
		}
	}
	Admit(dt);
	// Newcomers, once their Name had its chance (it follows Hello), and the
	// chat's allowance.
	for (Player& player : players)
	{
		player.chatLines = std::min(CHAT_BURST, player.chatLines + std::min(dt, 1.0f) * CHAT_RATE);
		if (player.announced)
			continue;
		player.announced = true;
		PH_LOG_INFO("server: player %u is %s", player.peer, player.name.c_str());
		NoticeAll("chat.joined", player.name);
	}
	if (players.empty() && match.resetWhenEmpty)
	{
		sinceTick = 0.0f;
		return; // a fresh match, waiting for someone
	}
	// A long stall (a breakpoint, the app in the background) skips time
	// rather than running hundreds of ticks.
	sinceTick += std::min(dt, 0.25f);
	while (sinceTick >= sim::TICK_SECONDS)
	{
		sinceTick -= sim::TICK_SECONDS;
		ApplyControls();
		{
			PH_PROFILE_SCOPE("Server.Bots");
			for (Enemy& enemy : enemies)
				sim::SetControls(*world, enemy.ship,
				                 bots::Fly(*world, enemy.ship, enemy.pilot, skill));
			if (match.autopilot)
			{
				bots::PilotSkill ace;
				ace.aimError = 0.02f;
				for (Player& player : players)
					sim::SetControls(*world, player.ship,
					                 bots::Fly(*world, player.ship, player.pilot, ace));
			}
		}
		{
			PH_PROFILE_SCOPE("Sim.Step");
			sim::Step(*world);
		}
		Referee();
		PH_PROFILE_SCOPE("Server.Send");
		SendEvents();
		SendSnapshots();
	}
}

void Server::OnMessage(net::PeerId peer, const u8* data, u32 size)
{
	const auto player = std::find_if(players.begin(), players.end(),
	                                 [peer](const Player& p) { return p.peer == peer; });
	received.Add(data, size);
	switch (sim::TypeOf(data, size))
	{
		case sim::MessageType::Hello: OnHello(peer, data, size); return;
		case sim::MessageType::Input:
		{
			sim::Input input;
			if (player == players.end() || !sim::Read(data, size, input))
				return;
			// Each number once, in order: a message repeats the newest few.
			const u32 first = input.last - (input.count - 1);
			for (u32 i = 0; i < input.count; ++i)
			{
				if (first + i <= player->received)
					continue;
				player->queued.push_back({first + i, input.controls[i]});
				player->received = first + i;
			}
			if (player->queued.size() > MAX_QUEUED)
				player->queued.erase(player->queued.begin(), player->queued.end() - MAX_QUEUED);
			return;
		}
		case sim::MessageType::Name:
		{
			sim::Name name;
			if (player != players.end() && sim::Read(data, size, name))
				OnName(*player, name.name);
			return;
		}
		case sim::MessageType::Say:
		{
			sim::Say say;
			if (player != players.end() && sim::Read(data, size, say))
				OnSay(*player, say.text);
			return;
		}
		default: return; // what a server does not take
	}
}

void Server::OnHello(net::PeerId peer, const u8* data, u32 size)
{
	// Hello is reliable: a second one says nothing new.
	if (std::any_of(players.begin(), players.end(),
	                [peer](const Player& p) { return p.peer == peer; }))
		return;
	const auto newcomer = std::find_if(newcomers.begin(), newcomers.end(),
	                                   [peer](const Newcomer& n) { return n.peer == peer; });
	if (newcomer != newcomers.end() && newcomer->refused)
		return;
	sim::Hello hello;
	if (!sim::Read(data, size, hello))
	{
		if (newcomer != newcomers.end())
			newcomers.erase(newcomer);
		net::Disconnect(listener, peer);
		return;
	}
	if (hello.version != sim::PROTOCOL_VERSION)
	{
		PH_LOG_INFO("server: peer %u speaks protocol %u, not %u", peer, hello.version,
		            sim::PROTOCOL_VERSION);
		Refuse(peer, sim::RefusalReason::Version);
		return;
	}
	if (players.size() >= match.maxPlayers)
	{
		PH_LOG_INFO("server: peer %u turned away: %u players already", peer, u32(players.size()));
		Refuse(peer, sim::RefusalReason::Full);
		return;
	}
	// Side by side, 10 m apart, clear of rocks.
	sim::Ship ship;
	ship.position = FindRoom({10.0f * f32(players.size()), 0.0f}, ship.hull.radius + 1.0f);
	const sim::ShipHandle handle = sim::SpawnShip(*world, ship);
	if (!handle)
	{
		Refuse(peer, sim::RefusalReason::Full);
		return;
	}
	if (newcomer != newcomers.end())
		newcomers.erase(newcomer);
	// "Pilot 1", or the lowest number nobody has.
	std::string name;
	for (u32 n = 1; name.empty(); ++n)
	{
		const std::string candidate = "Pilot " + std::to_string(n);
		if (UniqueName(candidate, nullptr) == candidate)
			name = candidate;
	}
	Player& added = players.emplace_back();
	added.peer = peer;
	added.ship = handle;
	added.home = ship.position;
	added.name = name;
	sim::Welcome message;
	message.ship = sim::ShipId(handle);
	message.tick = world->tick;
	message.respawn = match.respawn;
	message.hull = ship.hull;
	message.weapon = ship.weapon;
	message.autopilot = match.autopilot;
	message.rocks.assign(world->rocks, world->rocks + world->rockCount);
	Send(peer, sim::Write(message), net::Delivery::Reliable);
	PH_LOG_INFO("server: player %u joined", peer);
}

void Server::OnName(Player& player, std::string_view wanted)
{
	const std::string clean = sim::CleanText(wanted, sim::MAX_NAME_BYTES);
	if (clean.empty() || clean == player.name)
		return;
	const std::string name = UniqueName(clean, &player);
	if (name == player.name)
		return;
	// After "joined" went out, a new name is told, and costs a chat line.
	if (player.announced)
	{
		if (player.chatLines < 1.0f)
			return;
		player.chatLines -= 1.0f;
	}
	PH_LOG_INFO("server: player %u is now %s", player.peer, name.c_str());
	const std::string old = player.name;
	player.name = name;
	if (player.announced)
		NoticeAll("chat.renamed", old, name);
}

std::string Server::UniqueName(std::string_view wanted, const Player* self) const
{
	const auto taken = [&](std::string_view name)
	{
		return std::any_of(players.begin(), players.end(), [&](const Player& other)
		                   { return &other != self && SameName(other.name, name); });
	};
	if (!taken(wanted))
		return std::string(wanted);
	std::string name;
	for (u32 n = 2; n < 100; ++n)
	{
		const std::string suffix = " " + std::to_string(n);
		name = sim::CleanText(wanted, sim::MAX_NAME_BYTES - u32(suffix.size())) + suffix;
		if (!taken(name))
			break;
	}
	return name;
}

void Server::Refuse(net::PeerId peer, sim::RefusalReason reason)
{
	sim::Refusal refusal;
	refusal.reason = reason;
	Send(peer, sim::Write(refusal), net::Delivery::Reliable);
	const auto newcomer = std::find_if(newcomers.begin(), newcomers.end(),
	                                   [peer](const Newcomer& n) { return n.peer == peer; });
	if (newcomer == newcomers.end())
		newcomers.push_back({peer, 0.0f, true});
	else
	{
		newcomer->refused = true;
		newcomer->age = 0.0f;
	}
}

void Server::Admit(f32 dt)
{
	for (auto newcomer = newcomers.begin(); newcomer != newcomers.end();)
	{
		newcomer->age += std::min(dt, 1.0f);
		if (newcomer->age < (newcomer->refused ? REFUSED_SECONDS : HELLO_SECONDS))
		{
			++newcomer;
			continue;
		}
		if (!newcomer->refused)
			PH_LOG_INFO("server: peer %u said no Hello in %.0f s", newcomer->peer,
			            f64(HELLO_SECONDS));
		net::Disconnect(listener, newcomer->peer);
		newcomer = newcomers.erase(newcomer);
	}
}

void Server::ApplyControls()
{
	for (Player& player : players)
	{
		if (player.queued.empty())
		{
			// The last controls hold, for a while.
			if (++player.idle == IDLE_TICKS && !match.autopilot)
				sim::SetControls(*world, player.ship, {});
			continue;
		}
		player.idle = 0;
		if (player.queued.size() > MAX_LEAD)
			player.queued.erase(player.queued.begin(), player.queued.end() - MAX_LEAD);
		const Controls next = player.queued.front();
		player.queued.erase(player.queued.begin());
		player.applied = next.number;
		if (!match.autopilot)
			sim::SetControls(*world, player.ship, next.controls);
	}
}

void Server::Leave(net::PeerId peer)
{
	const auto player = std::find_if(players.begin(), players.end(),
	                                 [peer](const Player& p) { return p.peer == peer; });
	if (player == players.end())
		return;
	sim::RemoveShip(*world, player->ship);
	const std::string name = player->name;
	const bool announced = player->announced;
	players.erase(player);
	PH_LOG_INFO("server: player %u left", peer);
	if (announced)
		NoticeAll("chat.left", name);
	if (players.empty() && match.resetWhenEmpty)
		Restart();
}

void Server::Restart()
{
	world = std::make_unique<sim::World>();
	sim::MakeAsteroidField(*world, match.field);
	enemies.clear();
	wave = 0;
	match.waves = started.waves;
	untilWave = match.waveDelay;
	seed = match.seed;
	PH_LOG_INFO("server: nobody left; the match starts over");
}

void Server::Referee()
{
	// A bot's wreck goes at once: this tick's events tell of its end.
	for (auto enemy = enemies.begin(); enemy != enemies.end();)
	{
		const sim::Ship* ship = sim::GetShip(*world, enemy->ship);
		if (ship && sim::IsAlive(*ship))
		{
			++enemy;
			continue;
		}
		sim::RemoveShip(*world, enemy->ship);
		enemy = enemies.erase(enemy);
		PH_LOG_INFO("server: tick %llu, a bot of wave %u is down",
		            static_cast<unsigned long long>(world->tick), wave);
	}
	// A player's wreck stays a while, then the ship is back home, whole.
	for (Player& player : players)
	{
		const sim::Ship* ship = sim::GetShip(*world, player.ship);
		if (!ship || sim::IsAlive(*ship))
		{
			player.down = 0.0f;
			continue;
		}
		if (player.down == 0.0f)
			PH_LOG_INFO("server: tick %llu, player %u is down",
			            static_cast<unsigned long long>(world->tick), player.peer);
		player.down += sim::TICK_SECONDS;
		if (player.down >= match.respawn)
		{
			sim::ReviveShip(*world, player.ship, FindRoom(player.home, ship->hull.radius + 1.0f),
			                0.0f);
			player.down = 0.0f;
			PH_LOG_INFO("server: player %u is back", player.peer);
		}
	}
	// The next wave once the last is gone, after a breath; none without
	// players.
	if (!match.waves || players.empty() || !enemies.empty())
		return;
	untilWave -= sim::TICK_SECONDS;
	if (untilWave <= 0.0f)
	{
		SendWave();
		untilWave = match.waveDelay;
	}
}

void Server::SendWave()
{
	++wave;
	skill = SkillFor(wave);
	const u32 count = std::min(match.firstWave + wave - 1, match.largestWave);
	// From the first player that flies.
	Vec2 center = {};
	for (const Player& player : players)
	{
		const sim::Ship* ship = sim::GetShip(*world, player.ship);
		if (ship && sim::IsAlive(*ship))
		{
			center = ship->position;
			break;
		}
	}
	AddBots(count, center, 110.0f, 130.0f);
	PH_LOG_INFO("server: wave %u, %u bots", wave, u32(enemies.size()));
}

u32 Server::AddBots(u32 count, Vec2 center, f32 near, f32 far)
{
	// Every ship in every snapshot.
	const u32 ships = u32(players.size() + enemies.size());
	count = std::min(count, ships < sim::MAX_SNAPSHOT_SHIPS ? sim::MAX_SNAPSHOT_SHIPS - ships : 0);
	if (wave == 0)
		skill = SkillFor(1);
	const f32 side = 2.0f * PI * Random();
	const sim::HullClass hull = BotHull();
	u32 added = 0;
	for (u32 i = 0; i < count; ++i)
	{
		const f32 angle = side + 0.18f * (f32(i) - 0.5f * f32(count - 1));
		const Vec2 at = FindRoom(center + sim::Forward(angle) * (near + (far - near) * Random()),
		                         hull.radius + 3.0f);
		sim::Ship ship;
		ship.position = at;
		ship.angle = sim::AngleOf(center - at);
		ship.team = sim::BOTS;
		ship.hull = hull;
		ship.weapon = BotWeapon();
		const sim::ShipHandle handle = sim::SpawnShip(*world, ship);
		if (!handle)
			break;
		Enemy enemy;
		enemy.ship = handle;
		enemy.pilot.random = u32(Random() * 16777216.0f) * 2u + 1u;
		enemy.pilot.roam = center;
		enemies.push_back(enemy);
		++added;
	}
	return added;
}

void Server::RemoveBots()
{
	for (const Enemy& enemy : enemies)
		sim::RemoveShip(*world, enemy.ship);
	enemies.clear();
	untilWave = match.waveDelay;
}

void Server::OnSay(Player& player, std::string_view text)
{
	const std::string line = sim::CleanText(text, sim::MAX_CHAT_BYTES);
	if (line.empty())
		return;
	if (player.chatLines < 1.0f)
	{
		Notice(player.peer, "chat.too_fast");
		return;
	}
	player.chatLines -= 1.0f;
	if (line[0] == '/')
	{
		PH_LOG_INFO("server: %s: %s", player.name.c_str(), line.c_str());
		Command(player, line);
		return;
	}
	PH_LOG_INFO("chat: %s: %s", player.name.c_str(), line.c_str());
	sim::Chat chat;
	chat.ship = sim::ShipId(player.ship);
	chat.name = player.name;
	chat.text = line;
	Broadcast(chat);
}

void Server::Command(Player& player, std::string_view line)
{
	std::string_view name;
	std::string_view argument;
	SplitCommand(line, name, argument);
	if (name == "help")
		Notice(player.peer, "chat.help");
	else if (name == "add_bots")
	{
		const u32 count = argument.empty() ? DEFAULT_BOTS : ParseCount(argument);
		if (count == 0)
		{
			Notice(player.peer, "chat.count");
			return;
		}
		const sim::Ship* ship = sim::GetShip(*world, player.ship);
		const u32 added = AddBots(count, ship ? ship->position : player.home, 80.0f, 100.0f);
		if (added == 0)
			Notice(player.peer, "chat.full", std::to_string(sim::MAX_SNAPSHOT_SHIPS));
		else
			NoticeAll("chat.bots_added", player.name, std::to_string(added));
	}
	else if (name == "remove_bots")
	{
		RemoveBots();
		NoticeAll("chat.bots_removed", player.name);
	}
	else if (name == "waves" && (argument == "on" || argument == "off"))
	{
		match.waves = argument == "on";
		untilWave = match.waveDelay;
		NoticeAll(match.waves ? "chat.waves_on" : "chat.waves_off", player.name);
	}
	else if (name == "waves")
		Notice(player.peer, "chat.waves_usage");
	else if (name == "who")
	{
		std::string names;
		for (const Player& other : players)
			names += (names.empty() ? "" : ", ") + other.name;
		Notice(player.peer, "chat.who", {}, sim::CleanText(names, sim::MAX_CHAT_BYTES));
	}
	else
		Notice(player.peer, "chat.unknown",
		       sim::CleanText("/" + std::string(name), sim::MAX_NAME_BYTES));
}

void Server::Notice(net::PeerId peer, const char* key, std::string_view name,
                    std::string_view extra)
{
	sim::Chat chat;
	chat.notice = true;
	chat.text = key;
	chat.name = name;
	chat.extra = extra;
	Send(peer, sim::Write(chat), net::Delivery::Reliable);
}

void Server::NoticeAll(const char* key, std::string_view name, std::string_view extra)
{
	sim::Chat chat;
	chat.notice = true;
	chat.text = key;
	chat.name = name;
	chat.extra = extra;
	Broadcast(chat);
}

void Server::Broadcast(const sim::Chat& chat)
{
	const std::vector<u8> bytes = sim::Write(chat);
	for (const Player& player : players)
		Send(player.peer, bytes, net::Delivery::Reliable);
}

Vec2 Server::FindRoom(Vec2 near, f32 radius) const
{
	// Out along a spiral until no rock is in the way.
	for (u32 k = 0; k < 64; ++k)
	{
		const Vec2 at = near + sim::Forward(2.4f * f32(k)) * (2.0f * f32(k));
		bool clear = true;
		for (u32 r = 0; r < world->rockCount && clear; ++r)
		{
			const sim::Rock& rock = world->rocks[r];
			const f32 reach = rock.radius + radius;
			clear = rock.health <= 0.0f ||
			        sim::Dot(at - rock.position, at - rock.position) >= reach * reach;
		}
		if (clear)
			return at;
	}
	return near;
}

f32 Server::Random()
{
	seed = seed * 1664525u + 1013904223u;
	return f32(seed >> 8) / f32(1u << 24);
}

void Server::SendEvents()
{
	if (players.empty() || world->eventCount == 0)
		return;
	sim::TakeEvents(*world, events);
	const std::vector<u8> bytes = sim::Write(events);
	for (const Player& player : players)
		Send(player.peer, bytes, net::Delivery::Reliable);
}

void Server::SendSnapshots()
{
	// Each player's own: its ship first, the nearest after it, and what its
	// prediction needs.
	for (const Player& player : players)
	{
		sim::TakeSnapshot(*world, snapshot, player.ship, match.autopilot);
		snapshot.wave = wave;
		snapshot.input = player.applied;
		const sim::Ship* ship = sim::GetShip(*world, player.ship);
		snapshot.cooldown = ship ? ship->cooldown : 0.0f;
		Send(player.peer, sim::Write(snapshot), net::Delivery::Unreliable);
	}
}

void Server::Send(net::PeerId peer, const std::vector<u8>& bytes, net::Delivery delivery)
{
	if (net::Send(listener, peer, bytes.data(), u32(bytes.size()), delivery))
		sent.Add(bytes.data(), bytes.size());
}
} // namespace sn::server
