#include "flight.h"

#include "resources.h"

#include <ph/core/log.h>
#include <ph/core/profile.h>
#include <ph/core/time.h>
#include <ph/debug_ui/debug_ui.h>
#include <ph/os/input.h>
#include <ph/platform/platform.h>
#include <ph/render/camera.h>
#include <ph/render/frame.h>
#include <ph/render/sky.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sn
{
namespace
{
using namespace ph;
using namespace ph::os;
using render::PackColor;

constexpr f32 CAMERA_HEIGHT = 40.0f; // m above the ship
constexpr f32 COMBAT_HEIGHT = 54.0f; // with enemies near: more to see
constexpr f32 COMBAT_REACH = 70.0f;  // m: enemies as near as this make a fight
constexpr f32 FOV = 0.8f;            // radians, vertical
// Rocks far below the plane of play, small and dark, so that they read as
// the background: they only show how fast the ship goes.
constexpr u32 SCENERY_COUNT = 350;
constexpr f32 SCENERY_FIELD = 1000.0f; // m: their square, around the start
constexpr f32 STICK_DEAD_ZONE = 0.15f;
constexpr f32 TRIGGER_FIRES = 0.3f; // how far the right trigger goes before it fires
constexpr f32 WAVE_BANNER = 2.5f;   // s a new wave's number shows
constexpr f32 HINT_SECONDS = 10.0f; // s the controls show at the bottom, before they fade
// A correction of our predicted ship fades out of what is drawn by e in
// this many seconds; a jump bigger than SNAP m is the ship put elsewhere (back
// home), shown as it is.
constexpr f32 SMOOTHING = 0.1f;
constexpr f32 SNAP = 4.0f;
// What our predicted shots stop at: an enemy's hull as drawn (bots fly the
// default hull's size).
constexpr f32 TARGET_RADIUS = 1.5f;
// The chat: lines kept, lines shown, and how long they stay before fading.
constexpr usize CHAT_KEPT = 8;
constexpr usize CHAT_SHOWN = 6;
constexpr f32 CHAT_SECONDS = 12.0f;

// The plane of play in the world: x is X, y is -Z (sim/world.h).
Vec3 ToWorld(Vec2 p, f32 height = 0.0f) { return {p.x, height, -p.y}; }

// Steady pseudo-random numbers, so that the looks are the same each time.
f32 Random(u32& state)
{
	state = state * 1664525u + 1013904223u;
	return f32(state >> 8) / f32(1u << 24);
}

// A stick's x without its middle: 0 inside the dead zone, then up to 1.
f32 DeadZone(f32 x)
{
	const f32 size = std::fabs(x);
	if (size <= STICK_DEAD_ZONE)
		return 0.0f;
	return std::copysign(std::min(1.0f, (size - STICK_DEAD_ZONE) / (1.0f - STICK_DEAD_ZONE)), x);
}

// Our side's ships burn orange and trail blue; the enemies' are red.
ShipLook MakeLook(const assets::MeshBounds& bounds, f32 shieldRadius, bool enemy)
{
	ShipLook look;
	look.bounds = bounds;
	look.shieldRadius = shieldRadius;
	if (enemy)
	{
		look.flameHot = PackColor(1.0f, 0.55f, 0.65f, 0.55f);
		look.flameCold = PackColor(0.9f, 0.1f, 0.3f, 0.0f);
		look.core = PackColor(1.0f, 0.85f, 0.9f, 0.85f);
		look.trail = {1.0f, 0.3f, 0.3f};
		look.shield = {1.0f, 0.45f, 0.35f};
		look.flameRate = 0.5f; // many of them: lighter on particles
	}
	else
	{
		look.flameHot = PackColor(1.0f, 0.85f, 0.5f, 0.55f);
		look.flameCold = PackColor(1.0f, 0.3f, 0.05f, 0.0f);
		look.core = PackColor(1.0f, 0.95f, 0.8f, 0.85f);
		look.trail = {0.45f, 0.8f, 1.0f};
		look.shield = {0.45f, 0.85f, 1.0f};
	}
	return look;
}

// A bar of `share` (0-255) over a dark track; premultiplied colors.
void Bar(Ui& ui, f32 x, f32 y, f32 w, f32 h, u8 share, Vec3 color)
{
	ui.Box(x, y, w, h, PackColor(0.02f, 0.03f, 0.05f, 0.6f));
	if (share > 0)
		ui.Box(x, y, w * f32(share) / 255.0f, h, PackColor(color.x, color.y, color.z, 1.0f));
}
} // namespace

void Flight::Enter(Resources& from, const char* address, os::WindowId into)
{
	resources = &from;
	window = into;
	chat.clear();
	typing = false;
	draft.clear();
	client = net::Connect(address);
	ship = 0;
	team = sim::PLAYERS;
	history.clear();
	incoming = std::make_unique<sim::Snapshot>();
	latest = nullptr;
	renderTick = 0.0;
	// A local server's snapshots come every tick, without fail; a distant
	// one's unevenly: three ticks (100 ms) of them in hand smooth that out.
	server = address;
	delayTicks = server.rfind("loopback:", 0) == 0 ? 1.0f : 3.0f;
	welcomed = false;
	rocks.clear(); // the server's, in Welcome
	pieces.clear();
	present.clear();
	effects.Reset();
	ownLook = MakeLook(resources->shipBounds, resources->shieldRadius, false);
	enemyLook = MakeLook(resources->enemyBounds, resources->enemyShieldRadius, true);
	height = CAMERA_HEIGHT;
	kills = 0;
	wave = 0;
	waveShown = WAVE_BANNER;
	flown = 0.0f;
	warmed = false;
	downFor = -1.0f;
	hurt = 0.0f;
	shake = 0.0f;
	sentCounts = {};
	receivedCounts = {};
	staleSnapshots = 0;
	netView = {};
	prediction = {};
	autopiloted = false;
	tickTime = 0.0f;
	ticks = 0;
	smoothing = {};
	smoothingAngle = 0.0f;
	ownShots.clear();
	targets.clear();
	refused = false;
	named = false;
	outbox.clear();
	inbox.clear();

	scenery.clear();
	u32 seed = 7;
	for (u32 i = 0; i < SCENERY_COUNT; ++i)
	{
		Scenery rock;
		rock.position = {(Random(seed) - 0.5f) * SCENERY_FIELD, -120.0f - 130.0f * Random(seed),
		                 (Random(seed) - 0.5f) * SCENERY_FIELD};
		rock.size = 3.0f + 9.0f * Random(seed) * Random(seed);
		rock.spin = (Random(seed) - 0.5f) * 0.6f;
		rock.mesh = u32(Random(seed) * f32(ROCK_MESHES)) % ROCK_MESHES;
		scenery.push_back(rock);
	}

	audio::PlayDesc desc;
	desc.loop = true;
	desc.volume = 0.0f;
	engine = audio::Play(resources->thruster, desc);
}

void Flight::Leave()
{
	if (typing)
		StopTyping();
	audio::Stop(engine);
	net::Close(client);
	client = {};
	history.clear();
	incoming.reset();
	latest = nullptr;
	outbox.clear();
	inbox.clear();
}

bool Flight::OnEvent(const Event& event)
{
	if (event.type == EventType::TouchDown)
		sawTouch = true;
	if (typing)
	{
		OnTyping(event);
		return false;
	}
	// The chat opens with Enter, the Chat button, or a pad's View button (on
	// a Steam Deck in Game Mode, Steam's keyboard comes up with it). Alt+Enter
	// is the shell's fullscreen toggle.
	const bool enter =
		(event.type == EventType::KeyDown && !event.key.repeat && !event.key.mods.alt &&
		 (event.key.scancode == Scancode::Return || event.key.scancode == Scancode::KpEnter)) ||
		(event.type == EventType::GamepadButtonDown && event.gamepad.button == GamepadButton::Back);
	const bool button =
		event.type == EventType::TouchDown && sawTouch && event.touch.x >= chatButton[0] &&
		event.touch.x < chatButton[0] + chatButton[2] && event.touch.y >= chatButton[1] &&
		event.touch.y < chatButton[1] + chatButton[3];
	if ((enter || button) && welcomed)
	{
		StartTyping();
		return false;
	}
	if (event.type == EventType::KeyDown && !event.key.repeat)
		return event.key.scancode == Scancode::Escape || event.key.scancode == Scancode::AcBack;
	if (event.type == EventType::GamepadButtonDown)
		return event.gamepad.button == GamepadButton::Start ||
		       event.gamepad.button == GamepadButton::East;
	return false;
}

void Flight::StartTyping()
{
	typing = true;
	draft.clear();
	os::StartTextInput(window);
}

void Flight::StopTyping()
{
	typing = false;
	draft.clear();
	os::StopTextInput(window);
}

void Flight::OnTyping(const Event& event)
{
	if (event.type == EventType::TextInput)
	{
		const usize length = std::strlen(event.text.text);
		if (draft.size() + length <= sim::MAX_CHAT_BYTES)
			draft.append(event.text.text, length);
		return;
	}
	// A pad's B or View: a cancel.
	if (event.type == EventType::GamepadButtonDown &&
	    (event.gamepad.button == GamepadButton::East ||
	     event.gamepad.button == GamepadButton::Back))
	{
		StopTyping();
		return;
	}
	if (event.type != EventType::KeyDown)
		return;
	switch (event.key.scancode)
	{
		case Scancode::Backspace:
			// The last character: its continuation bytes, then its first.
			while (!draft.empty() && (u8(draft.back()) & 0xC0) == 0x80)
				draft.pop_back();
			if (!draft.empty())
				draft.pop_back();
			break;
		case Scancode::Return:
		case Scancode::KpEnter:
		{
			const sim::Say say{sim::CleanText(draft, sim::MAX_CHAT_BYTES)};
			if (!say.text.empty())
				Post(sim::Write(say), net::Delivery::Reliable);
			StopTyping();
			break;
		}
		case Scancode::Escape:
		case Scancode::AcBack: StopTyping(); break;
		default: break;
	}
}

void Flight::OnChat(const sim::Chat& said)
{
	ChatLine line;
	line.notice = said.notice;
	if (said.notice)
		line.text =
			resources->strings.Format(said.text.c_str(), said.name.c_str(), said.extra.c_str());
	else
	{
		line.text = said.name + ": " + said.text;
		line.own = said.ship == ship;
	}
	chat.push_back(std::move(line));
	if (chat.size() > CHAT_KEPT)
		chat.erase(chat.begin());
}

sim::ShipControls Flight::ReadControls(PixelSize size) const
{
	// No flying while they type a chat line: keys type, and on the Steam Deck
	// the pad and its trackpads work Steam's keyboard.
	sim::ShipControls controls;
	if (typing)
		return controls;
	const auto key = [](Scancode a, Scancode b)
	{ return IsKeyDown(a) || IsKeyDown(b) ? 1.0f : 0.0f; };
	f32 turn = key(Scancode::A, Scancode::Left) - key(Scancode::D, Scancode::Right);
	f32 thrust = key(Scancode::W, Scancode::Up) - key(Scancode::S, Scancode::Down);
	controls.fire = IsKeyDown(Scancode::Space);

	// Pads: the left trigger thrusts (as far as it is pulled), the left
	// bumper reverses at full power, the left stick only turns, and the
	// right trigger fires.
	GamepadState pads[MAX_GAMEPADS];
	const u32 count = GetGamepads(pads, MAX_GAMEPADS);
	for (u32 i = 0; i < count; ++i)
	{
		turn -= DeadZone(pads[i].Axis(GamepadAxis::LeftX));
		thrust += pads[i].Axis(GamepadAxis::LeftTrigger);
		if (pads[i].IsDown(GamepadButton::LeftShoulder))
			thrust -= 1.0f;
		controls.fire = controls.fire || pads[i].Axis(GamepadAxis::RightTrigger) > TRIGGER_FIRES;
	}

	// A finger (or the held mouse) where the ship should go: the ship is in
	// the middle of the screen, so the way there is from the middle. A
	// second finger, or the right button, fires.
	const MouseState mouse = GetMouseState();
	if ((mouse.buttons & MouseButtonBit(MouseButton::Left)) && size.width > 0)
	{
		const f32 dx = mouse.x - 0.5f * f32(size.width);
		const f32 dy = 0.5f * f32(size.height) - mouse.y;
		const f32 distance = std::sqrt(dx * dx + dy * dy);
		if (distance > 0.04f * f32(size.height))
		{
			const sim::ShipState* state = latest ? latest->Find(ship) : nullptr;
			const f32 heading = prediction.IsActive() ? prediction.Now().angle
			                    : state               ? state->angle
			                                          : 0.0f;
			const f32 wanted = std::atan2(-dx, dy);
			const f32 off = sim::WrapAngle(wanted - heading);
			turn += std::clamp(off * 2.5f, -1.0f, 1.0f);
			thrust +=
				std::abs(off) < 0.8f ? std::min(1.0f, distance / (0.3f * f32(size.height))) : 0.1f;
		}
	}
	controls.fire = controls.fire || (mouse.buttons & MouseButtonBit(MouseButton::Right)) != 0 ||
	                GetTouches(nullptr, 0) >= 2;
	controls.turn = std::clamp(turn, -1.0f, 1.0f);
	controls.thrust = std::clamp(thrust, -1.0f, 1.0f);
	return controls;
}

void Flight::SendControls(PixelSize size, f32 dt)
{
	Release(outbox, false);
	if (!ship)
		return;
	// Our ticks at the server's rate, on the frame's time as a local server
	// counts it: each flies our ship at once and goes to the server.
	tickTime += std::min(dt, 0.25f);
	const sim::ShipControls controls = sim::Quantize(ReadControls(size));
	bool ticked = false;
	while (tickTime >= sim::TICK_SECONDS)
	{
		tickTime -= sim::TICK_SECONDS;
		++ticks;
		sim::Shot shot;
		if (prediction.Step(controls, shot))
			Shoot(shot);
		ticked = true;
	}
	if (ticked)
		SendInput();
	if (welcomed)
		SendName();
	// The engine hums only while the ship flies.
	const f32 push = downFor < 0.0f ? std::abs(controls.thrust) : 0.0f;
	audio::SetVolume(engine, downFor < 0.0f ? 0.15f + 0.55f * push : 0.0f);
	audio::SetPitch(engine, 0.8f + 0.3f * push);
}

void Flight::SendName()
{
	// Steam's name for us, once Steam (connected in the background) has
	// one; until then the server calls us "Pilot N".
	const sim::Name name{sim::CleanText(platform::GetPlayerName(), sim::MAX_NAME_BYTES)};
	if (named || name.name.empty())
		return;
	Post(sim::Write(name), net::Delivery::Reliable);
	named = true;
}

void Flight::SendInput()
{
	// The newest few not yet applied, so that a lost message costs nothing.
	const std::vector<Prediction::Controls>& pending = prediction.Pending();
	if (pending.empty())
		return;
	sim::Input input;
	input.count = std::min(u32(pending.size()), sim::MAX_INPUTS);
	input.last = pending.back().number;
	for (u32 i = 0; i < input.count; ++i)
		input.controls[i] = pending[pending.size() - input.count + i].controls;
	Post(sim::Write(input), net::Delivery::Unreliable);
}

void Flight::Shoot(const sim::Shot& shot)
{
	// It flew a tick already, in the tick it was fired.
	ownShots.push_back(
		{shot.position, shot.velocity, f64(ticks) - 1.0, f64(ticks) - 1.0, shot.life, shot.radius});
	resources->PlayEffect(resources->fire, 0.3f);
}

void Flight::Post(const std::vector<u8>& bytes, net::Delivery delivery)
{
	sentCounts.Add(bytes.data(), bytes.size());
	if (!lagNs && loss <= 0.0f)
	{
		net::Send(client, bytes.data(), u32(bytes.size()), delivery);
		return;
	}
	if (delivery == net::Delivery::Unreliable && Random(random) < loss)
		return; // lost on the way
	outbox.push_back({MonotonicNs() + lagNs, bytes, delivery});
}

void Flight::Release(std::deque<Held>& held, bool inbound)
{
	const u64 now = MonotonicNs();
	while (!held.empty() && held.front().dueNs <= now)
	{
		const Held message = std::move(held.front());
		held.pop_front();
		if (inbound)
			OnMessage(message.bytes.data(), u32(message.bytes.size()));
		else
			net::Send(client, message.bytes.data(), u32(message.bytes.size()), message.delivery);
	}
}

void Flight::SimulateNetwork(f32 roundTrip, f32 lost)
{
	lagNs = u64(std::max(0.0f, roundTrip) * 0.5e9f);
	loss = std::clamp(lost, 0.0f, 1.0f);
	if (lagNs || loss > 0.0f)
		PH_LOG_INFO("flight: a network %.0f ms slower each round trip, %.0f%% of snapshots "
		            "and inputs lost",
		            f64(lagNs) * 2e-6, f64(loss) * 100.0);
}

void Flight::OwnPose(Vec2& position, f32& angle) const
{
	const sim::Ship& at = prediction.Now();
	const sim::Ship& before = prediction.Before();
	const f32 alpha = std::clamp(tickTime / sim::TICK_SECONDS, 0.0f, 1.0f);
	position = before.position + (at.position - before.position) * alpha + smoothing;
	angle = before.angle + sim::WrapAngle(at.angle - before.angle) * alpha + smoothingAngle;
}

void Flight::Correct(const sim::ShipState& own)
{
	if (autopiloted)
		return;
	const bool was = prediction.IsActive();
	Vec2 wasPosition;
	f32 wasAngle = 0.0f;
	if (was)
		OwnPose(wasPosition, wasAngle);
	prediction.Correct(own, latest->cooldown, latest->input);
	Vec2 position;
	f32 angle = 0.0f;
	if (prediction.IsActive())
		OwnPose(position, angle);
	// What is drawn stays where it was, and the jump fades out of it.
	const Vec2 jump = wasPosition - position;
	if (!was || !prediction.IsActive() || sim::Length(jump) > SNAP)
	{
		smoothing = {};
		smoothingAngle = 0.0f;
		return;
	}
	smoothing = smoothing + jump;
	smoothingAngle += sim::WrapAngle(wasAngle - angle);
}

bool Flight::Receive()
{
	PH_PROFILE_SCOPE("Flight.Receive");
	net::Event event;
	while (net::Poll(client, event))
	{
		if (event.type == net::EventType::Connected)
		{
			Post(sim::Write(sim::Hello{}), net::Delivery::Reliable);
			SendName();
			continue;
		}
		if (event.type == net::EventType::Disconnected)
		{
			PH_LOG_WARN("flight: the server is gone");
			return false;
		}
		receivedCounts.Add(event.data, event.size);
		if (!lagNs && loss <= 0.0f)
			OnMessage(event.data, event.size);
		else if (sim::TypeOf(event.data, event.size) != sim::MessageType::Snapshot ||
		         Random(random) >= loss)
			inbox.push_back({MonotonicNs() + lagNs,
			                 std::vector<u8>(event.data, event.data + event.size),
			                 net::Delivery::Reliable});
	}
	Release(inbox, true);
	return !refused;
}

void Flight::OnMessage(const u8* data, u32 size)
{
	switch (sim::TypeOf(data, size))
	{
		case sim::MessageType::Welcome:
		{
			sim::Welcome welcome;
			if (sim::Read(data, size, welcome))
				OnWelcome(welcome);
			break;
		}
		case sim::MessageType::Events:
		{
			sim::Events events;
			if (sim::Read(data, size, events))
				OnEvents(events);
			break;
		}
		case sim::MessageType::Chat:
		{
			sim::Chat said;
			if (sim::Read(data, size, said))
				OnChat(said);
			break;
		}
		case sim::MessageType::Refusal:
		{
			sim::Refusal answer;
			if (!sim::Read(data, size, answer))
				break;
			refused = true;
			refusal = answer.reason;
			PH_LOG_WARN("flight: the server turned us away: %s",
			            answer.reason == sim::RefusalReason::Full ? "it is full"
			                                                      : "another protocol version");
			break;
		}
		case sim::MessageType::Snapshot:
			if (!sim::Read(data, size, *incoming))
				break;
			// Late or doubled ones (UDP reorders) are dropped: the newer
			// replaces them.
			if (latest && incoming->tick <= latest->tick)
				++staleSnapshots;
			else
			{
				std::unique_ptr<sim::Snapshot> spare;
				if (history.size() == HISTORY)
				{
					spare = std::move(history.front());
					history.erase(history.begin());
				}
				history.push_back(std::move(incoming));
				incoming = spare ? std::move(spare) : std::make_unique<sim::Snapshot>();
				latest = history.back().get();
				if (history.size() == 1)
					renderTick = f64(latest->tick) - delayTicks;
				OnSnapshot();
			}
			break;
		default: break;
	}
}

void Flight::DrawNetworkWindow()
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
	ImGui::Text("%s%s", server.c_str(), welcomed ? "" : " (connecting)");
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
	// How far behind the newest snapshot ships are drawn, from how many.
	if (latest)
		ImGui::Text("drawn %.0f ms behind the newest of %u snapshots",
		            (f64(latest->tick) - renderTick) * sim::TICK_SECONDS * 1000.0,
		            u32(history.size()));
	ImGui::Text("%u snapshots late or doubled, dropped", staleSnapshots);
	ImGui::End();
#endif
}

