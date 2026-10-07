#include "flight.h"

#include "icon_codes.h"
#include "resources.h"

#include <sn/sim/catalog.h>

#include <ph/core/log.h>
#include <ph/core/profile.h>
#include <ph/core/time.h>
#include <ph/debug_ui/debug_ui.h>
#include <ph/os/input.h>
#include <ph/platform/platform.h>
#include <ph/render/camera.h>
#include <ph/render/frame.h>
#include <ph/render/pbr.h>
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
// The HUD's least width in units (Ui::Begin): on a phone held upright its
// corners keep apart (its bars shorter: BarWidth), and its text stays as
// large as the width allows.
constexpr f32 HUD_WIDTH = 440.0f;

// The HUD's bars: shorter on a narrow screen.
f32 BarWidth(const Ui& ui) { return ui.Width() < 600.0f ? 140.0f : 220.0f; }
constexpr f32 HIGHEST = 300.0f;      // m: the camera at its farthest
constexpr f32 COMBAT_REACH = 160.0f; // m: enemies as near as this make a fight, and show
constexpr f32 COMBAT_MARGIN = 14.0f; // m around them
constexpr f32 FOV = 0.8f;            // radians, vertical
// Rocks far below the plane of play, small and dark, so that they read as
// the background: they only show how fast the ship goes.
constexpr u32 SCENERY_COUNT = 350;
constexpr f32 SCENERY_FIELD = 1000.0f; // m: their square, around the start
constexpr f32 STICK_DEAD_ZONE = 0.15f;
constexpr f32 WAVE_BANNER = 2.5f;   // s a new wave's number shows
constexpr f32 HINT_SECONDS = 10.0f; // s the controls show at the bottom, before they fade
constexpr f32 ORDER_MARK = 1.2f;    // s an order's mark shows
constexpr f32 PICK_PIXELS = 36.0f;  // a click this near an enemy picks it
// A correction of our predicted ship fades out of what is drawn by e in
// this many seconds; a jump bigger than SNAP m is the ship put elsewhere (back
// home), shown as it is.
constexpr f32 SMOOTHING = 0.1f;
constexpr f32 SNAP = 4.0f;
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

// A button of the HUD: a dark box with its label in the middle.
void HudButton(Ui& ui, f32 x, f32 y, f32 w, f32 h, const char* text, u32 fill, f32 (&rect)[4])
{
	ui.Box(x, y, w, h, fill);
	TextLook label;
	label.size = 18.0f;
	label.align = Align::Center;
	label.color = PackColor(0.9f, 0.95f, 1.0f);
	ui.Text(text, x + 0.5f * w, y + 0.5f * (h - ui.Measure(text, label).y), label);
	const f32 scale = ui.PixelsPerUnit();
	rect[0] = x * scale;
	rect[1] = y * scale;
	rect[2] = w * scale;
	rect[3] = h * scale;
}
} // namespace

void Flight::Enter(Resources& from, Connection& through, os::WindowId into)
{
	resources = &from;
	connection = &through;
	window = into;
	chat.clear();
	typing = false;
	draft.clear();
	ship = 0;
	team = sim::PLAYERS;
	kind = sim::MatchKind::Skirmish;
	ring = 0;
	modules.clear();
	attackDistance = 40.0f;
	history.clear();
	incoming = std::make_unique<sim::Snapshot>();
	latest = nullptr;
	renderTick = 0.0;
	// A local server's snapshots come every tick, without fail; a distant
	// one's unevenly: three ticks (100 ms) of them in hand smooth that out.
	delayTicks = connection->IsLocal() ? 1.0f : 3.0f;
	welcomed = false;
	rocks.clear(); // the server's, in Welcome
	field.clear();
	pieces.clear();
	crates.clear();
	present.clear();
	effects.Reset();
	ownLook = MakeLook(resources->shipBounds, resources->shieldRadius, false);
	enemyLook = MakeLook(resources->enemyBounds, resources->enemyShieldRadius, true);
	height = CAMERA_HEIGHT;
	kills = 0;
	wave = 0;
	waveShown = WAVE_BANNER;
	flown = 0.0f;
	cleared = -1.0f;
	warmed = false;
	downFor = -1.0f;
	hurt = 0.0f;
	shake = 0.0f;
	lastHealth = 0;
	staleSnapshots = 0;
	prediction = {};
	autopiloted = false;
	tickTime = 0.0f;
	ticks = 0;
	smoothing = {};
	smoothingAngle = 0.0f;
	order = {};
	target = 0;
	pressed = false;
	following = false;
	steering = false;
	orderShown = ORDER_MARK;
	zoom = 1.0f;
	named = false;
	std::fill(std::begin(returnButton), std::end(returnButton), 0.0f);

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
	history.clear();
	incoming.reset();
	latest = nullptr;
}

bool Flight::Inside(const f32 (&rect)[4], f32 x, f32 y)
{
	return rect[2] > 0.0f && x >= rect[0] && x < rect[0] + rect[2] && y >= rect[1] &&
	       y < rect[1] + rect[3];
}

