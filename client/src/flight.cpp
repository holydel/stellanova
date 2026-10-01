#include "flight.h"

#include "resources.h"

#include <ph/core/log.h>
#include <ph/os/input.h>
#include <ph/render/camera.h>
#include <ph/render/frame.h>
#include <ph/render/sky.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sn
{
namespace
{
using namespace ph;
using namespace ph::os;

constexpr f32 CAMERA_HEIGHT = 40.0f; // m above the ship
constexpr u32 ROCK_COUNT = 2000;
constexpr f32 FIELD = 600.0f; // m: the rocks' square, around the start

// The plane of play in the world: x is X, y is -Z (sim/world.h).
Vec3 ToWorld(Vec2 p) { return {p.x, 0.0f, -p.y}; }

f32 WrapAngle(f32 angle)
{
	while (angle > PI)
		angle -= 2.0f * PI;
	while (angle < -PI)
		angle += 2.0f * PI;
	return angle;
}

// Steady pseudo-random numbers, so that the field looks the same each time.
f32 Random(u32& state)
{
	state = state * 1664525u + 1013904223u;
	return f32(state >> 8) / f32(1u << 24);
}
} // namespace

void Flight::Enter(Resources& from, const char* address)
{
	resources = &from;
	client = net::Connect(address);
	ship = 0;
	latest = std::make_unique<sim::Snapshot>();
	previous = std::make_unique<sim::Snapshot>();
	incoming = std::make_unique<sim::Snapshot>();
	sinceSnapshot = 0.0f;

	rocks.clear();
	u32 seed = 7;
	for (u32 i = 0; i < ROCK_COUNT; ++i)
	{
		Rock rock;
		rock.position = {(Random(seed) - 0.5f) * FIELD, -2.0f - 6.0f * Random(seed),
		                 (Random(seed) - 0.5f) * FIELD};
		const f32 size = 1.0f + 6.0f * Random(seed) * Random(seed);
		rock.scale = {size * (0.8f + 0.4f * Random(seed)), size * (0.7f + 0.3f * Random(seed)),
		              size * (0.8f + 0.4f * Random(seed))};
		rock.spin = (Random(seed) - 0.5f) * 0.8f;
		rock.mesh = u32(Random(seed) * f32(ROCK_MESHES)) % ROCK_MESHES;
		// The start stays clear.
		if (std::abs(rock.position.x) > 12.0f || std::abs(rock.position.z) > 12.0f)
			rocks.push_back(rock);
	}

	audio::PlayDesc desc;
	desc.loop = true;
	desc.volume = 0.0f;
	engine = audio::Play(resources->thruster, desc);
}

void Flight::Leave()
{
	audio::Stop(engine);
	net::Close(client);
	client = {};
	latest.reset();
	previous.reset();
	incoming.reset();
}

bool Flight::OnEvent(const Event& event)
{
	if (event.type == EventType::KeyDown && !event.key.repeat)
		return event.key.scancode == Scancode::Escape || event.key.scancode == Scancode::AcBack;
	if (event.type == EventType::GamepadButtonDown)
		return event.gamepad.button == GamepadButton::Start ||
		       event.gamepad.button == GamepadButton::East;
	return false;
}

sim::ShipControls Flight::ReadControls(PixelSize size) const
{
	const auto key = [](Scancode a, Scancode b)
	{ return IsKeyDown(a) || IsKeyDown(b) ? 1.0f : 0.0f; };
	f32 turn = key(Scancode::A, Scancode::Left) - key(Scancode::D, Scancode::Right);
	f32 thrust = key(Scancode::W, Scancode::Up) - key(Scancode::S, Scancode::Down);

	GamepadState pads[MAX_GAMEPADS];
	const u32 count = GetGamepads(pads, MAX_GAMEPADS);
	for (u32 i = 0; i < count; ++i)
	{
		f32 x = pads[i].Axis(GamepadAxis::LeftX);
		f32 y = pads[i].Axis(GamepadAxis::LeftY);
		ApplyDeadZone(x, y);
		turn -= x;
		thrust +=
			y + pads[i].Axis(GamepadAxis::RightTrigger) - pads[i].Axis(GamepadAxis::LeftTrigger);
	}

	// A finger (or the held mouse) where the ship should go: the ship is in
	// the middle of the screen, so the way there is from the middle.
	const MouseState mouse = GetMouseState();
	if ((mouse.buttons & MouseButtonBit(MouseButton::Left)) && size.width > 0)
	{
		const f32 dx = mouse.x - 0.5f * f32(size.width);
		const f32 dy = 0.5f * f32(size.height) - mouse.y;
		const f32 distance = std::sqrt(dx * dx + dy * dy);
		if (distance > 0.04f * f32(size.height))
		{
			const sim::ShipState* state = latest ? latest->Find(ship) : nullptr;
			const f32 wanted = std::atan2(-dx, dy);
			const f32 off = WrapAngle(wanted - (state ? state->angle : 0.0f));
			turn += std::clamp(off * 2.5f, -1.0f, 1.0f);
			thrust +=
				std::abs(off) < 0.8f ? std::min(1.0f, distance / (0.3f * f32(size.height))) : 0.1f;
		}
	}
	return {std::clamp(turn, -1.0f, 1.0f), std::clamp(thrust, -1.0f, 1.0f)};
}

void Flight::SendControls(PixelSize size)
{
	if (!ship)
		return;
	// Every frame: unreliable, so the newest one wins.
	sim::Input input;
	input.tick = latest->tick;
	input.controls = ReadControls(size);
	const std::vector<u8> bytes = sim::Write(input);
	net::Send(client, bytes.data(), u32(bytes.size()), net::Delivery::Unreliable);
	const f32 push = std::abs(input.controls.thrust);
	audio::SetVolume(engine, 0.15f + 0.55f * push);
	audio::SetPitch(engine, 0.8f + 0.3f * push);
}

bool Flight::Receive(f32 dt)
{
	sinceSnapshot += dt;
	net::Event event;
	while (net::Poll(client, event))
	{
		if (event.type == net::EventType::Connected)
		{
			const std::vector<u8> hello = sim::Write(sim::Hello{});
			net::Send(client, hello.data(), u32(hello.size()), net::Delivery::Reliable);
		}
		else if (event.type == net::EventType::Disconnected)
		{
			PH_LOG_WARN("flight: the server is gone");
			return false;
		}
		else if (sim::TypeOf(event.data, event.size) == sim::MessageType::Welcome)
		{
			sim::Welcome welcome;
			if (sim::Read(event.data, event.size, welcome))
				ship = welcome.ship;
		}
		else if (sim::Read(event.data, event.size, *incoming))
		{
			// The newest becomes `latest`; the one before stays for drawing.
			std::swap(previous, latest);
			std::swap(latest, incoming);
			if (!previous->Find(ship))
				*previous = *latest; // the first: nothing to draw from yet
			sinceSnapshot = 0.0f;
		}
	}
	return true;
}

void Flight::Draw(rhi::CommandList& commands, const render::FrameTime& time, Ui& ui)
{
	if (!latest)
		return;
	const sim::ShipState* now = latest->Find(ship);
	const sim::ShipState* before = previous->Find(ship);
	if (!now || !before)
		return; // not in the game yet
	const f32 alpha = std::min(sinceSnapshot / sim::TICK_SECONDS, 1.0f);
	const Vec2 position = before->position + (now->position - before->position) * alpha;
	const f32 angle = before->angle + WrapAngle(now->angle - before->angle) * alpha;

	// From above, screen up along the plane's y (-Z).
	const Vec3 center = ToWorld(position);
	const Vec3 eye = {center.x, CAMERA_HEIGHT, center.z};
	const Mat4 view = LookAt(eye, center, {0.0f, 0.0f, -1.0f});
	render::FrameData frame = render::MakeFrameData3D(commands, time, view, eye, 0.8f);
	// A low sun from the upper left: shapes read better than under one from
	// straight above.
	constexpr f32 SUN_HEIGHT = 0.55f; // radians above the plane
	constexpr f32 SUN_YAW = -2.3f;
	frame.sunDirection[0] = std::cos(SUN_HEIGHT) * std::sin(SUN_YAW);
	frame.sunDirection[1] = std::sin(SUN_HEIGHT);
	frame.sunDirection[2] = std::cos(SUN_HEIGHT) * std::cos(SUN_YAW);
	render::SetFrameData(commands, frame);

	// Banked into turns, a little.
	const f32 bank = -0.5f * now->controls.turn;
	resources->DrawShip(commands, Translation(center) * RotationY(angle) * RotationZ(bank));
	const f32 t = f32(time.seconds);
	for (const Rock& rock : rocks)
	{
		// Only what can be on screen.
		if (std::abs(rock.position.x - center.x) > 80.0f ||
		    std::abs(rock.position.z - center.z) > 50.0f)
			continue;
		render::DrawMesh(commands, resources->rocks[rock.mesh], resources->rockMaterial,
		                 Translation(rock.position) * RotationY(rock.spin * t) *
		                     RotationX(0.7f * rock.spin * t) * Scale(rock.scale));
	}
	if (resources->sky)
		render::DrawSky(commands, resources->sky, frame, 1.0f);

	ui.Begin(commands, time, *resources);
	TextLook look;
	look.size = 22.0f;
	look.color = render::PackColor(0.85f, 0.9f, 1.0f);
	const f32 speed =
		std::sqrt(now->velocity.x * now->velocity.x + now->velocity.y * now->velocity.y);
	char number[16];
	std::snprintf(number, sizeof(number), "%.0f", speed);
	ui.Text(resources->strings.Format("flight.speed", number).c_str(), 32.0f, 28.0f, look);
	look.size = 16.0f;
	look.color = render::PackColor(0.45f, 0.5f, 0.6f);
	look.maxWidth = ui.Width() - 64.0f;
	const char* hint = resources->strings.Get("flight.hint");
	ui.Text(hint, 32.0f, Ui::HEIGHT - 28.0f - ui.Measure(hint, look).y, look);
	ui.End(commands);
}
} // namespace sn