void Flight::OnWelcome(const sim::Welcome& welcome)
{
	ship = welcome.ship;
	respawn = welcome.respawn;
	welcomed = true;
	prediction.Reset(welcome.hull, welcome.weapon, welcome.rocks);
	autopiloted = welcome.autopilot;
	tickTime = 0.0f;
	ticks = 0;
	ownShots.clear();
	smoothing = {};
	smoothingAngle = 0.0f;
	rocks.clear();
	u32 seed = 11;
	for (const sim::Rock& from : welcome.rocks)
	{
		Rock rock;
		rock.position = from.position;
		rock.radius = from.radius;
		rock.broken = from.health <= 0.0f;
		rock.mesh = u32(Random(seed) * f32(ROCK_MESHES)) % ROCK_MESHES;
		rock.height = 0.6f + 0.4f * Random(seed);
		rock.spin = (Random(seed) - 0.5f) * 0.6f;
		rock.phase = Random(seed) * 2.0f * PI;
		rocks.push_back(rock);
	}
	PH_LOG_INFO("flight: ship %u in a field of %zu rocks", ship, rocks.size());
}

void Flight::OnSnapshot()
{
	if (const sim::ShipState* own = latest->Find(ship))
	{
		team = own->team;
		Correct(*own);
	}
	// Ships that were not flying in the last snapshot have just arrived: a
	// wave's bots, or ours, at the start and back from its wreck.
	bool arrived = false;
	Vec2 where = listener;
	std::vector<u32> flying;
	for (u32 i = 0; i < latest->count; ++i)
	{
		const sim::ShipState& state = latest->ships[i];
		if (state.health == 0)
			continue;
		flying.push_back(state.id);
		if (std::find(present.begin(), present.end(), state.id) != present.end())
			continue;
		effects.WarpIn(ToWorld(state.position, 0.3f),
		               state.team == team ? ownLook.trail : enemyLook.trail);
		arrived = true;
		where = state.position;
	}
	present.swap(flying);
	if (arrived)
		PlayAt(resources->warp, 0.5f, where);
	if (latest->wave != wave)
	{
		wave = latest->wave;
		waveShown = 0.0f;
	}
}

