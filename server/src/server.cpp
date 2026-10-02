#include <sn/server/server.h>

#include <ph/core/log.h>
#include <ph/core/profile.h>

#include <algorithm>
#include <cmath>

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
	skill.aimError = std::max(0.03f, 0.12f - 0.015f * f32(wave - 1));
	return skill;
}
} // namespace

bool Server::Start(const char* address, const MatchDesc& desc)
{
	Stop();
	listener = net::Listen(address);
	if (!listener)
		return false;
	match = desc;
	world = std::make_unique<sim::World>();
	sim::MakeAsteroidField(*world, match.field);
	wave = 0;
	untilWave = match.waveDelay;
	seed = match.seed;
	sinceTick = 0.0f;
	sent = {};
	received = {};
	PH_LOG_INFO("server: listening at %s; %u rocks%s", address, world->rockCount,
	            match.bots ? "; bots" : "");
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
		if (event.type == net::EventType::Message)
			OnMessage(event.peer, event.data, event.size);
		else if (event.type == net::EventType::Disconnected)
			Leave(event.peer);
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
		case sim::MessageType::Hello:
		{
			sim::Hello hello;
			if (!sim::Read(data, size, hello) || hello.version != sim::PROTOCOL_VERSION ||
			    player != players.end())
			{
				net::Disconnect(listener, peer);
				return;
			}
			// Side by side, 10 m apart.
			sim::Ship ship;
			ship.position = {10.0f * f32(players.size()), 0.0f};
			const sim::ShipHandle handle = sim::SpawnShip(*world, ship);
			if (!handle)
			{
				net::Disconnect(listener, peer);
				return;
			}
			players.push_back({peer, handle, ship.position, 0.0f, {}});
			sim::Welcome message{sim::ShipId(handle), world->tick, match.respawn, {}};
			message.rocks.assign(world->rocks, world->rocks + world->rockCount);
			const std::vector<u8> welcome = sim::Write(message);
			Send(peer, welcome, net::Delivery::Reliable);
			PH_LOG_INFO("server: player %u joined", peer);
			return;
		}
		case sim::MessageType::Input:
		{
			sim::Input input;
			if (player != players.end() && sim::Read(data, size, input) && !match.autopilot)
				sim::SetControls(*world, player->ship, input.controls);
			return;
		}
		default: return; // what a server does not take
	}
}

void Server::Leave(net::PeerId peer)
{
	const auto player = std::find_if(players.begin(), players.end(),
	                                 [peer](const Player& p) { return p.peer == peer; });
	if (player == players.end())
		return;
	sim::RemoveShip(*world, player->ship);
	players.erase(player);
	PH_LOG_INFO("server: player %u left", peer);
	if (players.empty() && match.resetWhenEmpty)
		Restart();
}

void Server::Restart()
{
	world = std::make_unique<sim::World>();
	sim::MakeAsteroidField(*world, match.field);
	enemies.clear();
	wave = 0;
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
	if (!match.bots || players.empty() || !enemies.empty())
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
	// Together, from one side, 110-130 m from the first player that flies.
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
	const f32 side = 2.0f * PI * Random();
	const sim::HullClass hull = BotHull();
	for (u32 i = 0; i < count; ++i)
	{
		const f32 angle = side + 0.18f * (f32(i) - 0.5f * f32(count - 1));
		const Vec2 at = FindRoom(center + sim::Forward(angle) * (110.0f + 20.0f * Random()),
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
	}
	PH_LOG_INFO("server: wave %u, %u bots", wave, u32(enemies.size()));
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
	if (players.empty())
		return;
	sim::TakeSnapshot(*world, snapshot);
	snapshot.wave = wave;
	const std::vector<u8> bytes = sim::Write(snapshot);
	for (const Player& player : players)
		Send(player.peer, bytes, net::Delivery::Unreliable);
}

void Server::Send(net::PeerId peer, const std::vector<u8>& bytes, net::Delivery delivery)
{
	if (net::Send(listener, peer, bytes.data(), u32(bytes.size()), delivery))
		sent.Add(bytes.data(), bytes.size());
}
} // namespace sn::server
