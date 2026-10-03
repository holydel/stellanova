// stellanova-server, the game's dedicated server (docs/adr/0010-online-server.md):
// one match that every player who connects joins, with the bots its
// players call in (/add_bots; waves with /waves on), native clients over UDP
// and browsers over WebSocket behind a proxy that gives TLS. Logs to its
// output; stops on Ctrl+C or SIGTERM.
//   stellanova-server [--listen "<addresses>"] [--waves] [--stats SECONDS]
//                     [--capture FILE] [--seconds N]
//   --listen         default "udp:0.0.0.0:27015 ws:127.0.0.1:27080"
//   --waves          waves of bots from the start, as in a local skirmish
//   --stats SECONDS  a line on the match and its traffic that often, while
//                    anyone plays (default 60; 0: none)
//   --capture FILE   profile the whole run, the network's rates included:
//                    FILE (Chrome trace JSON) and FILE.csv at the end (Dev
//                    builds; docs/profiling.md)
//   --seconds N      stop after N seconds, as SIGTERM would (captures, tests)
// Debug mode: SIGUSR1 (`systemctl kill -s USR1 stellanova-server`) switches
// the stats to every second, with a line per player; the next one back.

#include <sn/server/server.h>

#include <ph/core/log.h>
#include <ph/core/profile.h>
#include <ph/core/profile_capture.h>
#include <ph/core/time.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

namespace
{
using namespace ph;
using sn::sim::MessageCounts;

std::atomic<bool> gStop{false};
std::atomic<bool> gDebug{false};

void OnSignal(int) { gStop = true; }
[[maybe_unused]] void OnDebugSignal(int) { gDebug.store(!gDebug.load()); }

// What the last stats line read, for the rates since.
struct Readings
{
	u64 atNs = 0;
	u64 tick = 0;
	net::TrafficStats traffic;
	MessageCounts sent;
	MessageCounts received;
	std::unordered_map<net::PeerId, net::TrafficStats> players;
};

// "31.2 KB", "4.5 MB".
std::string Size(u64 bytes)
{
	char text[32];
	if (bytes < 1024 * 1024)
		std::snprintf(text, sizeof(text), "%.1f KB", f64(bytes) / 1024.0);
	else
		std::snprintf(text, sizeof(text), "%.1f MB", f64(bytes) / (1024.0 * 1024.0));
	return text;
}

// "snapshot 30.0/s 851 B, events 1.2/s 64 B": each type that moved, its rate
// and mean size.
std::string ByType(const MessageCounts& now, const MessageCounts& then, f64 seconds)
{
	std::string text;
	for (u32 type = 0; type < MessageCounts::TYPES; ++type)
	{
		const u64 count = now.count[type] - then.count[type];
		if (!count)
			continue;
		char part[96];
		std::snprintf(
			part, sizeof(part), "%s%s %.1f/s %llu B", text.empty() ? "" : ", ",
			sn::sim::MessageName(type), f64(count) / seconds,
			static_cast<unsigned long long>((now.bytes[type] - then.bytes[type]) / count));
		text += part;
	}
	return text.empty() ? "none" : text;
}

// Reads the server's counts; with `log`, a line on the match and its traffic
// since the last reading, and with `perPlayer` one more for each player.
void Read(const sn::server::Server& server, Readings& last, u64 nowNs, bool log, bool perPlayer)
{
	const net::TrafficStats traffic = net::GetServerStats(server.GetListener());
	const sn::sim::World* world = server.GetWorld();
	const u64 tick = world ? world->tick : 0;
	const f64 seconds = std::max(f64(nowNs - last.atNs) * 1e-9, 1e-3);
	const net::TrafficRates rates = net::Rates(last.traffic, traffic, seconds);
	if (log)
	{
		// A match that started over counts its ticks from 0.
		const u64 ticks = tick >= last.tick ? tick - last.tick : tick;
		PH_LOG_INFO(
			"stats: %u players, %.1f ticks/s; out %.0f packets/s, %.1f KB/s (%s); in "
			"%.0f packets/s, %.1f KB/s (%s); %u peers, rtt %.0f ms, %llu resent, %llu B "
			"queued",
			server.GetPlayerCount(), f64(ticks) / seconds, rates.packetsSent,
			rates.bytesSent / 1024.0, ByType(server.GetSent(), last.sent, seconds).c_str(),
			rates.packetsReceived, rates.bytesReceived / 1024.0,
			ByType(server.GetReceived(), last.received, seconds).c_str(), traffic.peers,
			f64(traffic.roundTripMs),
			static_cast<unsigned long long>(traffic.resentFragments - last.traffic.resentFragments),
			static_cast<unsigned long long>(traffic.queuedBytes));
	}
	std::unordered_map<net::PeerId, net::TrafficStats> players;
	for (u32 i = 0; i < server.GetPlayerCount(); ++i)
	{
		const net::PeerId peer = server.GetPlayerPeer(i);
		net::TrafficStats stats;
		if (!net::GetPeerStats(server.GetListener(), peer, stats))
			continue;
		const auto before = last.players.find(peer);
		if (log && perPlayer && before != last.players.end())
		{
			const net::TrafficRates its = net::Rates(before->second, stats, seconds);
			PH_LOG_INFO("stats: player %u: rtt %.0f ms; out %.0f packets/s, %.1f KB/s; in %.0f "
			            "packets/s, %.1f KB/s; %llu resent, %llu B queued",
			            peer, f64(stats.roundTripMs), its.packetsSent, its.bytesSent / 1024.0,
			            its.packetsReceived, its.bytesReceived / 1024.0,
			            static_cast<unsigned long long>(stats.resentFragments -
			                                            before->second.resentFragments),
			            static_cast<unsigned long long>(stats.queuedBytes));
		}
		players[peer] = stats;
	}
	last = {nowNs, tick, traffic, server.GetSent(), server.GetReceived(), std::move(players)};
}
} // namespace