u8 Flight::TeamOf(u32 id) const
{
	const sim::ShipState* state = latest ? latest->Find(id) : nullptr;
	return state ? state->team : sim::BOTS;
}

void Flight::OnEvents(const sim::Events& events)
{
	for (const sim::EventState& event : events.events)
	{
		const bool ours = event.ship == ship;
		// The shield lights up on the side that was hit.
		const auto light = [&](f32 strength)
		{
			if (const sim::ShipState* state = latest ? latest->Find(event.ship) : nullptr)
				effects.Bump(event.ship, ToWorld(event.position) - ToWorld(state->position),
				             strength);
		};
		switch (event.type)
		{
			case sim::EventType::Fired:
				if (ours)
				{
					if (!prediction.IsActive()) // else Shoot played it already
						resources->PlayEffect(resources->fire, 0.3f);
				}
				else
					PlayAt(TeamOf(event.ship) == team ? resources->fire : resources->enemyFire,
					       0.3f, event.position);
				break;
			case sim::EventType::RockHit:
				effects.Sparks(ToWorld(event.position, 0.3f));
				PlayAt(resources->hit, 0.3f, event.position);
				break;
			case sim::EventType::RockBroken:
				prediction.BreakRock(event.rock);
				if (event.rock < rocks.size())
				{
					Rock& rock = rocks[event.rock];
					rock.broken = true;
					Burst(rock.position, 5 + u32(rock.radius * 1.5f), 0.3f * rock.radius,
					      5.0f + rock.radius, false);
				}
				PlayAt(resources->explode, std::min(1.0f, 0.4f + 0.08f * event.strength),
				       event.position);
				break;
			case sim::EventType::ShipBumped:
				light(event.strength);
				if (!ours)
					break;
				shake = std::min(1.0f, shake + event.strength / 20.0f);
				resources->PlayEffect(resources->bump, std::min(1.0f, event.strength / 15.0f));
				break;
			case sim::EventType::ShieldHit:
				light(8.0f);
				if (ours)
				{
					shake = std::min(1.0f, shake + 0.12f);
					resources->PlayEffect(resources->shieldHit, 0.5f);
				}
				else
					PlayAt(resources->shieldHit, 0.35f, event.position);
				break;
			case sim::EventType::HullHit:
				effects.Sparks(ToWorld(event.position, 0.3f), 6);
				if (ours)
				{
					shake = std::min(1.0f, shake + 0.35f);
					hurt = 1.0f;
					resources->PlayEffect(resources->hullHit, 0.7f);
				}
				else
					PlayAt(resources->hullHit, 0.45f, event.position);
				break;
			case sim::EventType::ShipDestroyed:
				effects.Explosion(ToWorld(event.position, 0.3f), std::max(1.0f, event.strength));
				Burst(event.position, 10, 0.35f, 9.0f, true);
				PlayAt(resources->blast, ours ? 1.0f : 0.8f, event.position);
				if (ours)
				{
					downFor = 0.0f;
					shake = 1.0f;
				}
				else if (event.other == ship)
					++kills;
				break;
		}
	}
}

