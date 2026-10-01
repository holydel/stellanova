#include <sn/server/server.h>

#include <ph/core/log.h>

#include <algorithm>

namespace sn::server
{
using namespace ph;

bool Server::Start(const char* address)
{
	Stop();
	listener = net::Listen(address);
	if (!listener)
		return false;
	world = std::make_unique<sim::World>();
	sinceTick = 0.0f;
	PH_LOG_INFO("server: listening at %s", address);
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
	// A long stall (a breakpoint, the app in the background) skips time
	// rather than running hundreds of ticks.
	sinceTick += std::min(dt, 0.25f);
	while (sinceTick >= sim::TICK_SECONDS)
	{
		sim::Step(*world);
		sinceTick -= sim::TICK_SECONDS;
		SendSnapshots();
	}
}

void Server::OnMessage(net::PeerId peer, const u8* data, u32 size)
{
	const auto player = std::find_if(players.begin(), players.end(),
	                                 [peer](const Player& p) { return p.peer == peer; });
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
			players.push_back({peer, handle});
			const std::vector<u8> welcome =
				sim::Write(sim::Welcome{sim::ShipId(handle), world->tick});
			net::Send(listener, peer, welcome.data(), u32(welcome.size()), net::Delivery::Reliable);
			PH_LOG_INFO("server: player %u joined", peer);
			return;
		}
		case sim::MessageType::Input:
		{
			sim::Input input;
			if (player != players.end() && sim::Read(data, size, input))
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
}

void Server::SendSnapshots()
{
	if (players.empty())
		return;
	sim::TakeSnapshot(*world, snapshot);
	const std::vector<u8> bytes = sim::Write(snapshot);
	for (const Player& player : players)
		net::Send(listener, player.peer, bytes.data(), u32(bytes.size()),
		          net::Delivery::Unreliable);
}
} // namespace sn::server
