// The admin page's requests (docs/adr/0016-sign-in-and-admin.md): JSON
// objects over the admin listener, which only the proxy reaches, and only
// for WOS Observer's superusers. Every change goes to the log.

#include <sn/server/server.h>
#include <sn/sim/catalog.h>

#include <ph/core/log.h>

#include <yyjson.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sn::server
{
using namespace ph;

namespace
{
constexpr u32 LEADERS_SHOWN = 20;
constexpr u32 DEFAULT_LIMIT = 50;
constexpr u32 MAX_LIMIT = 200;
constexpr f64 MAX_GRANT = 1e9;

class Reading
{
public:
	Reading(const u8* data, u32 size)
		: document(yyjson_read(reinterpret_cast<const char*>(data), size, 0))
	{
	}
	~Reading() { yyjson_doc_free(document); }
	Reading(const Reading&) = delete;
	Reading& operator=(const Reading&) = delete;
	yyjson_val* Root() const { return document ? yyjson_doc_get_root(document) : nullptr; }

private:
	yyjson_doc* document;
};

std::string_view Text(yyjson_val* object, const char* key)
{
	yyjson_val* value = yyjson_is_obj(object) ? yyjson_obj_get(object, key) : nullptr;
	return yyjson_is_str(value) ? std::string_view(yyjson_get_str(value), yyjson_get_len(value))
	                            : std::string_view();
}

f64 Number(yyjson_val* object, const char* key, f64 fallback = 0.0)
{
	yyjson_val* value = yyjson_is_obj(object) ? yyjson_obj_get(object, key) : nullptr;
	return yyjson_is_num(value) ? yyjson_get_num(value) : fallback;
}

bool Flag(yyjson_val* object, const char* key)
{
	yyjson_val* value = yyjson_is_obj(object) ? yyjson_obj_get(object, key) : nullptr;
	return yyjson_is_true(value);
}

// An answer being written: one JSON object.
class Out
{
public:
	Out() : document(yyjson_mut_doc_new(nullptr)), root(yyjson_mut_obj(document))
	{
		yyjson_mut_doc_set_root(document, root);
	}
	~Out() { yyjson_mut_doc_free(document); }
	Out(const Out&) = delete;
	Out& operator=(const Out&) = delete;

	yyjson_mut_val* Root() const { return root; }
	yyjson_mut_val* Object() { return yyjson_mut_obj(document); }
	yyjson_mut_val* Array() { return yyjson_mut_arr(document); }
	// Keys are literals: yyjson keeps the pointer.
	void Set(yyjson_mut_val* object, const char* key, std::string_view text)
	{
		yyjson_mut_obj_add_strncpy(document, object, key, text.data(), text.size());
	}
	void Set(yyjson_mut_val* object, const char* key, const char* text)
	{
		Set(object, key, std::string_view(text));
	}
	void Set(yyjson_mut_val* object, const char* key, i64 value)
	{
		yyjson_mut_obj_add_int(document, object, key, value);
	}
	void Set(yyjson_mut_val* object, const char* key, u32 value) { Set(object, key, i64(value)); }
	void Set(yyjson_mut_val* object, const char* key, bool value)
	{
		yyjson_mut_obj_add_bool(document, object, key, value);
	}
	void Set(yyjson_mut_val* object, const char* key, yyjson_mut_val* value)
	{
		yyjson_mut_obj_add_val(document, object, key, value);
	}
	void Append(yyjson_mut_val* array, yyjson_mut_val* value)
	{
		yyjson_mut_arr_append(array, value);
	}
	void AppendText(yyjson_mut_val* array, std::string_view text)
	{
		yyjson_mut_arr_add_strncpy(document, array, text.data(), text.size());
	}
	// Other JSON text, as a value of this answer (null when it is not JSON).
	yyjson_mut_val* Copy(std::string_view json)
	{
		yyjson_doc* other = yyjson_read(json.data(), json.size(), 0);
		yyjson_mut_val* value = other ? yyjson_val_mut_copy(document, yyjson_doc_get_root(other))
		                              : yyjson_mut_null(document);
		yyjson_doc_free(other);
		return value;
	}
	std::string Write() const
	{
		usize length = 0;
		char* text = yyjson_mut_write(document, 0, &length);
		std::string json = text ? std::string(text, length) : std::string("{}");
		std::free(text);
		return json;
	}

private:
	yyjson_mut_doc* document;
	yyjson_mut_val* root;
};

// The store's ids: 16 lowercase hex digits (nothing else reaches a path).
bool IsAccountId(std::string_view id)
{
	return id.size() == 16 &&
	       std::all_of(id.begin(), id.end(),
	                   [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool Contains(std::string_view text, std::string_view part)
{
	return std::search(text.begin(), text.end(), part.begin(), part.end(), [](char a, char b)
	                   { return std::tolower(u8(a)) == std::tolower(u8(b)); }) != text.end();
}
} // namespace

void Server::UpdateAdmin()
{
	if (!adminListener)
		return;
	net::Event event;
	while (net::Poll(adminListener, event))
	{
		if (event.type == net::EventType::Connected)
			PH_LOG_INFO("admin: page %u connected", event.peer);
		else if (event.type == net::EventType::Disconnected)
			PH_LOG_INFO("admin: page %u left", event.peer);
		else if (event.type == net::EventType::Message)
			OnAdmin(event.peer, event.data, event.size);
	}
}

void Server::OnAdmin(net::PeerId peer, const u8* data, u32 size)
{
	const Reading request(data, size);
	yyjson_val* root = request.Root();
	const std::string op(Text(root, "op"));
	const std::string id(Text(root, "account"));
	std::string answer;
	std::string why;
	const auto account = [&]() -> sim::Account*
	{
		sim::Account* found = IsAccountId(id) ? LoadAccount(id) : nullptr;
		if (!found)
			why = "unknown account";
		return found;
	};
	if (!yyjson_is_obj(root))
		why = "not a JSON object";
	else if (op == "status")
		answer = AdminStatus();
	else if (op == "accounts")
	{
		const f64 offset = Number(root, "offset");
		const f64 limit = Number(root, "limit", DEFAULT_LIMIT);
		answer = AdminAccounts(Text(root, "query"), Text(root, "filter"),
		                       u32(std::clamp(offset, 0.0, 1e9)),
		                       u32(std::clamp(limit, 1.0, f64(MAX_LIMIT))));
	}
	else if (op == "account")
	{
		if (account())
			answer = AdminAccount(id);
	}
	else if (op == "delete")
	{
		if (account())
			answer = AdminDelete(id, why);
	}
	else if (op == "grant" || op == "ban" || op == "hide" || op == "rename" || op == "signout" ||
	         op == "unlink" || op == "keep")
	{
		sim::Account* changed = account();
		if (changed && op == "grant")
		{
			yyjson_val* resources = yyjson_obj_get(root, "resources");
			f64 amounts[sim::RESOURCES] = {};
			for (u32 r = 0; r < sim::RESOURCES; ++r)
			{
				amounts[r] = std::floor(Number(resources, sim::ResourceId(sim::Resource(r))));
				if (!std::isfinite(amounts[r]) || std::fabs(amounts[r]) > MAX_GRANT)
					why = "an amount out of range";
			}
			std::string line;
			for (u32 r = 0; r < sim::RESOURCES && why.empty(); ++r)
			{
				f64& held = changed->resources.amount[r];
				held = std::max(0.0, held + amounts[r]);
				char part[64];
				std::snprintf(part, sizeof(part), " %s %+.0f", sim::ResourceId(sim::Resource(r)),
				              amounts[r]);
				line += part;
			}
			if (why.empty())
				PH_LOG_INFO("admin: grant to %s:%s", id.c_str(), line.c_str());
		}
		else if (changed && op == "ban")
		{
			changed->banned = Flag(root, "banned");
			PH_LOG_INFO("admin: %s %s", id.c_str(), changed->banned ? "banned" : "unbanned");
			if (changed->banned)
			{
				for (Client& client : clients)
				{
					if (client.account != id)
						continue;
					Notice(client.peer, "error.banned");
					client.account.clear();
					net::Disconnect(listener, client.peer);
				}
			}
		}
		else if (changed && op == "hide")
		{
			changed->hidden = Flag(root, "hidden");
			PH_LOG_INFO("admin: %s %s the leaderboard", id.c_str(),
			            changed->hidden ? "off" : "back on");
		}
		else if (changed && op == "rename")
		{
			const std::string name = sim::CleanText(Text(root, "name"), sim::MAX_NAME_BYTES);
			if (name.empty())
				why = "an empty name";
			else
			{
				PH_LOG_INFO("admin: %s renamed from %s to %s", id.c_str(), changed->name.c_str(),
				            name.c_str());
				changed->name = name;
			}
		}
		else if (changed && op == "signout")
		{
			for (const std::string& key : changed->keys)
				hub.store->RemoveLink(KeyLinkName(key));
			PH_LOG_INFO("admin: %s: %u devices signed out", id.c_str(), u32(changed->keys.size()));
			changed->keys.clear();
		}
		else if (changed && op == "unlink")
		{
			const std::string_view provider = Text(root, "provider");
			const sim::Identity* identity = changed->FindIdentity(provider);
			if (!identity)
				why = "no such sign-in";
			else if (changed->identities.size() == 1 && !hub.keepGuests)
				why = "its last sign-in: delete the account instead";
			else
			{
				hub.store->RemoveLink(LinkName(identity->provider, identity->id));
				PH_LOG_INFO("admin: %s: %s sign-in removed", id.c_str(),
				            identity->provider.c_str());
				std::erase_if(changed->identities, [provider](const sim::Identity& i)
				              { return i.provider == provider; });
			}
		}
		else if (changed && op == "keep")
		{
			// A guest saved without a sign-in, its device's key opening it,
			// until a sign-in joins it (ADR 0016's addendum of 2026-10-06).
			if (!changed->IsGuest())
				why = "it has a sign-in: saved already";
			else if (!changed->kept)
			{
				changed->kept = true;
				if (!summaries.contains(id))
					++accountCount;
				PH_LOG_INFO("admin: %s kept: a guest saved", id.c_str());
			}
		}
		if (changed)
		{
			AdminChanged(id, *changed);
			if (why.empty())
				answer = AdminAccount(id);
			ForgetAccount(id);
		}
	}
	else
		why = "unknown op";

	// The answer, with the request's tag.
	if (answer.empty())
	{
		Out out;
		out.Set(out.Root(), "op", "error");
		out.Set(out.Root(), "error", std::string_view(why.empty() ? "failed" : why));
		answer = out.Write();
	}
	yyjson_val* tag = yyjson_is_obj(root) ? yyjson_obj_get(root, "tag") : nullptr;
	if (tag && answer.size() > 2)
	{
		usize length = 0;
		char* text = yyjson_val_write(tag, 0, &length);
		if (text)
			answer.insert(1, "\"tag\":" + std::string(text, length) + ",");
		std::free(text);
	}
	net::Send(adminListener, peer, answer.data(), u32(answer.size()), net::Delivery::Reliable);
}

std::string Server::AdminStatus()
{
	Out out;
	yyjson_mut_val* root = out.Root();
	out.Set(root, "op", "status");
	out.Set(root, "protocol", sim::PROTOCOL_VERSION);
	char catalog[16];
	std::snprintf(catalog, sizeof(catalog), "%08x", sim::GetCatalog().hash);
	out.Set(root, "catalog", catalog);
	out.Set(root, "started", startedAt);
	out.Set(root, "now", Now());
	out.Set(root, "accounts", accountCount);
	u32 signedIn = 0;
	for (const auto& [id, summary] : summaries)
		signedIn += summary.identities.empty() ? 0 : 1;
	out.Set(root, "signedIn", signedIn);
	out.Set(root, "battles", GetBattleCount());
	out.Set(root, "skirmish", GetPlayerCount());
	yyjson_mut_val* online = out.Array();
	for (const Client& client : clients)
	{
		yyjson_mut_val* entry = out.Object();
		out.Set(entry, "peer", u32(client.peer));
		out.Set(entry, "name", std::string_view(client.name));
		out.Set(entry, "account", std::string_view(client.account));
		const auto found = accounts.find(client.account);
		out.Set(entry, "provider",
		        found == accounts.end() ? std::string_view() : found->second.Provider());
		out.Set(entry, "where",
		        !client.match                                  ? "hub"
		        : client.match->kind == sim::MatchKind::Battle ? "battle"
		                                                       : "skirmish");
		out.Append(online, entry);
	}
	out.Set(root, "online", online);
	yyjson_mut_val* leaders = out.Array();
	const auto& entries = leaderboard.Entries();
	for (u32 i = 0; i < std::min(LEADERS_SHOWN, u32(entries.size())); ++i)
	{
		yyjson_mut_val* entry = out.Object();
		out.Set(entry, "rank", i + 1);
		out.Set(entry, "account", std::string_view(entries[i].id));
		out.Set(entry, "name", std::string_view(entries[i].name));
		out.Set(entry, "ring", entries[i].ring);
		out.Set(entry, "at", entries[i].at);
		out.Set(entry, "provider", std::string_view(entries[i].provider));
		out.Append(leaders, entry);
	}
	out.Set(root, "leaderboard", leaders);
	return out.Write();
}

std::string Server::AdminAccounts(std::string_view query, std::string_view filter, u32 offset,
                                  u32 limit)
{
	// The saved accounts, and the guests in memory.
	struct Item
	{
		std::string id;
		Summary summary;
		bool saved = true;
	};
	std::vector<Item> items;
	const auto wanted = [&](const std::string& id, const Summary& summary)
	{
		if ((filter == "signed" && summary.identities.empty()) ||
		    (filter == "guests" && !summary.identities.empty()) ||
		    (filter == "banned" && !summary.banned) || (filter == "hidden" && !summary.hidden))
			return false;
		if (query.empty() || Contains(summary.name, query) || id.starts_with(query))
			return true;
		return std::any_of(summary.identities.begin(), summary.identities.end(),
		                   [&](const sim::Identity& identity)
		                   { return Contains(identity.id, query); });
	};
	for (const auto& [id, summary] : summaries)
	{
		const auto live = accounts.find(id);
		const Summary current = live == accounts.end() ? summary : SummaryOf(live->second);
		if (wanted(id, current))
			items.push_back({id, current, true});
	}
	for (const auto& [id, account] : accounts)
	{
		if (summaries.contains(id))
			continue;
		const Summary summary = SummaryOf(account);
		if (wanted(id, summary))
			items.push_back({id, summary, false});
	}
	std::sort(items.begin(), items.end(),
	          [](const Item& a, const Item& b)
	          {
				  return a.summary.updated != b.summary.updated
							 ? a.summary.updated > b.summary.updated
							 : a.id < b.id;
			  });
	Out out;
	yyjson_mut_val* root = out.Root();
	out.Set(root, "op", "accounts");
	out.Set(root, "total", u32(items.size()));
	out.Set(root, "offset", offset);
	yyjson_mut_val* list = out.Array();
	for (usize i = offset; i < items.size() && i < usize(offset) + limit; ++i)
	{
		const Item& item = items[i];
		yyjson_mut_val* entry = out.Object();
		out.Set(entry, "account", std::string_view(item.id));
		out.Set(entry, "name", std::string_view(item.summary.name));
		yyjson_mut_val* providers = out.Array();
		for (const sim::Identity& identity : item.summary.identities)
			out.AppendText(providers, identity.provider);
		out.Set(entry, "providers", providers);
		out.Set(entry, "deepest", item.summary.deepest);
		out.Set(entry, "battles", item.summary.battles);
		out.Set(entry, "kills", item.summary.kills);
		out.Set(entry, "created", item.summary.created);
		out.Set(entry, "updated", item.summary.updated);
		out.Set(entry, "banned", item.summary.banned);
		out.Set(entry, "hidden", item.summary.hidden);
		out.Set(entry, "online",
		        std::any_of(clients.begin(), clients.end(),
		                    [&](const Client& c) { return c.account == item.id; }));
		out.Set(entry, "saved", item.saved);
		out.Append(list, entry);
	}
	out.Set(root, "accounts", list);
	return out.Write();
}

std::string Server::AdminAccount(const std::string& id)
{
	const sim::Account* account = LoadAccount(id);
	if (!account)
		return {};
	Out out;
	yyjson_mut_val* root = out.Root();
	out.Set(root, "op", "account");
	out.Set(root, "account", std::string_view(id));
	out.Set(root, "online",
	        std::any_of(clients.begin(), clients.end(),
	                    [&](const Client& c) { return c.account == id; }));
	out.Set(root, "saved", summaries.contains(id));
	out.Set(root, "devices", u32(account->keys.size()));
	out.Set(root, "data", out.Copy(sim::ToJson(*account, false)));
	return out.Write();
}

std::string Server::AdminDelete(const std::string& id, std::string& why)
{
	if (InUse(id))
	{
		why = "online";
		return {};
	}
	const sim::Account* account = LoadAccount(id);
	if (!account || !summaries.contains(id))
	{
		why = "unknown account";
		ForgetAccount(id);
		return {};
	}
	for (const sim::Identity& identity : account->identities)
		hub.store->RemoveLink(LinkName(identity.provider, identity.id));
	for (const std::string& key : account->keys)
		hub.store->RemoveLink(KeyLinkName(key));
	hub.store->Remove(id);
	PH_LOG_INFO("admin: %s (%s) deleted", id.c_str(), account->name.c_str());
	accounts.erase(id);
	summaries.erase(id);
	accountCount = accountCount > 0 ? accountCount - 1 : 0;
	if (leaderboard.Update(id, {}, 0, 0))
		leaderboardChanged = true;
	Out out;
	out.Set(out.Root(), "op", "deleted");
	out.Set(out.Root(), "account", std::string_view(id));
	return out.Write();
}

void Server::AdminChanged(const std::string& id, sim::Account& account)
{
	SaveAccount(id);
	UpdateLeaderboard(id, account);
	for (Client& client : clients)
	{
		if (client.account != id)
			continue;
		client.name = UniqueName(account.name, &client);
		SendProfile(client);
	}
}
} // namespace sn::server