int main(int argc, char** argv)
{
	const char* listen = "udp:0.0.0.0:27015 ws:127.0.0.1:27080";
	const char* capture = nullptr;
	f64 statsSeconds = 60.0;
	f64 runSeconds = 0.0; // 0: until stopped
	sn::server::MatchDesc match;
	match.resetWhenEmpty = true;
	match.waves = false;
	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "--listen") == 0 && i + 1 < argc)
			listen = argv[++i];
		else if (std::strcmp(argv[i], "--waves") == 0)
			match.waves = true;
		else if (std::strcmp(argv[i], "--stats") == 0 && i + 1 < argc)
			statsSeconds = std::atof(argv[++i]);
		else if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc)
			capture = argv[++i];
		else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc)
			runSeconds = std::atof(argv[++i]);
		else
		{
			std::fprintf(stderr, "usage: stellanova-server [--listen \"udp:0.0.0.0:27015 "
			                     "ws:127.0.0.1:27080\"] [--waves] [--stats SECONDS] "
			                     "[--capture FILE] [--seconds N]\n");
			return 2;
		}
	}
	std::signal(SIGINT, OnSignal);
	std::signal(SIGTERM, OnSignal);
#ifdef SIGUSR1
	std::signal(SIGUSR1, OnDebugSignal);
#endif

	sn::server::Server server;
	if (!server.Start(listen, match))
		return 1;
	if (capture && !PH_ENABLE_PROFILING)
	{
		PH_LOG_WARN("stellanova-server: --capture needs a Dev build");
		capture = nullptr;
	}
	if (capture)
		profile::BeginCapture();
	PH_LOG_INFO("stellanova-server: running; Ctrl+C or SIGTERM stops it, SIGUSR1 turns debug "
	            "mode on and off");
	// A millisecond between updates while someone plays: inputs come in and
	// snapshots go out with little delay, and the processor stays idle. Ten
	// while nobody does: a newcomer waits that long at most.
	u64 last = MonotonicNs();
	const u64 endNs = runSeconds > 0.0 ? last + u64(runSeconds * 1e9) : ~u64(0);
	Readings readings;
	readings.atNs = last;
	bool debug = false;
	while (!gStop && last < endNs)
	{
		const u64 now = MonotonicNs();
		server.Update(f32(f64(now - last) * 1e-9));
		last = now;
		if (debug != gDebug.load())
		{
			debug = !debug;
			PH_LOG_INFO("stellanova-server: debug mode %s",
			            debug ? "on: stats every second, per player too" : "off");
		}
		// Quiet while nobody plays and nothing comes in.
		const f64 interval = debug ? 1.0 : statsSeconds;
		if (interval > 0.0 && f64(now - readings.atNs) * 1e-9 >= interval)
		{
			const bool busy = server.GetPlayerCount() > 0 ||
			                  net::GetServerStats(server.GetListener()).packetsReceived !=
			                      readings.traffic.packetsReceived;
			Read(server, readings, now, busy, debug);
		}
		SleepFor(server.GetPlayerCount() > 0 ? 1'000'000 : 10'000'000);
	}
	const net::TrafficStats total = net::GetServerStats(server.GetListener());
	server.Stop();
	PH_LOG_INFO("stellanova-server: stopped; sent %s in %llu packets, received %s in %llu packets",
	            Size(total.bytesSent).c_str(), static_cast<unsigned long long>(total.packetsSent),
	            Size(total.bytesReceived).c_str(),
	            static_cast<unsigned long long>(total.packetsReceived));
	if (capture)
	{
		profile::EndCapture();
		const std::string summary = std::string(capture) + ".csv";
		if (profile::WriteCaptureTrace(capture) && profile::WriteCaptureSummary(summary.c_str()))
			PH_LOG_INFO("stellanova-server: profile written to %s and %s", capture,
			            summary.c_str());
		if (profile::DroppedEvents())
			PH_LOG_WARN("stellanova-server: %llu events did not fit the capture",
			            static_cast<unsigned long long>(profile::DroppedEvents()));
	}
	return 0;
}