void Flight::Burst(Vec2 at, u32 count, f32 size, f32 speed, bool metal)
{
	for (u32 i = 0; i < count; ++i)
	{
		const f32 angle = Random(random) * 2.0f * PI;
		const f32 pace = speed * (0.4f + 0.6f * Random(random));
		Piece piece;
		piece.position = ToWorld(at, (Random(random) - 0.5f) * size);
		piece.velocity = {std::cos(angle) * pace, (Random(random) - 0.3f) * 0.5f * pace,
		                  std::sin(angle) * pace};
		piece.size = size * (0.5f + Random(random));
		piece.spin = (Random(random) - 0.5f) * 8.0f;
		piece.lifetime = 0.9f + 0.6f * Random(random);
		piece.life = piece.lifetime;
		piece.mesh = u32(Random(random) * f32(ROCK_MESHES)) % ROCK_MESHES;
		piece.metal = metal;
		pieces.push_back(piece);
	}
}

void Flight::PlayAt(audio::Sound sound, f32 volume, Vec2 at) const
{
	const Vec2 off = at - listener;
	resources->PlayEffect(sound, volume / (1.0f + sim::Length(off) / 30.0f),
	                      std::clamp(off.x / 40.0f, -1.0f, 1.0f));
}

// Each part is a CPU scope and a GPU label: both show in pith's profile
// captures (and the labels in RenderDoc, PIX and Nsight).
void Flight::Draw(rhi::CommandList& commands, const render::FrameTime& time, Ui& ui)
{
	PH_PROFILE_SCOPE("Flight.Draw");
	const f32 dt = f32(time.delta);
	const f32 t = f32(time.seconds);
	if (!latest || !latest->Find(ship))
	{
		DrawConnecting(commands, time, ui);
		return; // not in the game yet
	}

	// The tick drawn: behind the newest snapshot by the delay, moving with
	// the frame's time, pulled gently back to the delay as snapshots come.
	renderTick += f64(dt) * sim::TICK_RATE;
	const f64 target = f64(latest->tick) - delayTicks;
	if (std::abs(renderTick - target) > 4.0)
		renderTick = target;
	else
		renderTick += (target - renderTick) * std::min(1.0, f64(dt) * 2.0);
	// The two snapshots around it: the newest alone past them.
	const sim::Snapshot* from = history.front().get();
	const sim::Snapshot* to = from;
	for (usize i = 0; i < history.size(); ++i)
	{
		if (f64(history[i]->tick) > renderTick)
		{
			to = history[i].get();
			break;
		}
		from = to = history[i].get();
	}
	const f32 alpha =
		to->tick > from->tick
	        ? std::clamp(f32((renderTick - f64(from->tick)) / f64(to->tick - from->tick)), 0.0f,
	                     1.0f)
	        : 0.0f;
	const sim::ShipState* now = to->Find(ship);
	if (!now)
		now = latest->Find(ship);
	if (now->health > 0)
		downFor = -1.0f;
	else
		downFor = std::max(downFor, 0.0f) + dt;

	// Between the two; from the newer alone for a ship that just arrived.
	const auto pose = [&](const sim::ShipState& state, Vec2& position, f32& angle)
	{
		const sim::ShipState* before = from->Find(state.id);
		if (!before || before->health == 0 || from == to)
		{
			position = state.position;
			angle = state.angle;
			return;
		}
		position = before->position + (state.position - before->position) * alpha;
		angle = before->angle + sim::WrapAngle(state.angle - before->angle) * alpha;
	};
	// Ours where the prediction has it, a correction fading out.
	const f32 fade = std::exp(-dt / SMOOTHING);
	smoothing = smoothing * fade;
	smoothingAngle *= fade;
	const bool predicted = prediction.IsActive() && !autopiloted;
	Vec2 position;
	f32 angle = 0.0f;
	if (predicted)
		OwnPose(position, angle);
	else
		pose(*now, position, angle);

	// From above, screen up along the plane's y (-Z), higher while enemies
	// are near; a bump shakes it.
	bool fighting = false;
	for (u32 i = 0; i < to->count; ++i)
	{
		const sim::ShipState& other = to->ships[i];
		fighting |= other.team != team && other.health > 0 &&
		            sim::Length(other.position - now->position) < COMBAT_REACH;
	}
	height += ((fighting ? COMBAT_HEIGHT : CAMERA_HEIGHT) - height) * std::min(1.0f, 1.2f * dt);
	listener = position;
	const Vec3 center = ToWorld(position);
	shake = std::max(0.0f, shake - 3.0f * dt);
	const Vec3 jolt = {(Random(random) - 0.5f) * shake, 0.0f, (Random(random) - 0.5f) * shake};
	eye = Vec3{center.x, height, center.z} + jolt;
	const Mat4 view = LookAt(eye, center + jolt, {0.0f, 0.0f, -1.0f});
	render::FrameData frame = render::MakeFrameData3D(commands, time, view, eye, FOV);
	// A low sun from the upper left: shapes read better than under one from
	// straight above.
	constexpr f32 SUN_HEIGHT = 0.55f; // radians above the plane
	constexpr f32 SUN_YAW = -2.3f;
	frame.sunDirection[0] = std::cos(SUN_HEIGHT) * std::sin(SUN_YAW);
	frame.sunDirection[1] = std::sin(SUN_HEIGHT);
	frame.sunDirection[2] = std::cos(SUN_HEIGHT) * std::cos(SUN_YAW);
	render::SetFrameData(commands, frame);

	// What the camera sees of the plane, and of the scenery below it.
	const f32 aspect =
		commands.size.height > 0 ? f32(commands.size.width) / f32(commands.size.height) : 1.0f;
	const f32 reach = std::tan(0.5f * FOV);
	const auto seen = [&](Vec3 at, f32 depth, f32 radius)
	{
		return std::abs(at.x - center.x) <= reach * aspect * depth + radius &&
		       std::abs(at.z - center.z) <= reach * depth + radius;
	};

	// Ships, banked into turns a little; ours and our side's as they are,
	// the enemies' red. Wrecks have broken apart already.
	effects.Begin(dt);
	marks.clear();
	targets.clear();
	{
		PH_PROFILE_SCOPE("Flight.Ships");
		rhi::DebugLabelScope label(commands, "ships");
		for (u32 i = 0; i < to->count; ++i)
		{
			const sim::ShipState& state = to->ships[i];
			if (state.health == 0)
				continue;
			Vec2 at;
			f32 heading = 0.0f;
			Vec2 velocity = state.velocity;
			sim::ShipControls looks = state.controls;
			if (predicted && state.id == ship)
			{
				at = position;
				heading = angle;
				velocity = prediction.Now().velocity;
				looks = prediction.Now().controls;
			}
			else
				pose(state, at, heading);
			const bool enemy = state.team != team;
			if (enemy)
			{
				marks.push_back({at, state.health, state.shield});
				targets.push_back(at);
			}
			const Mat4 model =
				Translation(ToWorld(at)) * RotationY(heading) * RotationZ(-0.5f * looks.turn);
			if (seen(ToWorld(at), height, 4.0f))
				render::DrawMesh(commands, enemy ? resources->enemy : resources->ship,
				                 enemy ? resources->enemyMaterial : resources->material, model);
			effects.Ship(state.id, enemy ? enemyLook : ownLook, model,
			             Vec3{velocity.x, 0.0f, -velocity.y}, looks.thrust,
			             1.0f - f32(state.health) / 255.0f);
		}
	}

	{
		PH_PROFILE_SCOPE("Flight.Rocks");
		rhi::DebugLabelScope label(commands, "rocks");
		for (const Rock& rock : rocks)
		{
			const Vec3 at = ToWorld(rock.position);
			if (rock.broken || !seen(at, height, rock.radius))
				continue;
			const f32 scale = rock.radius / resources->rockExtents[rock.mesh];
			render::DrawMesh(commands, resources->rocks[rock.mesh], resources->rockMaterial,
			                 Translation(at) * RotationY(rock.phase + rock.spin * t) *
			                     Scale({scale, scale * rock.height, scale}));
		}
	}
	{
		PH_PROFILE_SCOPE("Flight.Scenery");
		rhi::DebugLabelScope label(commands, "scenery");
		for (const Scenery& rock : scenery)
		{
			if (!seen(rock.position, height - rock.position.y, rock.size))
				continue;
			const f32 scale = rock.size / resources->rockExtents[rock.mesh];
			render::DrawMesh(commands, resources->rocks[rock.mesh], resources->sceneryMaterial,
			                 Translation(rock.position) * RotationY(rock.spin * t) *
			                     RotationX(0.7f * rock.spin * t) * Scale({scale, scale, scale}));
		}
	}

	// Shots, where they are at the tick drawn, stretched along their way;
	// ours orange, the enemies' red.
	const f32 since = f32((renderTick - f64(to->tick)) * sim::TICK_SECONDS);
	{
		PH_PROFILE_SCOPE("Flight.Shots");
		rhi::DebugLabelScope label(commands, "shots and pieces");
		for (u32 i = 0; i < to->shotCount; ++i)
		{
			const sim::ShotState& shot = to->shots[i];
			const Vec2 at = shot.position + shot.velocity * since;
			const f32 heading = std::atan2(-shot.velocity.x, shot.velocity.y);
			render::DrawMesh(
				commands, resources->bolt,
				shot.team == team ? resources->boltMaterial : resources->enemyBoltMaterial,
				Translation(ToWorld(at, 0.3f)) * RotationY(heading) * Scale({0.22f, 0.22f, 2.4f}));
		}

		// Ours, from the prediction, at our own tick drawn: each stops at the
		// first rock or enemy on its way, as drawn; the server's events tell
		// what it hit.
		const f64 tick =
			f64(ticks) - 1.0 + f64(std::clamp(tickTime / sim::TICK_SECONDS, 0.0f, 1.0f));
		for (auto shot = ownShots.begin(); shot != ownShots.end();)
		{
			const auto where = [&](f64 when)
			{ return shot->from + shot->velocity * f32((when - shot->born) * sim::TICK_SECONDS); };
			const Vec2 start = where(shot->drawn);
			const Vec2 path = where(tick) - start;
			bool hit = f32((tick - shot->born) * sim::TICK_SECONDS) > shot->life;
			for (const Rock& rock : rocks)
			{
				hit =
					hit || (!rock.broken && sim::SweepContact(start, path, rock.position,
					                                          rock.radius + shot->radius) >= 0.0f);
			}
			for (const Vec2 enemyAt : targets)
				hit = hit ||
				      sim::SweepContact(start, path, enemyAt, TARGET_RADIUS + shot->radius) >= 0.0f;
			if (hit)
			{
				shot = ownShots.erase(shot);
				continue;
			}
			shot->drawn = tick;
			const Vec2 at = start + path;
			render::DrawMesh(commands, resources->bolt, resources->boltMaterial,
			                 Translation(ToWorld(at, 0.3f)) *
			                     RotationY(sim::AngleOf(shot->velocity)) *
			                     Scale({0.22f, 0.22f, 2.4f}));
			++shot;
		}

		// Pieces of broken rocks and ships fly, slow down and shrink away.
		for (Piece& piece : pieces)
		{
			piece.life -= dt;
			piece.position = piece.position + piece.velocity * dt;
			piece.velocity = piece.velocity * std::max(0.0f, 1.0f - 0.8f * dt);
		}
		pieces.erase(std::remove_if(pieces.begin(), pieces.end(),
		                            [](const Piece& piece) { return piece.life <= 0.0f; }),
		             pieces.end());
		for (const Piece& piece : pieces)
		{
			const f32 left = piece.life / piece.lifetime;
			const f32 size = piece.size * std::min(1.0f, left * 3.0f);
			const Mat4 turn = Translation(piece.position) * RotationY(piece.spin * t) *
			                  RotationX(0.6f * piece.spin * t);
			if (piece.metal)
				render::DrawMesh(commands, resources->bolt, resources->debrisMaterial,
				                 turn * Scale({size, 0.6f * size, 1.4f * size}));
			else
			{
				const f32 scale = size / resources->rockExtents[piece.mesh];
				render::DrawMesh(commands, resources->rocks[piece.mesh], resources->rockMaterial,
				                 turn * Scale({scale, scale, scale}));
			}
		}
	}
	if (resources->sky)
	{
		rhi::DebugLabelScope label(commands, "sky");
		render::DrawSky(commands, resources->sky, frame, 1.0f);
	}
	// Glows last: they blend over the scene and the sky.
	{
		PH_PROFILE_SCOPE("Flight.Effects");
		rhi::DebugLabelScope label(commands, "effects");
		effects.Draw(commands, *resources, render::GetCameraAxes(view));
	}

	PH_PROFILE_SCOPE("Flight.Hud");
	rhi::DebugLabelScope label(commands, "hud");
	ui.Begin(commands, time, *resources);
	if (!warmed)
		WarmHud(ui);
	DrawHud(ui, *now, dt);
	DrawChat(ui, dt);
	ui.End(commands);
}