Flight::Action Flight::OnEvent(const Event& event)
{
	if (event.type == EventType::TouchDown)
		sawTouch = true;
	if (typing)
	{
		OnTyping(event);
		return Action::None;
	}
	// The chat opens with Enter, the Chat button, or a pad's View button (on
	// a Steam Deck in Game Mode, Steam's keyboard comes up with it). Alt+Enter
	// is the shell's fullscreen toggle.
	const bool enter =
		(event.type == EventType::KeyDown && !event.key.repeat && !event.key.mods.alt &&
		 (event.key.scancode == Scancode::Return || event.key.scancode == Scancode::KpEnter)) ||
		(event.type == EventType::GamepadButtonDown && event.gamepad.button == GamepadButton::Back);
	const bool button = event.type == EventType::TouchDown && sawTouch &&
	                    Inside(chatButton, event.touch.x, event.touch.y);
	if ((enter || button) && welcomed)
	{
		StartTyping();
		return Action::None;
	}
	const bool returning = IsBattle() && ((event.type == EventType::MouseButtonDown &&
	                                       event.button.button == MouseButton::Left &&
	                                       Inside(returnButton, event.button.x, event.button.y)) ||
	                                      (event.type == EventType::TouchDown &&
	                                       Inside(returnButton, event.touch.x, event.touch.y)));
	if (returning)
		return Action::Return;
	// The wheel: nearer or farther.
	if (event.type == EventType::MouseWheel && event.wheel.y != 0.0f)
	{
		zoom = std::clamp(zoom * (event.wheel.y > 0.0f ? 1.0f / 1.15f : 1.15f), 0.4f, 2.5f);
		return Action::None;
	}
	const bool back =
		(event.type == EventType::KeyDown && !event.key.repeat &&
		 (event.key.scancode == Scancode::Escape || event.key.scancode == Scancode::AcBack)) ||
		(event.type == EventType::GamepadButtonDown &&
		 (event.gamepad.button == GamepadButton::Start ||
		  event.gamepad.button == GamepadButton::East));
	if (back)
		return IsBattle() ? Action::Return : Action::Leave;
	return Action::None;
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
				connection->Post(sim::Write(say), net::Delivery::Reliable);
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
	{
		if (said.text == "battle.cleared")
			cleared = 0.0f;
		line.text =
			resources->strings.Format(said.text.c_str(), said.name.c_str(), said.extra.c_str());
	}
	else
	{
		line.text = said.name + ": " + said.text;
		line.own = said.ship == ship;
	}
	chat.push_back(std::move(line));
	if (chat.size() > CHAT_KEPT)
		chat.erase(chat.begin());
}

bool Flight::ReadSteering(sim::ShipControls& controls) const
{
	// No flying while they type a chat line: keys type, and on the Steam Deck
	// the pad and its trackpads work Steam's keyboard.
	if (typing)
		return false;
	const auto key = [](Scancode a, Scancode b)
	{ return IsKeyDown(a) || IsKeyDown(b) ? 1.0f : 0.0f; };
	f32 turn = key(Scancode::A, Scancode::Left) - key(Scancode::D, Scancode::Right);
	f32 thrust = key(Scancode::W, Scancode::Up) - key(Scancode::S, Scancode::Down);
	// Pads: the left trigger thrusts (as far as it is pulled), the left
	// bumper reverses at full power, the left stick only turns.
	GamepadState pads[MAX_GAMEPADS];
	const u32 count = GetGamepads(pads, MAX_GAMEPADS);
	for (u32 i = 0; i < count; ++i)
	{
		turn -= DeadZone(pads[i].Axis(GamepadAxis::LeftX));
		thrust += pads[i].Axis(GamepadAxis::LeftTrigger) > 0.05f
					  ? pads[i].Axis(GamepadAxis::LeftTrigger)
					  : 0.0f;
		if (pads[i].IsDown(GamepadButton::LeftShoulder))
			thrust -= 1.0f;
	}
	controls.turn = std::clamp(turn, -1.0f, 1.0f);
	controls.thrust = std::clamp(thrust, -1.0f, 1.0f);
	return controls.turn != 0.0f || controls.thrust != 0.0f;
}

Vec2 Flight::ToPlane(PixelSize size, f32 x, f32 y) const
{
	// Straight down from `eye`: the screen's middle is under it.
	const f32 perMeter = 0.5f * f32(size.height) / (height * std::tan(0.5f * FOV));
	const f32 dx = (x - 0.5f * f32(size.width)) / perMeter;
	const f32 dz = (y - 0.5f * f32(size.height)) / perMeter;
	return {eye.x + dx, -(eye.z + dz)};
}

