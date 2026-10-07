#include "connection.h"

#include <ph/core/log.h>
#include <ph/core/profile.h>
#include <ph/core/time.h>
#include <ph/debug_ui/debug_ui.h>

#include <algorithm>
#include <cfloat>
#include <cstring>

namespace sn
{
using namespace ph;

void Connection::Open(const char* where, sim::Joining what)
{
	Close();
	address = where;
	joining = what;
	greeted = false;
	early.clear();
	answered = false;
	refused = false;
	sentCounts = {};
	receivedCounts = {};
	netView = {};
	client = net::Connect(where);
}

void Connection::Close()
{
	if (client)
		net::Close(client);
	client = {};
	early.clear();
	outbox.clear();
	inbox.clear();
}

std::string Connection::HostName() const
{
	std::string name = address;
	for (const char* scheme : {"udp:", "wss://", "ws://"})
	{
		if (name.rfind(scheme, 0) == 0)
			name = name.substr(std::strlen(scheme));
	}
	return name.substr(0, std::min(name.find(':'), name.find('/')));
}

f32 Connection::Random()
{
	random = random * 1664525u + 1013904223u;
	return f32(random >> 8) / f32(1u << 24);
}

void Connection::SimulateNetwork(f32 roundTrip, f32 lost)
{
	lagNs = u64(std::max(0.0f, roundTrip) * 0.5e9f);
	loss = std::clamp(lost, 0.0f, 1.0f);
	if (lagNs || loss > 0.0f)
		PH_LOG_INFO("connection: a network %.0f ms slower each round trip, %.0f%% of snapshots "
		            "and inputs lost",
		            f64(lagNs) * 2e-6, f64(loss) * 100.0);
}

void Connection::Post(const std::vector<u8>& bytes, net::Delivery delivery)
{
	if (!client)
		return;
	if (!greeted && sim::TypeOf(bytes.data(), bytes.size()) != sim::MessageType::Hello)
	{
		early.push_back(bytes);
		return;
	}
	sentCounts.Add(bytes.data(), bytes.size());
	if (!lagNs && loss <= 0.0f)
	{
		net::Send(client, bytes.data(), u32(bytes.size()), delivery);
		return;
	}
	if (delivery == net::Delivery::Unreliable && Random() < loss)
		return; // lost on the way
	outbox.push_back({MonotonicNs() + lagNs, bytes, delivery});
}

bool Connection::Receive(Handler handle, void* self)
{
	PH_PROFILE_SCOPE("Connection.Receive");
	if (!client)
		return false;
	const u64 now = MonotonicNs();
	while (!outbox.empty() && outbox.front().dueNs <= now)
	{
		const Held& message = outbox.front();
		net::Send(client, message.bytes.data(), u32(message.bytes.size()), message.delivery);
		outbox.pop_front();
	}
	net::Event event;
	while (net::Poll(client, event))
	{
		if (event.type == net::EventType::Connected)
		{
			sim::Hello hello;
			hello.joining = joining;
			Post(sim::Write(hello), net::Delivery::Reliable);
			greeted = true;
			for (const std::vector<u8>& waiting : early)
				Post(waiting, net::Delivery::Reliable);
			early.clear();
			continue;
		}
		if (event.type == net::EventType::Disconnected)
		{
			PH_LOG_WARN("connection: the server is gone");
			return false;
		}
		receivedCounts.Add(event.data, event.size);
		sim::Refusal answer;
		if (sim::Read(event.data, event.size, answer))
		{
			refused = true;
			refusal = answer.reason;
			PH_LOG_WARN("connection: the server turned us away: %s",
			            answer.reason == sim::RefusalReason::Full ? "it is full"
			                                                      : "another version of the game");
			return false;
		}
		if (!lagNs && loss <= 0.0f)
			handle(self, event.data, event.size);
		else if (sim::TypeOf(event.data, event.size) != sim::MessageType::Snapshot ||
		         Random() >= loss)
			inbox.push_back({now + lagNs, std::vector<u8>(event.data, event.data + event.size),
			                 net::Delivery::Reliable});
	}
	while (!inbox.empty() && inbox.front().dueNs <= MonotonicNs())
	{
		const Held message = std::move(inbox.front());
		inbox.pop_front();
		handle(self, message.bytes.data(), u32(message.bytes.size()));
	}
	return true;
}

void Connection::DrawNetworkWindow(u32 staleSnapshots)
{
#if PH_ENABLE_DEBUG_UI
	if (!client)
		return;
	// Rates twice a second, from the counts.
	const u64 now = MonotonicNs();
	const net::TrafficStats traffic = net::GetClientStats(client);
	NetView& view = netView;
	if (!view.atNs || now - view.atNs >= 500'000'000)
	{
		if (view.atNs)
		{
			const f64 seconds = f64(now - view.atNs) * 1e-9;
			view.rates = net::Rates(view.traffic, traffic, seconds);
			for (u32 type = 0; type < sim::MessageCounts::TYPES; ++type)
			{
				view.sentPerSecond[type] =
					f64(sentCounts.count[type] - view.sent.count[type]) / seconds;
				view.receivedPerSecond[type] =
					f64(receivedCounts.count[type] - view.received.count[type]) / seconds;
			}
			view.receivedKb[view.next] = f32(view.rates.bytesReceived / 1024.0);
			view.next = (view.next + 1) % NetView::HISTORY;
		}
		view.atNs = now;
		view.traffic = traffic;
		view.sent = sentCounts;
		view.received = receivedCounts;
	}

	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 16.0f, 16.0f),
	                        ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
	ImGui::Begin("Network", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::Text("%s%s", address.c_str(), answered ? "" : " (connecting)");
	if (traffic.roundTripMs > 0.0f)
		ImGui::Text("round trip %.0f ms", f64(traffic.roundTripMs));
	else
		ImGui::TextDisabled("round trip: not known here");
	ImGui::Text("out %5.0f packets/s %6.1f KB/s", view.rates.packetsSent,
	            view.rates.bytesSent / 1024.0);
	ImGui::Text("in  %5.0f packets/s %6.1f KB/s", view.rates.packetsReceived,
	            view.rates.bytesReceived / 1024.0);
	ImGui::PlotLines("##in", view.receivedKb, int(NetView::HISTORY), int(view.next),
	                 "KB/s in, the last minute", 0.0f, FLT_MAX, ImVec2(280.0f, 48.0f));
	// Each message type one way: its rate, and its mean size.
	for (u32 type = 0; type < sim::MessageCounts::TYPES; ++type)
	{
		const bool out = sentCounts.count[type] > 0;
		const sim::MessageCounts& counts = out ? sentCounts : receivedCounts;
		if (!counts.count[type])
			continue;
		ImGui::Text("%-8s %s %6.1f/s %5llu B", sim::MessageName(type), out ? "out" : "in ",
		            out ? view.sentPerSecond[type] : view.receivedPerSecond[type],
		            static_cast<unsigned long long>(counts.bytes[type] / counts.count[type]));
	}
	ImGui::Text("%llu fragments resent, %llu B queued",
	            static_cast<unsigned long long>(traffic.resentFragments),
	            static_cast<unsigned long long>(traffic.queuedBytes));
	ImGui::Text("%.1f KB sent, %.1f KB received in all", f64(traffic.bytesSent) / 1024.0,
	            f64(traffic.bytesReceived) / 1024.0);
	ImGui::Text("%u snapshots late or doubled, dropped", staleSnapshots);
	ImGui::End();
#else
	(void)staleSnapshots;
#endif
}
} // namespace sn