void Flight::DrawConnecting(rhi::CommandList& commands, const render::FrameTime& time, Ui& ui)
{
	if (server.rfind("loopback:", 0) == 0)
		return; // a frame or two at most
	// The server's name alone: wos-observer.com, 127.0.0.1.
	std::string name = server;
	for (const char* scheme : {"udp:", "wss://", "ws://"})
	{
		if (name.rfind(scheme, 0) == 0)
			name = name.substr(std::strlen(scheme));
	}
	name = name.substr(0, std::min(name.find(':'), name.find('/')));
	ui.Begin(commands, time, *resources);
	TextLook look;
	look.size = 24.0f;
	look.align = Align::Center;
	look.color = PackColor(0.6f, 0.68f, 0.8f);
	ui.Text(resources->strings.Format("flight.connecting", name.c_str()).c_str(), 0.5f * ui.Width(),
	        340.0f, look);
	ui.End(commands);
}

void Flight::WarmHud(Ui& ui)
{
	PH_PROFILE_SCOPE("Flight.WarmHud");
	warmed = true;
	StringTable& strings = resources->strings;
	TextLook look;
	look.size = 22.0f;
	ui.Measure(strings.Format("flight.speed", "0123456789").c_str(), look);
	ui.Measure(strings.Format("flight.back", "0123456789").c_str(), look);
	look.size = 18.0f;
	ui.Measure(strings.Format("flight.enemies", "0123456789").c_str(), look);
	ui.Measure(strings.Format("flight.kills", "0123456789").c_str(), look);
	look.size = 14.0f;
	ui.Measure(strings.Get("flight.shield"), look);
	ui.Measure(strings.Get("flight.hull"), look);
	TextLook bold;
	bold.bold = true;
	bold.size = 56.0f;
	ui.Measure(strings.Format("flight.wave", "0123456789").c_str(), bold);
	bold.size = 52.0f;
	ui.Measure(strings.Get("flight.destroyed"), bold);
}