void Flight::ReadPointer(PixelSize size)
{
	const MouseState mouse = GetMouseState();
	const bool down = !typing && (mouse.buttons & MouseButtonBit(MouseButton::Left)) != 0;
	// The right button: stop, and no target.
	if (!typing && (mouse.buttons & MouseButtonBit(MouseButton::Right)) != 0)
	{
		order = {};
		target = 0;
		following = false;
	}
	if (down && !pressed && size.height > 0 && !Inside(chatButton, mouse.x, mouse.y) &&
	    !Inside(returnButton, mouse.x, mouse.y))
	{
		const Vec2 at = ToPlane(size, mouse.x, mouse.y);
		const f32 perMeter = 0.5f * f32(size.height) / (height * std::tan(0.5f * FOV));
		f32 best = std::max(4.0f, PICK_PIXELS / perMeter);
		const Mark* picked = nullptr;
		for (const Mark& mark : marks)
		{
			const f32 distance = sim::Length(mark.position - at);
			if (distance < best)
			{
				best = distance;
				picked = &mark;
			}
		}
		if (picked)
		{
			// An enemy: our turrets prefer it, and the ship circles it.
			target = picked->id;
			order.kind = bots::Order::Kind::Attack;
			following = false;
		}
		else
		{
			order.kind = bots::Order::Kind::Move;
			order.point = at;
			following = true;
		}
		orderShown = 0.0f;
	}
	else if (down && following && size.height > 0)
	{
		order.point = ToPlane(size, mouse.x, mouse.y);
		orderShown = 0.0f;
	}
	if (!down)
		following = false;
	pressed = down;
}

sim::ShipControls Flight::OrderControls() const
{
	if (!prediction.IsActive())
		return {};
	const sim::ShipState* other = latest && target ? latest->Find(target) : nullptr;
	bots::Mark mark;
	if (other && other->health > 0)
		mark = {other->position, other->velocity};
	return bots::Steer(prediction.Now(), order, other && other->health > 0 ? &mark : nullptr,
	                   attackDistance, field.data(), u32(field.size()));
}

