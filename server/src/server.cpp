#include <sn/server/server.h>
#include <sn/sim/catalog.h>

#include <ph/core/log.h>
#include <ph/core/profile.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <random>
#include <string>

namespace sn::server
{
using namespace ph;

static_assert(sim::MAX_SNAPSHOT_BYTES <= net::MAX_UNRELIABLE_BYTES);

namespace
{
constexpr f32 CHAT_BURST = 5.0f;    // lines a player may say at once
constexpr f32 CHAT_RATE = 1.0f;     // more lines a second
constexpr f32 REQUEST_BURST = 5.0f; // requests a client may make at once
constexpr f32 REQUEST_RATE = 2.0f;  // more a second
constexpr u32 DEFAULT_BOTS = 3;     // /add_bots without a count
constexpr u32 MAX_CLIENTS = 64;     // the skirmish has its own limit within
constexpr u32 LEADERBOARD_SIZE = 100;
// Accounts the store may hold: newcomers past it are turned away (ADR 0014).
constexpr u32 MAX_ACCOUNTS = 10'000;
// Newcomers: the time to say Hello, and a refused one's for its Refusal to go out.
constexpr f32 HELLO_SECONDS = 10.0f;
constexpr f32 REFUSED_SECONDS = 1.0f;
// Clients send controls, names, chat lines and requests: a few hundred
// bytes; a Login with a sign-in's proof, a few thousand.
constexpr u32 MAX_CLIENT_MESSAGE = 12 * 1024;
// A player's controls kept before they are applied, one set a tick.
constexpr u32 MAX_QUEUED = 8;

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

// 128 random bits as hex: a key handed out at sign-in.
std::string NewKey()
{
	std::random_device random;
	std::string key;
	while (key.size() < sim::KEY_BYTES)
	{
		const u32 bits = random();
		for (u32 i = 0; i < 8 && key.size() < sim::KEY_BYTES; ++i)
			key += "0123456789abcdef"[(bits >> (i * 4)) & 15];
	}
	return key;
}

// "Pilot 3": the name a client gets before it has one of its own.
bool IsDefaultName(std::string_view name) { return name.starts_with("Pilot "); }

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

bool Server::Start(const char* address, const MatchDesc& desc, const HubDesc& hubDesc)
{
	Stop();
	listener = net::Listen(address);
	if (!listener)
		return false;
	// What a peer can make the server hold follows this (pith ADR 0036).
	net::ServerLimits limits;
	limits.maxIncomingReliableBytes = MAX_CLIENT_MESSAGE;
	net::SetLimits(listener, limits);
	started = desc;
	hub = hubDesc;
	startedAt = Now();
	if (!hub.admin.empty())
	{
		adminListener = net::Listen(hub.admin.c_str());
		if (adminListener)
		{
			net::ServerLimits adminLimits;
			adminLimits.maxIncomingReliableBytes = 64 * 1024;
			net::SetLimits(adminListener, adminLimits);
		}
		else
			PH_LOG_ERROR("server: no admin listener at %s", hub.admin.c_str());
	}
	newcomers.clear();
	clients.clear();
	matches.clear();
	matches.push_back(NewSkirmish(desc));
	accounts.clear();
	summaries.clear();
	leaderboard = {};
	leaderboardChanged = false;
	accountCount = 0;
	sent = {};
	received = {};
	if (hub.store)
	{
		// The leaderboard and the admin's summaries from every account kept.
		const std::vector<std::string> ids = hub.store->List();
		accountCount = u32(ids.size());
		for (const std::string& id : ids)
		{
			std::string json;
			sim::Account account;
			if (!hub.store->Load(id, json) || !sim::FromJson(json, account))
				continue;
			UpdateLeaderboard(id, account);
			summaries[id] = SummaryOf(account);
		}
		leaderboardChanged = true;
	}
	PH_LOG_INFO("server: listening at %s; %u rocks%s%s%s%s", address, matches[0]->world->rockCount,
	            desc.waves ? "; waves" : "", hub.store ? "; the hub is open" : "",
	            hub.signIn ? "; sign-ins" : "", adminListener ? "; the admin listens" : "");
	return true;
}

void Server::Stop()
{
	if (!listener)
		return;
	// Battles in progress end as returned: their fleets come home.
	while (matches.size() > 1)
	{
		matches.back()->returning = true;
		EndBattle(*matches.back());
	}
	WriteLeaderboard();
	if (adminListener)
		net::Close(adminListener);
	adminListener = {};
	net::Close(listener);
	listener = {};
	matches.clear();
	clients.clear();
	newcomers.clear();
	accounts.clear();
}

const sim::World* Server::GetWorld() const
{
	return matches.empty() ? nullptr : matches[0]->world.get();
}

sim::World* Server::GetWorld() { return matches.empty() ? nullptr : matches[0]->world.get(); }

u32 Server::GetWave() const { return matches.empty() ? 0 : matches[0]->wave; }

u32 Server::GetPlayerCount() const { return matches.empty() ? 0 : u32(matches[0]->players.size()); }

u32 Server::GetBotCount() const { return matches.empty() ? 0 : u32(matches[0]->enemies.size()); }

sim::World* Server::GetBattleWorld(u32 index)
{
	return index + 1 < matches.size() ? matches[index + 1]->world.get() : nullptr;
}

i64 Server::Now() const { return hub.clock ? hub.clock() : i64(std::time(nullptr)); }

Server::Client* Server::FindClient(net::PeerId peer)
{
	const auto found = std::find_if(clients.begin(), clients.end(),
	                                [peer](const Client& c) { return c.peer == peer; });
	return found == clients.end() ? nullptr : &*found;
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
	PollSignIns();
	UpdateAdmin();
	// The skirmish's newcomers, once their Name had its chance (it follows
	// Hello); allowances.
	for (Client& client : clients)
	{
		const f32 step = std::min(dt, 1.0f);
		client.chatLines = std::min(CHAT_BURST, client.chatLines + step * CHAT_RATE);
		client.requests = std::min(REQUEST_BURST, client.requests + step * REQUEST_RATE);
		if (client.announced || client.match != matches[0].get())
			continue;
		client.announced = true;
		PH_LOG_INFO("server: player %u is %s", client.peer, client.name.c_str());
		NoticeAll(*matches[0], "chat.joined", client.name);
	}
	for (usize m = 0; m < matches.size(); ++m)
		Run(*matches[m], dt);
	for (usize m = 1; m < matches.size();)
	{
		if (BattleOver(*matches[m]))
			EndBattle(*matches[m]); // removes it
		else
			++m;
	}
	WriteLeaderboard();
}

void Server::WriteLeaderboard()
{
	if (!leaderboardChanged || hub.leaderboard.empty())
		return;
	leaderboardChanged = false;
	if (!WriteWhole(hub.leaderboard,
	                leaderboard.Json(sim::GetCatalog().season, Now(), LEADERBOARD_SIZE)))
		PH_LOG_WARN("server: cannot write the leaderboard to %s", hub.leaderboard.c_str());
}

void Server::OnMessage(net::PeerId peer, const u8* data, u32 size)
{
	received.Add(data, size);
	Client* client = FindClient(peer);
	switch (sim::TypeOf(data, size))
	{
		case sim::MessageType::Hello: OnHello(peer, data, size); return;
		case sim::MessageType::Input:
		{
			sim::Input input;
			if (client && sim::Read(data, size, input))
				OnInput(*client, input);
			return;
		}
		case sim::MessageType::Name:
		{
			sim::Name name;
			if (client && sim::Read(data, size, name))
				OnName(*client, name.name);
			return;
		}
		case sim::MessageType::Say:
		{
			sim::Say say;
			if (client && sim::Read(data, size, say))
				OnSay(*client, say.text);
			return;
		}
		case sim::MessageType::Login:
		{
			sim::Login login;
			if (client && sim::Read(data, size, login))
				OnLogin(*client, login);
			return;
		}
		case sim::MessageType::Request:
		{
			sim::Request request;
			if (client && sim::Read(data, size, request))
				OnRequest(*client, request.json);
			return;
		}
		default: return; // what a server does not take
	}
}

void Server::OnHello(net::PeerId peer, const u8* data, u32 size)
{
	// Hello is reliable: a second one says nothing new.
	if (FindClient(peer))
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
	if (hello.version != sim::PROTOCOL_VERSION || hello.catalog != sim::GetCatalog().hash)
	{
		PH_LOG_INFO("server: peer %u speaks protocol %u (catalog %08x), not %u (%08x)", peer,
		            hello.version, hello.catalog, sim::PROTOCOL_VERSION, sim::GetCatalog().hash);
		Refuse(peer, sim::RefusalReason::Version);
		return;
	}
	const bool skirmish = hello.joining == sim::Joining::Skirmish;
	if (clients.size() >= MAX_CLIENTS ||
	    (skirmish && matches[0]->players.size() >= started.maxPlayers) || (!skirmish && !hub.store))
	{
		PH_LOG_INFO("server: peer %u turned away: %u clients already", peer, u32(clients.size()));
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
	Client& added = clients.emplace_back();
	added.peer = peer;
	added.name = name;
	if (skirmish && !JoinSkirmish(added))
	{
		clients.pop_back();
		Refuse(peer, sim::RefusalReason::Full);
		return;
	}
	PH_LOG_INFO("server: peer %u joined %s", peer, skirmish ? "the skirmish" : "the hub");
}

void Server::OnInput(Client& client, const sim::Input& input)
{
	if (!client.match)
		return;
	for (Player& player : client.match->players)
	{
		if (player.peer != client.peer)
			continue;
		// Each number once, in order: a message repeats the newest few.
		const u32 first = input.last - (input.count - 1);
		for (u32 i = 0; i < input.count; ++i)
		{
			if (first + i <= player.received)
				continue;
			player.queued.push_back({first + i, input.controls[i]});
			player.received = first + i;
		}
		if (player.queued.size() > MAX_QUEUED)
			player.queued.erase(player.queued.begin(), player.queued.end() - MAX_QUEUED);
		// Its turrets' choice: an enemy, or none.
		const sim::ShipHandle target = sim::ShipFromId(input.target);
		const sim::Ship* other =
			input.target ? sim::GetShip(*client.match->world, target) : nullptr;
		const sim::Ship* own = sim::GetShip(*client.match->world, player.ship);
		sim::SetTarget(*client.match->world, player.ship,
		               other && own && other->team != own->team ? target : sim::ShipHandle{});
		return;
	}
}

void Server::OnName(Client& client, std::string_view wanted)
{
	const std::string clean = sim::CleanText(wanted, sim::MAX_NAME_BYTES);
	if (clean.empty() || clean == client.name)
		return;
	const std::string name = UniqueName(clean, &client);
	if (name == client.name)
		return;
	// After "joined" went out, a new name is told, and costs a chat line.
	if (client.announced)
	{
		if (client.chatLines < 1.0f)
			return;
		client.chatLines -= 1.0f;
	}
	PH_LOG_INFO("server: player %u is now %s", client.peer, name.c_str());
	const std::string old = client.name;
	client.name = name;
	if (client.announced && client.match)
		NoticeAll(*client.match, "chat.renamed", old, name);
	if (sim::Account* account = client.account.empty() ? nullptr : LoadAccount(client.account))
	{
		account->name = name;
		SaveAccount(client.account);
		UpdateLeaderboard(client.account, *account);
	}
}

std::string Server::UniqueName(std::string_view wanted, const Client* self) const
{
	const auto taken = [&](std::string_view name)
	{
		return std::any_of(clients.begin(), clients.end(), [&](const Client& other)
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

void Server::Leave(net::PeerId peer)
{
	Client* client = FindClient(peer);
	if (!client)
		return;
	Match* match = client->match;
	const std::string name = client->name;
	const bool announced = client->announced;
	const std::string account = client->account;
	if (match)
		RemovePlayer(*match, peer);
	std::erase_if(clients, [peer](const Client& c) { return c.peer == peer; });
	PH_LOG_INFO("server: player %u left", peer);
	if (match && announced)
		NoticeAll(*match, "chat.left", name);
	if (match == matches[0].get() && match->players.empty() && started.resetWhenEmpty)
		Restart(*match);
	if (!account.empty())
		ForgetAccount(account);
}

void Server::OnSay(Client& client, std::string_view text)
{
	const std::string line = sim::CleanText(text, sim::MAX_CHAT_BYTES);
	if (line.empty() || !client.match)
		return;
	if (client.chatLines < 1.0f)
	{
		Notice(client.peer, "chat.too_fast");
		return;
	}
	client.chatLines -= 1.0f;
	if (line[0] == '/')
	{
		PH_LOG_INFO("server: %s: %s", client.name.c_str(), line.c_str());
		Command(client, line);
		return;
	}
	PH_LOG_INFO("chat: %s: %s", client.name.c_str(), line.c_str());
	sim::Chat chat;
	for (const Player& player : client.match->players)
	{
		if (player.peer == client.peer)
			chat.ship = sim::ShipId(player.ship);
	}
	chat.name = client.name;
	chat.text = line;
	Broadcast(*client.match, chat);
}

void Server::Command(Client& client, std::string_view line)
{
	Match& match = *client.match;
	std::string_view name;
	std::string_view argument;
	SplitCommand(line, name, argument);
	const bool skirmish = match.kind == sim::MatchKind::Skirmish;
	if (name == "help")
		Notice(client.peer, "chat.help");
	else if (name == "add_bots" && skirmish)
	{
		const u32 count = argument.empty() ? DEFAULT_BOTS : ParseCount(argument);
		if (count == 0)
		{
			Notice(client.peer, "chat.count");
			return;
		}
		Vec2 near = {};
		for (const Player& player : match.players)
		{
			const sim::Ship* ship = sim::GetShip(*match.world, player.ship);
			if (player.peer == client.peer)
				near = ship ? ship->position : player.home;
		}
		const u32 added = AddBots(match, count, near, 80.0f, 100.0f, 1.0f);
		if (added == 0)
			Notice(client.peer, "chat.full", {}, std::to_string(sim::MAX_SNAPSHOT_SHIPS));
		else
			NoticeAll(match, "chat.bots_added", client.name, std::to_string(added));
	}
	else if (name == "remove_bots" && skirmish)
	{
		RemoveBots(match);
		NoticeAll(match, "chat.bots_removed", client.name);
	}
	else if (name == "waves" && skirmish && (argument == "on" || argument == "off"))
	{
		match.rules.waves = argument == "on";
		match.untilWave = match.rules.waveDelay;
		NoticeAll(match, match.rules.waves ? "chat.waves_on" : "chat.waves_off", client.name);
	}
	else if (name == "waves" && skirmish)
		Notice(client.peer, "chat.waves_usage");
	else if (name == "who")
	{
		std::string names;
		for (const Player& player : match.players)
		{
			if (const Client* other = FindClient(player.peer))
				names += (names.empty() ? "" : ", ") + other->name;
		}
		Notice(client.peer, "chat.who", {}, sim::CleanText(names, sim::MAX_CHAT_BYTES));
	}
	else
		Notice(client.peer, "chat.unknown",
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

void Server::NoticeAll(const Match& match, const char* key, std::string_view name,
                       std::string_view extra)
{
	sim::Chat chat;
	chat.notice = true;
	chat.text = key;
	chat.name = name;
	chat.extra = extra;
	Broadcast(match, chat);
}

void Server::Broadcast(const Match& match, const sim::Chat& chat)
{
	const std::vector<u8> bytes = sim::Write(chat);
	for (const Player& player : match.players)
		Send(player.peer, bytes, net::Delivery::Reliable);
}

void Server::OnLogin(Client& client, const sim::Login& login)
{
	if (!hub.store || client.match || client.signIn)
		return;
	// Once a connection, except a guest's sign-in (Steam came up late).
	if (!client.account.empty())
	{
		const sim::Account* account = LoadAccount(client.account);
		if (login.provider.empty() || !account || !account->IsGuest())
			return;
	}
	if (!IsKey(login.key))
	{
		Notice(client.peer, "error.account");
		return;
	}
	if (login.provider.empty())
	{
		FinishLogin(client, login, nullptr);
		return;
	}
	if (!hub.signIn)
	{
		SignInAnswer off;
		off.provider = login.provider;
		off.error = "error.signin_off";
		FinishLogin(client, login, &off);
		return;
	}
	// The provider answers later (PollSignIns): until then, no account.
	client.signIn = ++nextSignIn;
	client.login = login;
	hub.signIn->Check(client.signIn, {login.provider, login.proof, login.nonce});
}

void Server::PollSignIns()
{
	if (!hub.signIn)
		return;
	u64 ticket = 0;
	SignInAnswer answer;
	while (hub.signIn->Poll(ticket, answer))
	{
		const auto client = std::find_if(clients.begin(), clients.end(),
		                                 [ticket](const Client& c) { return c.signIn == ticket; });
		if (client == clients.end())
			continue; // it left meanwhile
		client->signIn = 0;
		const sim::Login login = std::move(client->login);
		client->login = {};
		FinishLogin(*client, login, &answer);
	}
}

bool Server::FindByKey(const std::string& key, std::string& id)
{
	id = AccountId(key);
	if (const sim::Account* account = LoadAccount(id))
	{
		if (account->Opens(key))
			return true;
		id.clear();
		return false;
	}
	std::string linked;
	if (hub.store->LoadLink(KeyLinkName(key), linked))
	{
		const sim::Account* account = LoadAccount(linked);
		if (account && account->Opens(key))
		{
			id = linked;
			return true;
		}
	}
	id.clear();
	return true;
}

std::string Server::HandOutKey(const std::string& id, sim::Account& account)
{
	std::string key = NewKey();
	while (account.keys.size() >= sim::MAX_KEYS_KEPT)
	{
		hub.store->RemoveLink(KeyLinkName(account.keys.front()));
		account.keys.erase(account.keys.begin());
	}
	account.keys.push_back(key);
	hub.store->SaveLink(KeyLinkName(key), id);
	return key;
}

bool Server::Saves(const sim::Account& account) const
{
	return hub.keepGuests || !account.IsGuest() || account.kept;
}

bool Server::Ranks(const sim::Account& account) const
{
	return !account.IsGuest() && !account.hidden && !account.banned;
}

void Server::FinishLogin(Client& client, const sim::Login& login, const SignInAnswer* answer)
{
	const i64 now = Now();
	// A guest signing in on its connection lets go of the guest account.
	const std::string previous = client.account;
	client.account.clear();
	sim::Signed reply;
	reply.key = login.key;
	if (hub.signIn)
		reply.offers = hub.signIn->Offers();
	std::string keyed;
	if (!FindByKey(login.key, keyed))
	{
		Notice(client.peer, "error.account");
		return;
	}
	const bool signedIn = answer && answer->error.empty();
	if (answer && !signedIn)
		reply.error = answer->error;
	const auto full = [&]
	{
		if (accountCount < MAX_ACCOUNTS)
			return false;
		PH_LOG_WARN("server: %u accounts already: a new one is refused", accountCount);
		Notice(client.peer, "error.full");
		return true;
	};
	const auto make = [&](const std::string& key)
	{
		const std::string made = AccountId(key);
		accounts[made] = sim::NewAccount(sim::GetCatalog(), key, client.name, now);
		if (hub.seed)
			hub.seed(accounts[made]);
		return made;
	};
	std::string id;
	std::string owner;
	if (signedIn && hub.store->LoadLink(LinkName(answer->provider, answer->id), owner) &&
	    LoadAccount(owner))
	{
		// The sign-in's account wins. A device without a key for it gets
		// one; what a guest did there is dropped.
		id = owner;
		if (keyed != id)
			reply.key = HandOutKey(id, *LoadAccount(id));
	}
	else if (signedIn && (keyed.empty() || LoadAccount(keyed)->IsGuest()))
	{
		// The device's guest (or a new account on its key) signs in: saved
		// from now on, progress and all.
		const bool fresh = keyed.empty() || !summaries.contains(keyed);
		if (fresh && full())
			return;
		id = keyed.empty() ? make(login.key) : keyed;
		if (fresh)
			++accountCount;
	}
	else if (signedIn)
	{
		// The device's key opens another signed-in account: a new one, with
		// a new key.
		if (full())
			return;
		reply.key = NewKey();
		id = make(reply.key);
		++accountCount;
	}
	else if (!keyed.empty())
		id = keyed;
	else
	{
		// A guest: in memory while connected (saved only with keepGuests).
		if (hub.keepGuests && full())
			return;
		id = make(login.key);
		if (hub.keepGuests)
			++accountCount;
		PH_LOG_INFO("server: a guest account %s for peer %u", id.c_str(), client.peer);
	}
	sim::Account* account = LoadAccount(id);
	if (!previous.empty() && previous != id)
		ForgetAccount(previous);
	if (signedIn && !account->FindIdentity(answer->provider))
	{
		account->identities.push_back({answer->provider, answer->id, answer->name});
		account->kept = false; // a guest no more: saved by its sign-in
		hub.store->SaveLink(LinkName(answer->provider, answer->id), id);
		PH_LOG_INFO("server: account %s signs in with %s", id.c_str(), answer->provider.c_str());
	}
	if (account->banned)
	{
		PH_LOG_INFO("server: account %s is banned", id.c_str());
		Notice(client.peer, "error.banned");
		ForgetAccount(id);
		return;
	}
	// One connection an account: a newer one takes it over.
	for (Client& other : clients)
	{
		if (&other != &client && other.account == id)
		{
			Notice(other.peer, "error.elsewhere");
			other.account.clear();
		}
	}
	client.account = id;
	// The name: the client's, or for a sign-in the provider's, while the
	// pilot has none of its own.
	const std::string offered = sim::CleanText(
		signedIn && !answer->name.empty() ? std::string_view(answer->name) : login.name,
		sim::MAX_NAME_BYTES);
	if (!offered.empty() && offered != account->name && (!signedIn || IsDefaultName(account->name)))
		account->name = offered;
	client.name = UniqueName(account->name, &client);
	sim::Advance(sim::GetCatalog(), *account, now);
	SaveAccount(id);
	UpdateLeaderboard(id, *account);
	PH_LOG_INFO("server: peer %u is account %s (%s, %s)", client.peer, id.c_str(),
	            account->name.c_str(),
	            account->kept        ? "a kept guest"
	            : account->IsGuest() ? "a guest"
	                                 : std::string(account->Provider()).c_str());
	// A kept guest is saved by its device's key: "device", for the hub to say so.
	reply.provider = account->kept ? "device" : account->Provider();
	Send(client.peer, sim::Write(reply), net::Delivery::Reliable);
	SendProfile(client);
}

void Server::OnRequest(Client& client, std::string_view json)
{
	if (client.account.empty())
		return;
	if (client.requests < 1.0f)
	{
		Notice(client.peer, "error.too_fast");
		return;
	}
	client.requests -= 1.0f;
	sim::Operation operation;
	sim::Account* account = LoadAccount(client.account);
	if (!account || !sim::FromJson(json, operation))
		return;
	const sim::Catalog& catalog = sim::GetCatalog();
	const i64 now = Now();
	std::string why;
	bool done = true;
	switch (operation.kind)
	{
		case sim::Operation::Kind::Refresh: sim::Advance(catalog, *account, now); break;
		case sim::Operation::Kind::Upgrade:
			done = sim::Upgrade(catalog, *account, operation.building, now, why);
			break;
		case sim::Operation::Kind::Build:
			done = sim::Build(catalog, *account, operation.id, now, why);
			break;
		case sim::Operation::Kind::Fit:
			done = sim::FitModule(catalog, *account, operation.ship, operation.slot,
			                      operation.module, why);
			break;
		case sim::Operation::Kind::Load:
			done = sim::LoadHold(catalog, *account, operation.ship, operation.id, operation.count,
			                     why);
			break;
		case sim::Operation::Kind::Repair:
			done = sim::Repair(catalog, *account, operation.ship, now, why);
			break;
		case sim::Operation::Kind::Launch:
			if (client.match)
			{
				done = false;
				why = "error.busy";
				break;
			}
			done = LaunchBattle(client, operation.node, operation.ships, why);
			break;
		case sim::Operation::Kind::Return:
			if (client.match && client.match->kind == sim::MatchKind::Battle)
				client.match->returning = true;
			return; // the battle's end answers
	}
	if (!done)
	{
		Notice(client.peer, why.empty() ? "error.refused" : why.c_str());
		return;
	}
	SaveAccount(client.account);
	SendProfile(client);
}

sim::Account* Server::LoadAccount(const std::string& id)
{
	const auto found = accounts.find(id);
	if (found != accounts.end())
		return &found->second;
	std::string json;
	sim::Account account;
	if (!hub.store || !hub.store->Load(id, json) || !sim::FromJson(json, account))
		return nullptr;
	// Read from the store, no battle of it can be running: ships away come
	// home as they left.
	sim::BringHome(account);
	return &(accounts[id] = std::move(account));
}

void Server::SaveAccount(const std::string& id)
{
	const auto found = accounts.find(id);
	if (!hub.store || found == accounts.end() || !Saves(found->second))
		return;
	hub.store->Save(id, sim::ToJson(found->second, true));
	summaries[id] = SummaryOf(found->second);
}

Server::Summary Server::SummaryOf(const sim::Account& account)
{
	Summary summary;
	summary.name = account.name;
	summary.identities = account.identities;
	summary.deepest = account.deepest;
	summary.battles = account.stats.battles;
	summary.kills = account.stats.kills;
	summary.created = account.created;
	summary.updated = account.updated;
	summary.banned = account.banned;
	summary.hidden = account.hidden;
	return summary;
}

void Server::SendProfile(const Client& client)
{
	const auto found = accounts.find(client.account);
	if (found == accounts.end())
		return;
	Send(client.peer, sim::Write(sim::Profile{sim::ProfileJson(found->second, Now())}),
	     net::Delivery::Reliable);
}

bool Server::InUse(const std::string& id) const
{
	return std::any_of(clients.begin(), clients.end(),
	                   [&](const Client& c) { return c.account == id; }) ||
	       std::any_of(matches.begin(), matches.end(),
	                   [&](const std::unique_ptr<Match>& m) { return m->account == id; });
}

void Server::ForgetAccount(const std::string& id)
{
	// A guest's goes with it: nothing of it was saved.
	if (!InUse(id))
		accounts.erase(id);
}

void Server::UpdateLeaderboard(const std::string& id, const sim::Account& account)
{
	if (leaderboard.Update(id, account.name, Ranks(account) ? account.deepest : 0,
	                       account.deepestAt, account.Provider()))
		leaderboardChanged = true;
}

void Server::Send(net::PeerId peer, const std::vector<u8>& bytes, net::Delivery delivery)
{
	if (net::Send(listener, peer, bytes.data(), u32(bytes.size()), delivery))
		sent.Add(bytes.data(), bytes.size());
}
} // namespace sn::server