void Flight::DrawHud(Ui& ui, const sim::ShipState& own, f32 dt)
{
	const f32 width = ui.Width();
	StringTable& strings = resources->strings;
	char number[16];

	// Red at the screen's edges after a hull hit, fading.
	hurt = std::max(0.0f, hurt - 2.0f * dt);
	if (hurt > 0.0f)
	{
		constexpr f32 BAND = 14.0f;
		for (u32 band = 0; band < 5; ++band)
		{
			const f32 a = hurt * 0.3f * (1.0f - f32(band) / 5.0f);
			const u32 red = PackColor(0.55f * a, 0.03f * a, 0.02f * a, a);
			const f32 in = BAND * f32(band);
			ui.Box(in, in, width - 2.0f * in, BAND, red);
			ui.Box(in, Ui::HEIGHT - in - BAND, width - 2.0f * in, BAND, red);
			ui.Box(in, in + BAND, BAND, Ui::HEIGHT - 2.0f * (in + BAND), red);
			ui.Box(width - in - BAND, in + BAND, BAND, Ui::HEIGHT - 2.0f * (in + BAND), red);
		}
	}

	// Enemies: a bar over the damaged ones on screen, a mark at the edge
	// toward the others.
	const f32 perMeter = 0.5f * Ui::HEIGHT / (height * std::tan(0.5f * FOV));
	const Vec2 middle = {0.5f * width, 0.5f * Ui::HEIGHT};
	const u32 hostile = PackColor(1.0f, 0.3f, 0.25f, 0.9f);
	u32 enemies = 0;
	for (const Mark& mark : marks)
	{
		++enemies;
		const Vec2 at = {middle.x + (mark.position.x - eye.x) * perMeter,
		                 middle.y + (-mark.position.y - eye.z) * perMeter};
		const Vec2 off = at - middle;
		constexpr f32 EDGE = 36.0f; // units inside the screen's edge
		if (std::abs(off.x) < middle.x - 8.0f && std::abs(off.y) < middle.y - 8.0f)
		{
			if (mark.health == 255 && mark.shield == 255)
				continue;
			const f32 barWidth = 40.0f;
			const f32 top = at.y - 2.4f * perMeter - 8.0f;
			Bar(ui, at.x - 0.5f * barWidth, top, barWidth, 3.0f, mark.shield, {1.0f, 0.55f, 0.45f});
			Bar(ui, at.x - 0.5f * barWidth, top + 4.0f, barWidth, 3.0f, mark.health,
			    {0.95f, 0.2f, 0.15f});
			continue;
		}
		const f32 scale =
			std::min(std::abs(off.x) > 1e-3f ? (middle.x - EDGE) / std::abs(off.x) : 1e9f,
			         std::abs(off.y) > 1e-3f ? (middle.y - EDGE) / std::abs(off.y) : 1e9f);
		const Vec2 tip = middle + off * scale;
		const f32 length = sim::Length(off);
		const Vec2 way = off * (1.0f / length);
		const Vec2 across = {-way.y, way.x};
		constexpr f32 SIZE = 16.0f;
		ui.Line(tip, tip - way * SIZE + across * (0.6f * SIZE), 4.0f, hostile);
		ui.Line(tip, tip - way * SIZE - across * (0.6f * SIZE), 4.0f, hostile);
	}

	// Our speed, shield and hull.
	TextLook look;
	look.size = 22.0f;
	look.color = PackColor(0.85f, 0.9f, 1.0f);
	const f32 speed = sim::Length(own.velocity);
	std::snprintf(number, sizeof(number), "%.0f", speed);
	ui.Text(strings.Format("flight.speed", number).c_str(), 32.0f, 28.0f, look);
	TextLook label;
	label.size = 14.0f;
	label.color = PackColor(0.55f, 0.6f, 0.7f);
	Bar(ui, 32.0f, 64.0f, 220.0f, 7.0f, own.shield, {0.35f, 0.75f, 1.0f});
	ui.Text(strings.Get("flight.shield"), 262.0f, 58.0f, label);
	Bar(ui, 32.0f, 80.0f, 220.0f, 7.0f, own.health, {1.0f, 0.55f, 0.2f});
	ui.Text(strings.Get("flight.hull"), 262.0f, 74.0f, label);

	// The wave, the enemies left, and our kills.
	look.align = Align::Right;
	TextLook small = look;
	small.size = 18.0f;
	if (wave > 0)
	{
		std::snprintf(number, sizeof(number), "%u", wave);
		ui.Text(strings.Format("flight.wave", number).c_str(), width - 32.0f, 28.0f, look);
	}
	// Bots called in without waves count too.
	if (wave > 0 || enemies > 0)
	{
		std::snprintf(number, sizeof(number), "%u", enemies);
		ui.Text(strings.Format("flight.enemies", number).c_str(), width - 32.0f, 60.0f, small);
	}
	std::snprintf(number, sizeof(number), "%u", kills);
	ui.Text(strings.Format("flight.kills", number).c_str(), width - 32.0f, 86.0f, small);

	// A new wave's number, big for a moment.
	waveShown += dt;
	if (wave > 0 && waveShown < WAVE_BANNER)
	{
		const f32 fade = std::min({1.0f, waveShown / 0.25f, (WAVE_BANNER - waveShown) / 0.6f});
		TextLook banner;
		banner.size = 56.0f;
		banner.bold = true;
		banner.align = Align::Center;
		banner.color = PackColor(1.0f, 0.55f, 0.45f, fade);
		banner.glow = 0.25f;
		banner.glowColor = PackColor(0.8f, 0.1f, 0.05f, 0.6f * fade);
		std::snprintf(number, sizeof(number), "%u", wave);
		ui.Text(strings.Format("flight.wave", number).c_str(), middle.x, 150.0f, banner);
	}

	// Our ship broke apart: when it comes back.
	if (downFor >= 0.0f)
	{
		TextLook down;
		down.size = 52.0f;
		down.bold = true;
		down.align = Align::Center;
		down.color = PackColor(1.0f, 0.45f, 0.35f);
		down.glow = 0.2f;
		down.glowColor = PackColor(0.6f, 0.05f, 0.02f, 0.6f);
		ui.Text(strings.Get("flight.destroyed"), middle.x, 250.0f, down);
		std::snprintf(number, sizeof(number), "%.0f", std::ceil(std::max(0.0f, respawn - downFor)));
		look.align = Align::Center;
		ui.Text(strings.Format("flight.back", number).c_str(), middle.x, 320.0f, look);
	}

	// The controls, for the first seconds of a flight.
	flown += dt;
	const f32 hintFade = std::clamp((HINT_SECONDS - flown) / 2.0f, 0.0f, 1.0f);
	if (hintFade > 0.0f)
	{
		TextLook hintLook;
		hintLook.size = 16.0f;
		hintLook.color = PackColor(0.45f, 0.5f, 0.6f, hintFade);
		hintLook.maxWidth = width - 64.0f;
		const char* hint = strings.Get("flight.hint");
		ui.Text(hint, 32.0f, Ui::HEIGHT - 28.0f - ui.Measure(hint, hintLook).y, hintLook);
	}
}

