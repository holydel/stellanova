// The server's matches: the skirmish and battles (server.h).

#include <sn/server/server.h>
#include <sn/sim/catalog.h>

#include <ph/core/log.h>
#include <ph/core/profile.h>

#include <algorithm>
#include <cmath>

namespace sn::server
{
using namespace ph;

namespace
{
// A player's controls kept before they are applied, one set a tick; more
// than MAX_LEAD waiting (a burst after a stall) and the oldest go, so that
// its ship never lags its controls by long.
constexpr u32 MAX_LEAD = 3;
// Controls that stop coming (a page in the background, a stalled link) let
// go of the stick after this many ticks, rather than flying on.
constexpr u32 IDLE_TICKS = 15;
// s a battle shows its lost fleet before it ends.
constexpr f32 LOST_SECONDS = 3.0f;
// Charges in a skirmish ship's hold: enough for a long fight.
constexpr u32 SKIRMISH_AMMO = 1500;

// The skirmish's ship: the colony's first, its gun's hold full.
sim::ShipFit SkirmishFit()
{
	const sim::Catalog& catalog = sim::GetCatalog();
	sim::ShipPlan plan = catalog.colony.startShips.empty() ? catalog.colony.rescue
	                                                       : catalog.colony.startShips.front();
	for (sim::Stack& stack : plan.hold)
	{
		if (catalog.FindAmmo(stack.id))
			stack.count = SKIRMISH_AMMO;
	}
	return sim::FitFromPlan(catalog, plan);
}

std::vector<std::string> SlotIds(const sim::ShipFit& fit)
{
	std::vector<std::string> ids;
	for (const sim::FittedModule& slot : fit.slots)
		ids.push_back(slot.id);
	return ids;
}

bots::PilotSkill EnemySkill()
{
	const sim::Catalog& catalog = sim::GetCatalog();
	bots::PilotSkill skill;
	skill.sight = catalog.enemies.sight;
	skill.distance = catalog.rules.attackRange;
	return skill;
}

// The ammo a fit's guns take: the first gun's ("" for none).
std::string GunAmmo(const sim::ShipFit& fit)
{
	for (const sim::FittedModule& slot : fit.slots)
	{
		const sim::ModuleDesc* module = sim::GetCatalog().FindModule(slot.id);
		if (module && !module->ammo.empty())
			return module->ammo;
	}
	return {};
}

std::unique_ptr<sim::World> NewWorld(const sim::AsteroidFieldDesc& field)
{
	auto world = std::make_unique<sim::World>();
	world->rules = sim::GetCatalog().rules.combat;
	sim::MakeAsteroidField(*world, field);
	return world;
}
} // namespace

std::unique_ptr<Server::Match> Server::NewSkirmish(const MatchDesc& desc) const
{
	auto match = std::make_unique<Match>();
	match->kind = sim::MatchKind::Skirmish;
	match->rules = desc;
	match->world = NewWorld(desc.field);
	match->skill = EnemySkill();
	match->untilWave = desc.waveDelay;
	match->seed = desc.seed;
	return match;
}

bool Server::JoinSkirmish(Client& client)
{
	Match& match = *matches[0];
	const sim::ShipFit fit = SkirmishFit();
	sim::Ship ship;
	if (!sim::MakeShip(sim::GetCatalog(), fit, ship))
		return false;
	// Side by side, 10 m apart, clear of rocks.
	ship.position =
		FindRoom(*match.world, {10.0f * f32(match.players.size()), 0.0f}, ship.hull.radius + 1.0f);
	ship.team = sim::PLAYERS;
	const sim::ShipHandle handle = sim::SpawnShip(*match.world, ship);
	if (!handle)
		return false;
	Player& added = match.players.emplace_back();
	added.peer = client.peer;
	added.ship = handle;
	added.home = ship.position;
	client.match = &match;
	SendWelcome(match, added, *sim::GetShip(*match.world, handle), SlotIds(fit));
	return true;
}

bool Server::LaunchBattle(Client& client, sim::Hex hex, const std::vector<u32>& ships,
                          std::string& why)
{
	const sim::Catalog& catalog = sim::GetCatalog();
	sim::Account* account = LoadAccount(client.account);
	if (!account || !sim::Launch(catalog, *account, hex, ships, Now(), why))
		return false;
	auto battle = std::make_unique<Match>();
	Match& match = *battle;
	match.kind = sim::MatchKind::Battle;
	match.rules = started;
	match.rules.waves = false;
	match.rules.autopilot = false;
	match.rules.resetWhenEmpty = false;
	match.account = client.account;
	match.node = sim::NodeAt(catalog, hex);
	match.seed = match.node.seed | 1u;
	sim::AsteroidFieldDesc field = catalog.map.field;
	field.seed = match.node.seed;
	match.world = NewWorld(field);
	match.skill = EnemySkill();
	// The fleet in a row at the clearing's middle, the flagship first.
	for (usize i = 0; i < ships.size(); ++i)
	{
		const sim::ShipRecord* record = account->FindShip(ships[i]);
		sim::Ship ship;
		if (!record || !sim::MakeShip(catalog, record->fit, ship))
			continue;
		const f32 side = i == 0 ? 0.0f : (i % 2 ? -1.0f : 1.0f) * 8.0f * f32((i + 1) / 2);
		ship.position = FindRoom(*match.world, {side, -4.0f * f32(i > 0)}, ship.hull.radius + 1.0f);
		ship.team = sim::PLAYERS;
		FleetShip& member = match.fleet.emplace_back();
		member.record = record->id;
		member.fit = record->fit;
		member.ship = sim::SpawnShip(*match.world, ship);
		member.pilot.random = match.seed + u32(i) * 2u;
	}
	if (match.fleet.empty())
	{
		why = "error.ship";
		return false;
	}
	Player& player = match.players.emplace_back();
	player.peer = client.peer;
	player.ship = match.fleet[0].ship;
	player.home = sim::GetShip(*match.world, player.ship)->position;
	AddBots(match, match.node.enemies, {}, catalog.map.enemyNear, catalog.map.enemyFar,
	        match.node.strength);
	client.match = &match;
	matches.push_back(std::move(battle));
	SaveAccount(client.account);
	PH_LOG_INFO("server: %s launched %u ships at (%d, %d), ring %u: %u enemies x%.2f",
	            client.account.c_str(), u32(match.fleet.size()), hex.q, hex.r, match.node.ring,
	            u32(match.enemies.size()), f64(match.node.strength));
	SendWelcome(match, player, *sim::GetShip(*match.world, player.ship),
	            SlotIds(match.fleet[0].fit));
	return true;
}

void Server::SendWelcome(const Match& match, const Player& player, const sim::Ship& ship,
                         const std::vector<std::string>& modules)
{
	sim::Welcome message;
	message.ship = sim::ShipId(player.ship);
	message.tick = match.world->tick;
	message.respawn = match.rules.respawn;
	message.kind = match.kind;
	message.ring = match.node.ring;
	message.hull = ship.hull;
	message.autopilot = match.rules.autopilot;
	message.modules = modules;
	message.rocks.assign(match.world->rocks, match.world->rocks + match.world->rockCount);
	for (u32 c = 0; c < sim::MAX_CRATES; ++c)
	{
		if (match.world->crates[c].life > 0.0f)
			message.crates.push_back({c, match.world->crates[c].position});
	}
	Send(player.peer, sim::Write(message), net::Delivery::Reliable);
}

void Server::RemovePlayer(Match& match, net::PeerId peer)
{
	const auto player = std::find_if(match.players.begin(), match.players.end(),
	                                 [peer](const Player& p) { return p.peer == peer; });
	if (player == match.players.end())
		return;
	// A battle's fleet fights on without its player, for a while.
	if (match.kind == sim::MatchKind::Skirmish)
		sim::RemoveShip(*match.world, player->ship);
	match.players.erase(player);
}

void Server::Run(Match& match, f32 dt)
{
	const bool skirmish = match.kind == sim::MatchKind::Skirmish;
	if (skirmish && match.players.empty() && match.rules.resetWhenEmpty)
	{
		match.sinceTick = 0.0f;
		return; // a fresh match, waiting for someone
	}
	// A long stall (a breakpoint, the app in the background) skips time
	// rather than running hundreds of ticks.
	match.sinceTick += std::min(dt, 0.25f);
	while (match.sinceTick >= sim::TICK_SECONDS)
	{
		match.sinceTick -= sim::TICK_SECONDS;
		ApplyControls(match);
		{
			PH_PROFILE_SCOPE("Server.Bots");
			FlyBots(match);
		}
		{
			PH_PROFILE_SCOPE("Sim.Step");
			sim::Step(*match.world);
		}
		if (skirmish)
			RefereeSkirmish(match);
		else
			RefereeBattle(match);
		PH_PROFILE_SCOPE("Server.Send");
		SendEvents(match);
		SendSnapshots(match);
	}
}

void Server::ApplyControls(Match& match)
{
	for (Player& player : match.players)
	{
		if (player.queued.empty())
		{
			// The last controls hold, for a while.
			if (++player.idle == IDLE_TICKS && !match.rules.autopilot)
				sim::SetControls(*match.world, player.ship, {});
			continue;
		}
		player.idle = 0;
		if (player.queued.size() > MAX_LEAD)
			player.queued.erase(player.queued.begin(), player.queued.end() - MAX_LEAD);
		const Controls next = player.queued.front();
		player.queued.erase(player.queued.begin());
		player.applied = next.number;
		if (!match.rules.autopilot)
			sim::SetControls(*match.world, player.ship, next.controls);
	}
}

void Server::FlyBots(Match& match)
{
	sim::World& world = *match.world;
	for (Enemy& enemy : match.enemies)
	{
		sim::SetControls(world, enemy.ship, bots::Fly(world, enemy.ship, enemy.pilot, match.skill));
		sim::SetTarget(world, enemy.ship, enemy.pilot.target);
	}
	if (match.rules.autopilot)
	{
		for (Player& player : match.players)
		{
			sim::SetControls(world, player.ship,
			                 bots::Fly(world, player.ship, player.pilot, match.skill));
			sim::SetTarget(world, player.ship, player.pilot.target);
		}
	}
	// A battle's escorts, and the flagship while its player is away: at the
	// flagship's target, else the nearest; with no one in sight, close by the
	// flagship.
	for (usize i = 0; i < match.fleet.size(); ++i)
	{
		FleetShip& member = match.fleet[i];
		const sim::Ship* ship = sim::GetShip(world, member.ship);
		if (!ship || !sim::IsAlive(*ship) || (i == 0 && !match.players.empty()))
			continue;
		const sim::Ship* flagship = sim::GetShip(world, match.fleet[0].ship);
		if (flagship && sim::IsAlive(*flagship) && flagship->target && i > 0)
			member.pilot.target = flagship->target;
		sim::ShipControls controls = bots::Fly(world, member.ship, member.pilot, match.skill);
		if (!member.pilot.target && i > 0 && flagship && sim::IsAlive(*flagship))
		{
			bots::Order follow;
			follow.kind = bots::Order::Kind::Move;
			const f32 side = i % 2 ? -1.0f : 1.0f;
			follow.point = flagship->position + sim::Forward(flagship->angle + side * 2.4f) * 9.0f;
			controls = bots::Steer(*ship, follow, nullptr, 0.0f, world.rocks, world.rockCount,
			                       bots::KeepApart(world, member.ship));
		}
		sim::SetControls(world, member.ship, controls);
		sim::SetTarget(world, member.ship, member.pilot.target);
	}
}

void Server::RefereeSkirmish(Match& match)
{
	sim::World& world = *match.world;
	// A bot's wreck goes at once: this tick's events tell of its end.
	for (auto enemy = match.enemies.begin(); enemy != match.enemies.end();)
	{
		const sim::Ship* ship = sim::GetShip(world, enemy->ship);
		if (ship && sim::IsAlive(*ship))
		{
			++enemy;
			continue;
		}
		sim::RemoveShip(world, enemy->ship);
		enemy = match.enemies.erase(enemy);
		PH_LOG_INFO("server: tick %llu, a bot of wave %u is down",
		            static_cast<unsigned long long>(world.tick), match.wave);
	}
	// A player's wreck stays a while, then the ship is back home, whole.
	for (Player& player : match.players)
	{
		const sim::Ship* ship = sim::GetShip(world, player.ship);
		if (!ship || sim::IsAlive(*ship))
		{
			player.down = 0.0f;
			continue;
		}
		if (player.down == 0.0f)
			PH_LOG_INFO("server: tick %llu, player %u is down",
			            static_cast<unsigned long long>(world.tick), player.peer);
		player.down += sim::TICK_SECONDS;
		if (player.down >= match.rules.respawn)
		{
			sim::ReviveShip(world, player.ship,
			                FindRoom(world, player.home, ship->hull.radius + 1.0f), 0.0f);
			player.down = 0.0f;
			PH_LOG_INFO("server: player %u is back", player.peer);
		}
	}
	// The next wave once the last is gone, after a breath; none without
	// players.
	if (!match.rules.waves || match.players.empty() || !match.enemies.empty())
		return;
	match.untilWave -= sim::TICK_SECONDS;
	if (match.untilWave <= 0.0f)
	{
		SendWave(match);
		match.untilWave = match.rules.waveDelay;
	}
}

void Server::RefereeBattle(Match& match)
{
	const sim::Catalog& catalog = sim::GetCatalog();
	sim::World& world = *match.world;
	match.loot.resize(sim::MAX_CRATES);
	// This tick's ends and pickups: a crate where an enemy broke apart, its
	// loot to the ship that takes it.
	const u32 count = world.eventCount;
	for (u32 e = 0; e < count; ++e)
	{
		const sim::Event event = world.events[e];
		if (event.type == sim::EventType::ShipDestroyed)
		{
			const bool enemy = std::any_of(match.enemies.begin(), match.enemies.end(),
			                               [&](const Enemy& x) { return x.ship == event.ship; });
			if (!enemy)
				continue;
			++match.kills;
			const sim::Loot loot = sim::RollLoot(catalog, match.node, match.seed);
			f32 volume = 0.0f;
			f32 mass = 0.0f;
			sim::LootSize(catalog, loot, volume, mass);
			const u32 index =
				sim::DropCrate(world, event.position, volume, mass, catalog.rules.crateLife);
			if (index != sim::NO_INDEX)
				match.loot[index] = loot;
		}
		else if (event.type == sim::EventType::CratePicked && event.index < sim::MAX_CRATES)
		{
			for (FleetShip& member : match.fleet)
			{
				if (member.ship == event.ship)
					sim::AddLoot(member.haul, match.loot[event.index]);
			}
			match.loot[event.index] = {};
		}
	}
	for (auto enemy = match.enemies.begin(); enemy != match.enemies.end();)
	{
		const sim::Ship* ship = sim::GetShip(world, enemy->ship);
		if (ship && sim::IsAlive(*ship))
			++enemy;
		else
		{
			sim::RemoveShip(world, enemy->ship);
			enemy = match.enemies.erase(enemy);
		}
	}
	match.age += sim::TICK_SECONDS;
	if (!match.cleared && match.enemies.empty())
	{
		match.cleared = true;
		NoticeAll(match, "battle.cleared");
		PH_LOG_INFO("server: %s cleared (%d, %d) in %.0f s", match.account.c_str(),
		            match.node.hex.q, match.node.hex.r, f64(match.age));
	}
	if (match.cleared)
		match.sinceCleared += sim::TICK_SECONDS;
	const bool fleetLeft = std::any_of(match.fleet.begin(), match.fleet.end(),
	                                   [&](const FleetShip& m)
	                                   {
										   const sim::Ship* ship = sim::GetShip(world, m.ship);
										   return ship && sim::IsAlive(*ship);
									   });
	if (!fleetLeft)
		match.sinceLost += sim::TICK_SECONDS;
	if (match.players.empty())
		match.unattended += sim::TICK_SECONDS;
}

bool Server::BattleOver(const Match& match) const
{
	if (match.kind != sim::MatchKind::Battle)
		return false;
	return match.returning || match.sinceLost >= LOST_SECONDS ||
	       (match.cleared && match.sinceCleared >= sim::GetCatalog().rules.clearedReturn) ||
	       match.age >= hub.longestBattle ||
	       (match.players.empty() && match.unattended >= hub.unattended);
}

void Server::EndBattle(Match& match)
{
	const sim::Catalog& catalog = sim::GetCatalog();
	const sim::World& world = *match.world;
	sim::BattleOutcome outcome;
	outcome.node = match.node.hex;
	outcome.cleared = match.cleared;
	outcome.kills = match.kills;
	sim::BattleReport report;
	report.node = match.node.hex;
	report.kills = match.kills;
	for (const FleetShip& member : match.fleet)
	{
		sim::ShipOutcome& result = outcome.ships.emplace_back();
		result.id = member.record;
		const sim::Ship* ship = sim::GetShip(world, member.ship);
		if (!ship || !sim::IsAlive(*ship))
		{
			result.lost = true;
			++report.lost;
			continue;
		}
		result.condition = ship->health / std::max(ship->hull.health, 1.0f);
		u32 charges = ship->ammo;
		for (u32 i = 0; i < ship->moduleCount; ++i)
		{
			const sim::Module& module = ship->modules[i];
			result.modules.push_back(module.type.health > 0.0f ? module.health / module.type.health
			                                                   : 1.0f);
			charges += module.loaded;
		}
		// The hold as it left, its guns' charges as they are now, and the haul.
		const std::string ammo = GunAmmo(member.fit);
		for (const sim::Stack& stack : member.fit.hold)
		{
			if (stack.id != ammo)
				result.hold.push_back(stack);
		}
		if (!ammo.empty() && charges > 0)
			result.hold.push_back({ammo, charges});
		for (const sim::Stack& stack : member.haul)
		{
			result.hold.push_back(stack);
			report.loot.push_back(stack);
		}
	}
	report.end = report.lost == match.fleet.size() ? sim::BattleReport::End::Lost
	             : match.cleared                   ? sim::BattleReport::End::Cleared
	                                               : sim::BattleReport::End::Returned;
	const i64 now = Now();
	if (sim::Account* account = LoadAccount(match.account))
	{
		sim::ApplyOutcome(catalog, *account, outcome, now);
		SaveAccount(match.account);
		UpdateLeaderboard(match.account, *account);
	}
	static constexpr const char* ENDS[] = {"cleared", "returned", "lost"};
	PH_LOG_INFO("server: %s's battle at (%d, %d) %s: %u kills, %u ships lost",
	            match.account.c_str(), match.node.hex.q, match.node.hex.r, ENDS[u32(report.end)],
	            report.kills, report.lost);
	const std::vector<u8> result = sim::Write(sim::Result{sim::ToJson(report)});
	for (Client& client : clients)
	{
		if (client.match == &match)
			client.match = nullptr;
		if (client.account == match.account && !match.account.empty())
		{
			Send(client.peer, result, net::Delivery::Reliable);
			SendProfile(client);
		}
	}
	const std::string account = match.account;
	std::erase_if(matches, [&match](const std::unique_ptr<Match>& m) { return m.get() == &match; });
	ForgetAccount(account);
}

void Server::Restart(Match& match)
{
	match.world = NewWorld(match.rules.field);
	match.enemies.clear();
	match.wave = 0;
	match.rules.waves = started.waves;
	match.untilWave = match.rules.waveDelay;
	match.seed = match.rules.seed;
	PH_LOG_INFO("server: nobody left; the match starts over");
}

void Server::SendWave(Match& match)
{
	++match.wave;
	const u32 count = std::min(match.rules.firstWave + match.wave - 1, match.rules.largestWave);
	// From the first player that flies.
	Vec2 center = {};
	for (const Player& player : match.players)
	{
		const sim::Ship* ship = sim::GetShip(*match.world, player.ship);
		if (ship && sim::IsAlive(*ship))
		{
			center = ship->position;
			break;
		}
	}
	AddBots(match, count, center, 110.0f, 130.0f,
	        std::pow(sim::GetCatalog().enemies.growth, f32(match.wave - 1)));
	PH_LOG_INFO("server: wave %u, %u bots", match.wave, u32(match.enemies.size()));
}

u32 Server::AddBots(Match& match, u32 count, Vec2 center, f32 near, f32 far, f32 strength)
{
	const sim::Catalog& catalog = sim::GetCatalog();
	sim::World& world = *match.world;
	// Every ship in every snapshot.
	u32 ships = 0;
	for (u32 slot = 0; slot < sim::MAX_SHIPS; ++slot)
		ships += bool(world.shipIds[slot]);
	count = std::min(count, ships < sim::MAX_SNAPSHOT_SHIPS ? sim::MAX_SNAPSHOT_SHIPS - ships : 0);
	const f32 side = 2.0f * PI * Random(match);
	u32 added = 0;
	for (u32 i = 0; i < count; ++i)
	{
		const sim::ShipPlan& plan =
			catalog.enemies.fits[u32(Random(match) * f32(catalog.enemies.fits.size())) %
			                     catalog.enemies.fits.size()];
		sim::Ship ship;
		if (!sim::MakeShip(catalog, sim::FitFromPlan(catalog, plan), ship))
			continue;
		sim::Strengthen(ship, strength);
		const f32 angle = side + 0.18f * (f32(i) - 0.5f * f32(count - 1));
		const Vec2 at =
			FindRoom(world, center + sim::Forward(angle) * (near + (far - near) * Random(match)),
			         ship.hull.radius + 3.0f);
		ship.position = at;
		ship.angle = sim::AngleOf(center - at);
		ship.team = sim::BOTS;
		const sim::ShipHandle handle = sim::SpawnShip(world, ship);
		if (!handle)
			break;
		Enemy enemy;
		enemy.ship = handle;
		enemy.pilot.random = u32(Random(match) * 16777216.0f) * 2u + 1u;
		enemy.pilot.roam = center;
		match.enemies.push_back(enemy);
		++added;
	}
	return added;
}

void Server::RemoveBots(Match& match)
{
	for (const Enemy& enemy : match.enemies)
		sim::RemoveShip(*match.world, enemy.ship);
	match.enemies.clear();
	match.untilWave = match.rules.waveDelay;
}

Vec2 Server::FindRoom(const sim::World& world, Vec2 near, f32 radius) const
{
	// Out along a spiral until no rock is in the way.
	for (u32 k = 0; k < 64; ++k)
	{
		const Vec2 at = near + sim::Forward(2.4f * f32(k)) * (2.0f * f32(k));
		bool clear = true;
		for (u32 r = 0; r < world.rockCount && clear; ++r)
		{
			const sim::Rock& rock = world.rocks[r];
			const f32 reach = rock.radius + radius;
			clear = rock.health <= 0.0f ||
			        sim::Dot(at - rock.position, at - rock.position) >= reach * reach;
		}
		if (clear)
			return at;
	}
	return near;
}

f32 Server::Random(Match& match)
{
	match.seed = match.seed * 1664525u + 1013904223u;
	return f32(match.seed >> 8) / f32(1u << 24);
}

void Server::SendEvents(Match& match)
{
	if (match.players.empty() || match.world->eventCount == 0)
		return;
	sim::TakeEvents(*match.world, events);
	const std::vector<u8> bytes = sim::Write(events);
	for (const Player& player : match.players)
		Send(player.peer, bytes, net::Delivery::Reliable);
}

void Server::SendSnapshots(Match& match)
{
	// Each player's own: its ship first, the nearest after it, and what its
	// prediction and HUD need.
	for (const Player& player : match.players)
	{
		sim::TakeSnapshot(*match.world, snapshot, player.ship);
		snapshot.wave = match.wave;
		snapshot.input = player.applied;
		Send(player.peer, sim::Write(snapshot), net::Delivery::Unreliable);
	}
}
} // namespace sn::server