void Flight::SendControls(PixelSize size, f32 dt)
{
	if (!ship)
		return;
	ReadPointer(size);
	sim::ShipControls steer;
	steering = ReadSteering(steer);
	// Steering by hand ends a move or a circling; the target stays.
	if (steering && order.kind != bots::Order::Kind::Stop)
	{
		order = {};
		following = false;
	}
	// Our ticks at the server's rate, on the frame's time as a local server
	// counts it: each flies our ship at once and goes to the server.
	tickTime += std::min(dt, 0.25f);
	bool ticked = false;
	sim::ShipControls controls;
	while (tickTime >= sim::TICK_SECONDS)
	{
		tickTime -= sim::TICK_SECONDS;
		++ticks;
		controls = sim::Quantize(steering ? steer : OrderControls());
		prediction.Step(controls);
		ticked = true;
	}
	if (ticked)
		SendInput();
	if (welcomed)
		SendName();
	// The engine hums only while the ship flies.
	const f32 push = downFor < 0.0f ? std::abs(prediction.Now().controls.thrust) : 0.0f;
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
	connection->Post(sim::Write(name), net::Delivery::Reliable);
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
	input.target = target;
	connection->Post(sim::Write(input), net::Delivery::Unreliable);
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
	prediction.Correct(own, latest->own.cargoMass, latest->input);
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
		case sim::MessageType::Snapshot:
			if (!incoming || !sim::Read(data, size, *incoming))
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
	if (!connection)
		return;
	connection->DrawNetworkWindow(staleSnapshots);
	// How far behind the newest snapshot ships are drawn, from how many.
	if (!latest)
		return;
	ImGui::Begin("Network", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::Text("drawn %.0f ms behind the newest of %u snapshots",
	            (f64(latest->tick) - renderTick) * sim::TICK_SECONDS * 1000.0, u32(history.size()));
	ImGui::End();
#endif
}

void Flight::OnWelcome(const sim::Welcome& welcome)
{
	ship = welcome.ship;
	respawn = welcome.respawn;
	kind = welcome.kind;
	ring = welcome.ring;
	modules = welcome.modules;
	welcomed = true;
	connection->SetAnswered();
	prediction.Reset(welcome.hull, welcome.rocks);
	autopiloted = welcome.autopilot;
	tickTime = 0.0f;
	ticks = 0;
	smoothing = {};
	smoothingAngle = 0.0f;
	order = {};
	target = 0;
	cleared = -1.0f;
	// Where an attack circles: its longest weapon's reach, less a margin.
	const sim::Catalog& catalog = sim::GetCatalog();
	f32 reach = 0.0f;
	for (const std::string& id : modules)
	{
		if (const sim::ModuleDesc* module = catalog.FindModule(id))
		{
			if (sim::IsWeapon(module->kind))
				reach = std::max(reach, module->range);
		}
	}
	attackDistance = reach > 0.0f ? catalog.rules.attackRange * reach : 40.0f;
	field = welcome.rocks;
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
	crates.clear();
	for (const sim::CrateState& crate : welcome.crates)
		crates.push_back({crate.index, crate.position, 0.0f});
	PH_LOG_INFO("flight: ship %u in a %s, a field of %zu rocks", ship,
	            IsBattle() ? "battle" : "skirmish", rocks.size());
}

void Flight::OnSnapshot()
{
	if (const sim::ShipState* own = latest->Find(ship))
	{
		team = own->team;
		Correct(*own);
		// Beams burn without events: the hull's fall shows them.
		if (own->health < lastHealth && own->health > 0)
			hurt = std::min(1.0f, hurt + f32(lastHealth - own->health) / 40.0f);
		lastHealth = own->health;
	}
	// A target gone or broken: no more of it.
	if (target)
	{
		const sim::ShipState* other = latest->Find(target);
		if (!other || other->health == 0)
		{
			target = 0;
			if (order.kind == bots::Order::Kind::Attack)
				order = {};
		}
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
					resources->PlayEffect(resources->fire, 0.3f);
				else
					PlayAt(TeamOf(event.ship) == team ? resources->fire : resources->enemyFire,
					       0.3f, event.position);
				break;
			case sim::EventType::RockHit:
				effects.Sparks(ToWorld(event.position, 0.3f));
				PlayAt(resources->hit, 0.3f, event.position);
				break;
			case sim::EventType::RockBroken:
				prediction.BreakRock(event.index);
				if (event.index < field.size())
					field[event.index].health = 0.0f;
				if (event.index < rocks.size())
				{
					Rock& rock = rocks[event.index];
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
					// As much red as the hit was hard: a plasma charge's
					// bleed past the shield barely shows.
					shake = std::min(1.0f, shake + std::min(0.35f, event.strength / 30.0f));
					hurt = std::min(1.0f, hurt + event.strength / 40.0f);
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
			case sim::EventType::ModuleBroken:
				effects.Sparks(ToWorld(event.position, 0.6f), 14);
				if (ours)
				{
					resources->PlayEffect(resources->hullHit, 1.0f);
					sim::Chat notice;
					notice.notice = true;
					notice.text = "flight.module_broken";
					OnChat(notice);
				}
				break;
			case sim::EventType::CrateDropped:
				crates.push_back({event.index, event.position, 0.0f});
				break;
			case sim::EventType::CratePicked:
			case sim::EventType::CrateLost:
			{
				const auto found = std::find_if(crates.begin(), crates.end(), [&](const Crate& c)
				                                { return c.index == event.index; });
				if (found != crates.end())
					crates.erase(found);
				if (event.type == sim::EventType::CratePicked && ours)
					resources->PlayEffect(resources->confirm, 0.6f);
				break;
			}
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
	const f64 goal = f64(latest->tick) - delayTicks;
	if (std::abs(renderTick - goal) > 4.0)
		renderTick = goal;
	else
		renderTick += (goal - renderTick) * std::min(1.0, f64(dt) * 2.0);
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

	// From above, screen up along the plane's y (-Z), as high as it takes to
	// show the enemies near (times the wheel's zoom); a bump shakes it.
	const f32 aspect =
		commands.size.height > 0 ? f32(commands.size.width) / f32(commands.size.height) : 1.0f;
	f32 half = 0.0f; // of the plane's height on screen, m
	for (u32 i = 0; i < to->count; ++i)
	{
		const sim::ShipState& other = to->ships[i];
		const Vec2 off = other.position - now->position;
		if (other.team == team || other.health == 0 || sim::Length(off) > COMBAT_REACH)
			continue;
		half = std::max({half, std::abs(off.y) + COMBAT_MARGIN,
		                 (std::abs(off.x) + COMBAT_MARGIN) / std::max(aspect, 0.5f)});
	}
	// Loot near enough to take shows too.
	for (const Crate& crate : crates)
	{
		const Vec2 off = crate.position - now->position;
		if (sim::Length(off) <= COMBAT_REACH)
			half = std::max({half, std::abs(off.y) + COMBAT_MARGIN,
			                 (std::abs(off.x) + COMBAT_MARGIN) / std::max(aspect, 0.5f)});
	}
	const f32 wanted =
		std::clamp(std::max(CAMERA_HEIGHT, half / std::tan(0.5f * FOV)) * zoom, 15.0f, HIGHEST);
	height += (wanted - height) * std::min(1.0f, 1.2f * dt);
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
	const f32 reach = std::tan(0.5f * FOV);
	const auto seen = [&](Vec3 at, f32 depth, f32 radius)
	{
		return std::abs(at.x - center.x) <= reach * aspect * depth + radius &&
		       std::abs(at.z - center.z) <= reach * depth + radius;
	};

	// Ships, banked into turns a little; ours and our side's as they are, the
	// enemies' red. Turrets are not drawn: ours shows in the HUD (its aim
	// around our ship), the others' only by what they fire. Wrecks have
	// broken apart already.
	effects.Begin(dt);
	marks.clear();
	friends.clear();
	turretShown = false;
	Vec2 drawn[sim::MAX_SNAPSHOT_SHIPS];
	{
		PH_PROFILE_SCOPE("Flight.Ships");
		rhi::DebugLabelScope label(commands, "ships");
		for (u32 i = 0; i < to->count; ++i)
		{
			const sim::ShipState& state = to->ships[i];
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
			drawn[i] = at;
			if (state.health == 0)
				continue;
			const bool enemy = state.team != team;
			if (enemy)
				marks.push_back({state.id, at, state.health, state.shield});
			else
				friends.push_back(at);
			if (state.id == ship)
			{
				turretShown = true;
				turretAt = at;
				turretAim = heading + sim::ByteToAngle(state.turret);
				turretFiring = state.beam != sim::NO_BEAM;
			}
			const Mat4 model =
				Translation(ToWorld(at)) * RotationY(heading) * RotationZ(-0.5f * looks.turn);
			// The generated ship where the art pack has it, else Kenney's.
			Resources::ShipModel& generated = enemy ? resources->enemyModel : resources->shipModel;
			if (seen(ToWorld(at), height, 4.0f))
			{
				if (generated.loaded)
					render::DrawModel(commands, generated.model, resources->light,
					                  model * generated.fit, frame);
				else
					render::DrawMesh(commands, enemy ? resources->enemy : resources->ship,
					                 enemy ? resources->enemyMaterial : resources->material, model);
			}
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
	// ours orange, the enemies' red. Beams from turret to target, or to the
	// rock in the way. Crates turn slowly, glowing.
	const f32 since = f32((renderTick - f64(to->tick)) * sim::TICK_SECONDS);
	{
		PH_PROFILE_SCOPE("Flight.Shots");
		rhi::DebugLabelScope label(commands, "shots, beams, crates and pieces");
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
		for (u32 i = 0; i < to->count; ++i)
		{
			const sim::ShipState& state = to->ships[i];
			if (state.beam == sim::NO_BEAM || state.health == 0 || state.beam >= to->count)
				continue;
			const Vec2 start = drawn[i];
			Vec2 end = drawn[state.beam];
			const f32 length = sim::Length(end - start);
			if (length < 0.5f)
				continue;
			f32 first = std::max(0.0f, (length - 1.4f) / length);
			for (const sim::Rock& rock : field)
			{
				const f32 hit = rock.health > 0.0f ? sim::SweepContact(start, end - start,
				                                                       rock.position, rock.radius)
				                                   : -1.0f;
				if (hit >= 0.0f && hit < first)
					first = hit;
			}
			end = start + (end - start) * first;
			const f32 shown = sim::Length(end - start);
			const f32 width = 0.14f + 0.05f * std::sin(40.0f * t + f32(i));
			render::DrawMesh(
				commands, resources->bolt,
				state.team == team ? resources->boltMaterial : resources->enemyBoltMaterial,
				Translation(ToWorld(start + (end - start) * 0.5f, 0.7f)) *
					RotationY(sim::AngleOf(end - start)) * Scale({width, width, shown}));
			if (Random(random) < dt * 8.0f)
				effects.Sparks(ToWorld(end, 0.5f), 2);
		}
		for (Crate& crate : crates)
		{
			crate.age += dt;
			const f32 size = 0.9f + 0.15f * std::sin(3.0f * t + f32(crate.index));
			render::DrawMesh(commands, resources->bolt, resources->boltMaterial,
			                 Translation(ToWorld(crate.position, 0.5f)) *
			                     RotationY(1.3f * t + f32(crate.index)) * RotationX(0.6f) *
			                     Scale({size, size, size}));
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
	ui.Begin(commands, time, *resources, HUD_WIDTH, os::GetSafeInsets(window));
	if (!warmed)
		WarmHud(ui);
	DrawHud(ui, *now, dt);
	DrawChat(ui, dt);
	ui.End(commands);
}

void Flight::DrawConnecting(rhi::CommandList& commands, const render::FrameTime& time, Ui& ui)
{
	if (connection->IsLocal())
		return; // a frame or two at most
	ui.Begin(commands, time, *resources, HUD_WIDTH, os::GetSafeInsets(window));
	TextLook look;
	look.size = 24.0f;
	look.align = Align::Center;
	look.color = PackColor(0.6f, 0.68f, 0.8f);
	ui.Text(resources->strings.Format("flight.connecting", connection->HostName().c_str()).c_str(),
	        0.5f * ui.Width(), 340.0f, look);
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
	ui.Measure(strings.Get("flight.capacitor"), look);
	TextLook bold;
	bold.bold = true;
	bold.size = 56.0f;
	ui.Measure(strings.Format("flight.wave", "0123456789").c_str(), bold);
	bold.size = 52.0f;
	ui.Measure(strings.Get("flight.destroyed"), bold);
}

void Flight::DrawSystems(Ui& ui, const sim::ShipState& own, f32 top)
{
	const sim::Catalog& catalog = sim::GetCatalog();
	StringTable& strings = resources->strings;
	const sim::OwnState& state = latest->own;
	const sim::HullClass& hull = prediction.Now().hull;
	TextLook label;
	label.size = 14.0f;
	label.color = PackColor(0.55f, 0.6f, 0.7f);
	char text[96];

	const f32 bar = BarWidth(ui);
	Bar(ui, 32.0f, top, bar, 7.0f, own.capacitor, {1.0f, 0.85f, 0.3f});
	std::snprintf(text, sizeof(text), "%s %.2f GJ", strings.Get("flight.capacitor"),
	              f64(state.capacitor));
	ui.Text(text, 42.0f + bar, top - 6.0f, label);
	f32 y = top + 20.0f;
	for (usize i = 0; i < modules.size() && i < sim::MAX_MODULES; ++i)
	{
		const sim::ModuleDesc* module = catalog.FindModule(modules[i]);
		if (!module)
			continue;
		const u8 health = state.modules[i];
		const bool offline = (state.flags[i] & sim::MODULE_OFFLINE) != 0;
		const bool working = (state.flags[i] & sim::MODULE_WORKING) != 0;
		TextLook name = label;
		name.color = health == 0 ? PackColor(1.0f, 0.35f, 0.3f)
		             : offline   ? PackColor(1.0f, 0.75f, 0.3f)
		             : working   ? PackColor(0.85f, 0.95f, 1.0f)
		                         : PackColor(0.6f, 0.68f, 0.8f);
		const std::string key = "module." + modules[i];
		std::string line = std::string(icon::Find(modules[i])) + " " + strings.Get(key.c_str());
		if (health == 0)
			line += std::string("  ") + strings.Get("flight.broken");
		else if (offline)
			line += std::string("  ") + strings.Get("flight.offline");
		else if (module->kind == sim::ModuleKind::Plasma)
		{
			if (state.reloading > 0.0f)
				std::snprintf(text, sizeof(text), "  %s",
				              strings.Format("flight.reloading", "").c_str());
			else
				std::snprintf(text, sizeof(text), "  %u + %u", u32(state.loaded), state.ammo);
			line += text;
		}
		Bar(ui, 32.0f, y + 5.0f, 60.0f, 5.0f, health, {0.7f, 0.75f, 0.8f});
		ui.Text(line.c_str(), 100.0f, y, name);
		y += 18.0f;
	}
	std::snprintf(text, sizeof(text), "%s %.1f / %.0f m3", strings.Get("flight.hold"),
	              f64(state.cargoUsed), f64(hull.cargo));
	ui.Text(text, 32.0f, y + 2.0f, label);
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
			ui.Box(in, ui.Height() - in - BAND, width - 2.0f * in, BAND, red);
			ui.Box(in, in + BAND, BAND, ui.Height() - 2.0f * (in + BAND), red);
			ui.Box(width - in - BAND, in + BAND, BAND, ui.Height() - 2.0f * (in + BAND), red);
		}
	}

	// Enemies: a bar over the damaged ones on screen, brackets around our
	// target, a mark at the edge toward the others.
	const f32 perMeter = 0.5f * ui.Height() / (height * std::tan(0.5f * FOV));
	const Vec2 middle = {0.5f * width, 0.5f * ui.Height()};
	const auto onScreen = [&](Vec2 plane)
	{
		return Vec2{middle.x + (plane.x - eye.x) * perMeter,
		            middle.y + (-plane.y - eye.z) * perMeter};
	};
	const u32 hostile = PackColor(1.0f, 0.3f, 0.25f, 0.9f);
	const u32 aimed = PackColor(1.0f, 0.85f, 0.3f, 0.95f);
	// Seen from far, ships are small: a ring around each, in its side's
	// color, keeps them readable.
	const f32 ringed = std::max(0.0f, std::min(1.0f, (height - 70.0f) / 60.0f));
	const auto circle = [&](Vec2 plane, u32 color)
	{
		const Vec2 at = onScreen(plane);
		const f32 r = std::max(10.0f, 2.6f * perMeter);
		constexpr u32 SEGMENTS = 16;
		for (u32 k = 0; k < SEGMENTS; ++k)
		{
			const f32 a = 2.0f * PI * f32(k) / f32(SEGMENTS);
			const f32 b = 2.0f * PI * f32(k + 1) / f32(SEGMENTS);
			ui.Line({at.x + r * std::cos(a), at.y + r * std::sin(a)},
			        {at.x + r * std::cos(b), at.y + r * std::sin(b)}, 1.6f, color);
		}
	};
	// A mark at the screen's edge, toward `off` from its middle.
	const auto edge = [&](Vec2 off, u32 color)
	{
		constexpr f32 EDGE = 36.0f; // units inside the screen's edge
		const f32 scale =
			std::min(std::abs(off.x) > 1e-3f ? (middle.x - EDGE) / std::abs(off.x) : 1e9f,
			         std::abs(off.y) > 1e-3f ? (middle.y - EDGE) / std::abs(off.y) : 1e9f);
		const Vec2 tip = middle + off * scale;
		const Vec2 way = off * (1.0f / std::max(sim::Length(off), 1e-3f));
		const Vec2 across = {-way.y, way.x};
		constexpr f32 SIZE = 16.0f;
		ui.Line(tip, tip - way * SIZE + across * (0.6f * SIZE), 4.0f, color);
		ui.Line(tip, tip - way * SIZE - across * (0.6f * SIZE), 4.0f, color);
	};
	// Our turret: an arc around our ship where it aims, with a pointer out;
	// pale blue when ready, amber while it reloads, dim while offline or
	// broken, brighter while it fires.
	if (turretShown && welcomed && !modules.empty() && !modules[0].empty())
	{
		const Vec2 at = onScreen(turretAt);
		const f32 r = 2.6f * perMeter + 14.0f;
		const bool off =
			(latest->own.flags[0] & sim::MODULE_OFFLINE) != 0 || latest->own.modules[0] == 0;
		const u32 color = off                            ? PackColor(0.5f, 0.55f, 0.6f, 0.45f)
		                  : latest->own.reloading > 0.0f ? PackColor(1.0f, 0.75f, 0.3f, 0.85f)
		                  : turretFiring                 ? PackColor(0.75f, 0.95f, 1.0f, 1.0f)
		                                                 : PackColor(0.45f, 0.85f, 1.0f, 0.8f);
		// Screen directions: the plane's y is up the screen.
		const auto way = [](f32 a) { return Vec2{-std::sin(a), -std::cos(a)}; };
		constexpr u32 SEGMENTS = 8;
		constexpr f32 SPAN = 0.45f; // rad on each side of the aim
		for (u32 k = 0; k < SEGMENTS; ++k)
		{
			const f32 a = turretAim - SPAN + 2.0f * SPAN * f32(k) / f32(SEGMENTS);
			const f32 b = turretAim - SPAN + 2.0f * SPAN * f32(k + 1) / f32(SEGMENTS);
			ui.Line(at + way(a) * r, at + way(b) * r, 2.0f, color);
		}
		const Vec2 aim = way(turretAim);
		const Vec2 across = {-aim.y, aim.x};
		const Vec2 tip = at + aim * (r + 9.0f);
		ui.Line(tip, at + aim * (r + 2.0f) + across * 5.0f, 2.0f, color);
		ui.Line(tip, at + aim * (r + 2.0f) - across * 5.0f, 2.0f, color);
	}
	if (ringed > 0.0f)
	{
		for (const Vec2 at : friends)
			circle(at, PackColor(0.45f, 0.85f, 1.0f, 0.7f * ringed));
		for (const Mark& mark : marks)
			circle(mark.position, PackColor(1.0f, 0.35f, 0.3f, 0.7f * ringed));
	}
	u32 enemies = 0;
	for (const Mark& mark : marks)
	{
		++enemies;
		const Vec2 at = onScreen(mark.position);
		const Vec2 off = at - middle;
		if (std::abs(off.x) < middle.x - 8.0f && std::abs(off.y) < middle.y - 8.0f)
		{
			if (mark.id == target)
			{
				const f32 r = 2.6f * perMeter + 6.0f;
				for (const f32 sx : {-1.0f, 1.0f})
				{
					for (const f32 sy : {-1.0f, 1.0f})
					{
						const Vec2 corner = {at.x + sx * r, at.y + sy * r};
						ui.Line(corner, {corner.x - sx * 8.0f, corner.y}, 2.5f, aimed);
						ui.Line(corner, {corner.x, corner.y - sy * 8.0f}, 2.5f, aimed);
					}
				}
			}
			if (mark.health == 255 && mark.shield == 255)
				continue;
			const f32 barWidth = 40.0f;
			const f32 top = at.y - 2.4f * perMeter - 8.0f;
			Bar(ui, at.x - 0.5f * barWidth, top, barWidth, 3.0f, mark.shield, {1.0f, 0.55f, 0.45f});
			Bar(ui, at.x - 0.5f * barWidth, top + 4.0f, barWidth, 3.0f, mark.health,
			    {0.95f, 0.2f, 0.15f});
			continue;
		}
		edge(off, mark.id == target ? aimed : hostile);
	}
	// Loot: a gold ring on screen, a gold mark at the edge toward it.
	const u32 gold = PackColor(1.0f, 0.8f, 0.3f, 0.9f);
	for (const Crate& crate : crates)
	{
		const Vec2 at = onScreen(crate.position);
		const Vec2 off = at - middle;
		if (std::abs(off.x) < middle.x - 8.0f && std::abs(off.y) < middle.y - 8.0f)
			circle(crate.position, gold);
		else
			edge(off, gold);
	}
	// Where a move order goes: a diamond, fading after the order.
	orderShown += dt;
	if (order.kind == bots::Order::Kind::Move)
	{
		const f32 a = std::max(0.35f, 1.0f - orderShown / ORDER_MARK);
		const Vec2 at = onScreen(order.point);
		const u32 color = PackColor(0.45f, 0.85f, 1.0f, a);
		const f32 r = 9.0f;
		ui.Line({at.x - r, at.y}, {at.x, at.y - r}, 2.0f, color);
		ui.Line({at.x, at.y - r}, {at.x + r, at.y}, 2.0f, color);
		ui.Line({at.x + r, at.y}, {at.x, at.y + r}, 2.0f, color);
		ui.Line({at.x, at.y + r}, {at.x - r, at.y}, 2.0f, color);
	}

	// Our speed, shield, hull, capacitor and modules.
	TextLook look;
	look.size = 22.0f;
	look.color = PackColor(0.85f, 0.9f, 1.0f);
	const f32 speed = sim::Length(own.velocity);
	std::snprintf(number, sizeof(number), "%.0f", speed);
	// Below a phone's status bar.
	const f32 top = ui.Top();
	ui.Text(strings.Format("flight.speed", number).c_str(), 32.0f, top + 28.0f, look);
	TextLook label;
	label.size = 14.0f;
	label.color = PackColor(0.55f, 0.6f, 0.7f);
	const f32 bar = BarWidth(ui);
	Bar(ui, 32.0f, top + 64.0f, bar, 7.0f, own.shield, {0.35f, 0.75f, 1.0f});
	ui.Text(strings.Get("flight.shield"), 42.0f + bar, top + 58.0f, label);
	Bar(ui, 32.0f, top + 80.0f, bar, 7.0f, own.health, {1.0f, 0.55f, 0.2f});
	ui.Text(strings.Get("flight.hull"), 42.0f + bar, top + 74.0f, label);
	DrawSystems(ui, own, top + 96.0f);

	// The wave or the ring, the enemies left, and our kills.
	look.align = Align::Right;
	TextLook small = look;
	small.size = 18.0f;
	if (IsBattle())
	{
		std::snprintf(number, sizeof(number), "%u", ring);
		ui.Text(strings.Format("flight.ring", number).c_str(), width - 32.0f, top + 28.0f, look);
	}
	else if (wave > 0)
	{
		std::snprintf(number, sizeof(number), "%u", wave);
		ui.Text(strings.Format("flight.wave", number).c_str(), width - 32.0f, top + 28.0f, look);
	}
	// Bots called in without waves count too.
	if (IsBattle() || wave > 0 || enemies > 0)
	{
		std::snprintf(number, sizeof(number), "%u", enemies);
		ui.Text(strings.Format("flight.enemies", number).c_str(), width - 32.0f, top + 60.0f,
		        small);
	}
	std::snprintf(number, sizeof(number), "%u", kills);
	ui.Text(strings.Format("flight.kills", number).c_str(), width - 32.0f, top + 86.0f, small);

	// A new wave's number, big for a moment; a battle won, until home.
	waveShown += dt;
	TextLook banner;
	banner.size = 56.0f;
	banner.bold = true;
	banner.align = Align::Center;
	banner.glow = 0.25f;
	if (!IsBattle() && wave > 0 && waveShown < WAVE_BANNER)
	{
		const f32 fade = std::min({1.0f, waveShown / 0.25f, (WAVE_BANNER - waveShown) / 0.6f});
		banner.color = PackColor(1.0f, 0.55f, 0.45f, fade);
		banner.glowColor = PackColor(0.8f, 0.1f, 0.05f, 0.6f * fade);
		std::snprintf(number, sizeof(number), "%u", wave);
		ui.Text(strings.Format("flight.wave", number).c_str(), middle.x, 150.0f, banner);
	}
	if (cleared >= 0.0f)
	{
		cleared += dt;
		const f32 fade = std::min(1.0f, cleared / 0.4f);
		banner.size = 44.0f;
		banner.color = PackColor(0.55f, 1.0f, 0.7f, fade);
		banner.glowColor = PackColor(0.05f, 0.4f, 0.15f, 0.6f * fade);
		ui.Text(strings.Get("flight.cleared"), middle.x, 130.0f, banner);
		look.align = Align::Center;
		look.size = 18.0f;
		std::snprintf(number, sizeof(number), "%.0f",
		              std::ceil(std::max(0.0f, sim::GetCatalog().rules.clearedReturn - cleared)));
		ui.Text(strings.Format("flight.cleared_hint", number).c_str(), middle.x, 186.0f, look);
		look.size = 22.0f;
	}

	// Our ship broke apart: when it comes back, or that it is lost.
	if (downFor >= 0.0f)
	{
		TextLook down;
		down.size = 52.0f;
		down.bold = true;
		down.align = Align::Center;
		down.color = PackColor(1.0f, 0.45f, 0.35f);
		down.glow = 0.2f;
		down.glowColor = PackColor(0.6f, 0.05f, 0.02f, 0.6f);
		ui.Text(strings.Get(IsBattle() ? "flight.lost" : "flight.destroyed"), middle.x, 250.0f,
		        down);
		if (!IsBattle())
		{
			std::snprintf(number, sizeof(number), "%.0f",
			              std::ceil(std::max(0.0f, respawn - downFor)));
			look.align = Align::Center;
			ui.Text(strings.Format("flight.back", number).c_str(), middle.x, 320.0f, look);
		}
	}

	// A battle's way home.
	if (IsBattle())
	{
		const f32 w = 150.0f;
		const f32 h = 44.0f;
		HudButton(ui, width - 24.0f - w, ui.Bottom() - 72.0f - 2.0f * h - 12.0f, w, h,
		          strings.Get("flight.return"),
		          cleared >= 0.0f ? PackColor(0.05f, 0.35f, 0.18f, 0.85f)
		                          : PackColor(0.05f, 0.08f, 0.12f, 0.7f),
		          returnButton);
	}

	// The controls, for the first seconds of a flight.
	flown += dt;
	const f32 hintFade = std::clamp((HINT_SECONDS - flown) / 2.0f, 0.0f, 1.0f);
	if (hintFade > 0.0f && !sawTouch) // phones: fewer words
	{
		TextLook hintLook;
		hintLook.size = 16.0f;
		hintLook.color = PackColor(0.45f, 0.5f, 0.6f, hintFade);
		hintLook.maxWidth = width - 260.0f;
		const char* hint = strings.Get("flight.hint");
		ui.Text(hint, 32.0f, ui.Bottom() - 28.0f - ui.Measure(hint, hintLook).y, hintLook);
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
	f32 bottom = ui.Bottom() - 72.0f - keyboard;
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
		HudButton(ui, width - 24.0f - w, ui.Bottom() - 72.0f - h - keyboard, w, h,
		          strings.Get("chat.button"), PackColor(0.05f, 0.08f, 0.12f, 0.7f), chatButton);
	}
}
} // namespace sn