void Flight::DrawChat(Ui& ui, f32 dt)
{
	// The keyboard put away by the user (Android's back button): a cancel.
	if (typing && !os::IsTextInputActive())
		StopTyping();
	for (ChatLine& line : chat)
		line.age += dt;
	StringTable& strings = resources->strings;
	const f32 width = ui.Width();
	const f32 keyboard = os::GetKeyboardInset(window) / ui.PixelsPerUnit();
	f32 bottom = Ui::HEIGHT - 72.0f - keyboard;
	TextLook look;
	look.size = 18.0f;
	look.maxWidth = std::min(560.0f, width - 64.0f);
	look.glow = 0.15f;
	look.glowColor = PackColor(0.0f, 0.0f, 0.0f, 0.8f);

	// The line being typed, in a box, its caret blinking.
	if (typing)
	{
		const std::string shown = std::string(strings.Get("chat.say")) + " " + draft +
		                          (std::fmod(flown, 1.0f) < 0.5f ? "_" : "");
		const Vec2 size = ui.Measure(shown.c_str(), look);
		ui.Box(24.0f, bottom - size.y - 8.0f, look.maxWidth + 16.0f, size.y + 16.0f,
		       PackColor(0.02f, 0.03f, 0.05f, 0.75f));
		ui.Text(shown.c_str(), 32.0f, bottom - size.y, look);
		bottom -= size.y + 24.0f;
	}

	// The newest lines upward; they fade after a while, unless the chat is open.
	const usize shown = std::min(chat.size(), CHAT_SHOWN);
	for (usize i = 0; i < shown; ++i)
	{
		const ChatLine& line = chat[chat.size() - 1 - i];
		const f32 alpha =
			typing ? 1.0f : std::clamp((CHAT_SECONDS + 2.0f - line.age) / 2.0f, 0.0f, 1.0f);
		if (alpha <= 0.0f)
			break;
		TextLook lineLook = look;
		lineLook.color = line.notice ? PackColor(0.55f, 0.8f, 1.0f, alpha)
		                 : line.own  ? PackColor(0.75f, 1.0f, 0.75f, alpha)
		                             : PackColor(1.0f, 1.0f, 1.0f, alpha);
		lineLook.glowColor = PackColor(0.0f, 0.0f, 0.0f, 0.8f * alpha);
		const Vec2 size = ui.Measure(line.text.c_str(), lineLook);
		ui.Text(line.text.c_str(), 32.0f, bottom - size.y, lineLook);
		bottom -= size.y + 6.0f;
	}

	// For fingers: the button that opens the chat.
	if (sawTouch && !typing)
	{
		const f32 w = 96.0f;
		const f32 h = 44.0f;
		const f32 x = width - 24.0f - w;
		const f32 y = Ui::HEIGHT - 72.0f - h - keyboard;
		ui.Box(x, y, w, h, PackColor(0.05f, 0.08f, 0.12f, 0.7f));
		TextLook label = look;
		label.align = Align::Center;
		label.maxWidth = 0.0f;
		const char* text = strings.Get("chat.button");
		ui.Text(text, x + 0.5f * w, y + 0.5f * (h - ui.Measure(text, label).y), label);
		const f32 scale = ui.PixelsPerUnit();
		chatButton[0] = x * scale;
		chatButton[1] = y * scale;
		chatButton[2] = w * scale;
		chatButton[3] = h * scale;
	}
}
} // namespace sn
