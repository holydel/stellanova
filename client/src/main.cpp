// Stella Nova's client (docs/vision.md, docs/solo-loop.md): the main menu,
// the campaign's hub (the solo loop, on the game's server), and flight: the
// skirmish, or a battle of the campaign. pith's shell runs the window, the
// device, the frame and the platform (Steam, as the game's own app). Options:
// the shell's (ph/shell/shell.h), and
//   --flight     start flying, past the menu (for screenshots and tests)
//   --online     start flying on the game's server (docs/adr/0010-online-server.md)
//   --server ADDRESS  start flying on that server: udp:<host>:<port>, or in
//                browsers ws://... or wss://...; with --campaign, its hub
//   --campaign   start in the campaign's hub (docs/adr/0014-accounts-on-the-game-server.md)
//   --local      the campaign on this machine: a practice account in the
//                data folder, apart from the online one (development)
//   --tab NAME   the hub's tab to start on: colony, hangar, storage, map
//   --inventory-test  the local hub on an account kept in memory, with many
//                things to drag, on its inventory (docs/adr/0017-inventory.md)
//   --battle     with --campaign: launch the first ship at once
//   --autopilot  our ship flies itself, as the bots do (demos, tests)
//   --lag MS     a worse network, to try prediction: MS more each round trip
//   --loss PERCENT  and that share of snapshots and inputs lost
//   --mute       no sound (tests; --screenshot runs are silent too)
//   --playground  the menus' test bench (docs/adr/0018-one-interface-every-device.md):
//                the hub as --inventory-test, a Playground window (the style,
//                the interface's size, a jump to any screen) and pith's
//                diagnostics window, whose Device pretends to be a phone or a
//                Deck, with touch or a pad (also --device, --touch, --pad)

#include "connection.h"
#include "flight.h"
#include "hub.h"
#include "input_kind.h"
#include "inventory_items.h"
#include "menu.h"
#include "resources.h"
#include "settings.h"

#include <sn/server/server.h>

#include <ph/audio/audio.h>
#include <ph/core/log.h>
#include <ph/core/profile.h>
#include <ph/core/time.h>
#include <ph/os/app.h>
#include <ph/os/window.h>
#include <ph/platform/platform.h>
#include <ph/shell/shell.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>

namespace
{
using namespace ph;
using namespace ph::os;
namespace sim = sn::sim;

constexpr u32 STEAM_APP_ID = 1096260;
// The splash's pack, in builds that have it (client/CMakeLists.txt).
constexpr bool ART_PACK = SN_ART_PACK;
// Where a local game's server listens: in this process.
constexpr const char* LOCAL_SERVER = "loopback:stellanova";
// The game's server, on the developer's machine: UDP, and for browsers,
// which have none, WebSocket through the site's TLS.
#if PH_OS_WEB
constexpr const char* ONLINE_SERVER = "wss://wos-observer.com/stellanova/ws";
#else
constexpr const char* ONLINE_SERVER = "udp:wos-observer.com:27015";
#endif

class Game final : public App
{
public:
	AppResult Init(const AppArgs& args) override
	{
		shell::ShellOptions options;
		options.title = "Stella Nova";
		options.steamAppId = STEAM_APP_ID;
		options.devControls = false;
		options.depth = true;
		const AppResult result = shell.Init(args, "stellanova", options);
		for (int i = 1; i < args.argc; ++i)
		{
			if (std::strcmp(args.argv[i], "--flight") == 0)
				start = Screen::Flight;
			else if (std::strcmp(args.argv[i], "--online") == 0)
			{
				start = Screen::Flight;
				address = ONLINE_SERVER;
			}
			else if (std::strcmp(args.argv[i], "--server") == 0 && i + 1 < args.argc)
			{
				if (start == Screen::Menu)
					start = Screen::Flight;
				address = args.argv[++i];
				campaignAddress = address;
			}
			else if (std::strcmp(args.argv[i], "--campaign") == 0)
				start = Screen::Hub;
			else if (std::strcmp(args.argv[i], "--local") == 0)
				campaignAddress = LOCAL_SERVER;
			else if (std::strcmp(args.argv[i], "--tab") == 0 && i + 1 < args.argc)
				hub.ShowTab(args.argv[++i]);
			else if (std::strcmp(args.argv[i], "--inventory-test") == 0)
			{
				start = Screen::Hub;
				campaignAddress = LOCAL_SERVER;
				inventoryTest = true;
				hub.ShowTab("storage");
			}
			else if (std::strcmp(args.argv[i], "--playground") == 0)
			{
				start = Screen::Hub;
				campaignAddress = LOCAL_SERVER;
				inventoryTest = true;
				playground = true;
			}
			else if (std::strcmp(args.argv[i], "--battle") == 0)
			{
				start = Screen::Hub;
				hub.LaunchOnce();
			}
			else if (std::strcmp(args.argv[i], "--autopilot") == 0)
				match.autopilot = true;
			else if (std::strcmp(args.argv[i], "--lag") == 0 && i + 1 < args.argc)
				lagMs = f32(std::atof(args.argv[++i]));
			else if (std::strcmp(args.argv[i], "--loss") == 0 && i + 1 < args.argc)
				lossPercent = f32(std::atof(args.argv[++i]));
			else if (std::strcmp(args.argv[i], "--mute") == 0)
				muted = true;
			else if (std::strcmp(args.argv[i], "--screenshot") == 0)
				muted = screenshot = true;
		}
		connection.SimulateNetwork(lagMs * 0.001f, lossPercent * 0.01f);
		// The playground's windows, but not in screenshots (tests).
		playground = playground && !screenshot;
		if (playground && !PH_OS_ANDROID)
			shell.ShowDiagnostics(true); // its Device section (a phone is the real thing)
		audio::Init();
		settings.Load();
		// Both packs are read at once, in the background; the menu waits for
		// its splash, so that it never shows without it.
		resources.pack.Request("stellanova.pak");
		if (ART_PACK)
			resources.art.Request("stellanova-art.pak");
		return result;
	}

	AppResult OnEvent(const Event& event) override
	{
		if (event.type == EventType::AppWillEnterBackground)
			audio::SetPaused(true);
		else if (event.type == EventType::AppDidEnterForeground)
			audio::SetPaused(false);
		const AppResult result = shell.OnEvent(event);
		if (result != AppResult::Continue || !ready || shell.UiTookEvent())
			return result;
		input.OnEvent(event, IsTextInputActive());
		switch (screen)
		{
			case Screen::Menu: return Act(menu.OnEvent(event));
			case Screen::Hub:
				if (hub.OnEvent(event) == sn::Hub::Action::Leave)
					OpenMenu();
				break;
			case Screen::Flight:
				switch (flight.OnEvent(event))
				{
					case sn::Flight::Action::None: break;
					case sn::Flight::Action::Leave: OpenMenu(); break;
					case sn::Flight::Action::Return:
					{
						sim::Operation home;
						home.kind = sim::Operation::Kind::Return;
						connection.Post(sim::Write(sim::Request{sim::ToJson(home)}),
						                net::Delivery::Reliable);
						break;
					}
				}
				break;
		}
		return AppResult::Continue;
	}

	AppResult Tick() override
	{
		PH_PROFILE_SCOPE("Game.Tick");
		if (!ready && !Load())
			return AppResult::Failure;
		audio::Update();
		if (ready)
			Jump();
		const f32 dt = f32(shell.GetFrameTime().delta);
		AppResult result = AppResult::Continue;
		if (ready && screen == Screen::Menu)
			result = Act(menu.Update(dt));
		// The interface's style and size (Settings, the playground).
		ui.SetSize(settings.interfaceSize);
		hub.SetLook(settings.ResolvedStyle(shell.GetWindow()) == sn::Style::Mobile,
		            settings.interfaceSize);
		rhi::CommandList commands;
		if (shell.BeginFrame(commands, {0.0f, 0.0f, 0.0f, 1.0f}))
		{
			if (ready && screen != Screen::Menu)
			{
				// Our controls, the local server's ticks (none online), the
				// answers.
				if (screen == Screen::Flight)
					flight.SendControls(commands.size, dt);
				{
					PH_PROFILE_SCOPE("Server.Update");
					server.Update(dt);
				}
				Pump();
			}
			if (ready && screen == Screen::Menu)
			{
				menu.SetInput(input.Last());
				menu.Draw(commands, shell.GetFrameTime(), ui);
			}
			else if (ready && screen == Screen::Hub)
			{
				hub.SetDensity(GetWindowScale(shell.GetWindow()));
				hub.SetInsets(GetSafeInsets(shell.GetWindow()));
				hub.Draw(commands, shell.GetFrameTime());
				if (hub.TakeLeave())
					OpenMenu();
			}
			else if (ready)
				flight.Draw(commands, shell.GetFrameTime(), ui);
			if (ready && !shown)
			{
				shown = true;
				PH_LOG_INFO("game: first frame shown %.0f ms after startup",
				            UptimeSeconds() * 1000.0);
			}
			shell.EndFrame(commands, [](void* self) { static_cast<Game*>(self)->DrawUi(); }, this);
		}
		return result != AppResult::Continue ? result : shell.TickResult();
	}

	void Quit(AppResult result) override
	{
		flight.Leave();
		connection.Close();
		server.Stop();
		audio::Shutdown(); // before the pack: sounds read its bytes in place
		rhi::WaitIdle();
		if (ready)
			resources.Destroy();
		shell.Quit(result);
	}

private:
	enum class Screen : u8
	{
		Menu,
		Hub,
		Flight,
	};

	// Beside F1's diagnostics (Debug and Dev builds): the connection's, and
	// the playground's.
	void DrawUi()
	{
		if (ready && screen != Screen::Menu)
			flight.DrawNetworkWindow();
		if (ready && playground)
			DrawPlayground();
	}

	// Where the playground goes next: taken at the next tick, between frames.
	enum class Destination : u8
	{
		None,
		Menu,
		Settings,
		Credits,
		Colony,
		Fitting,
		Inventory,
		Map,
		Battle,
	};

	// The playground (--playground): the interface's style and size, the
	// input seen last, and every screen a click away. The screen, touch and
	// the pad are pith's (its diagnostics window, Device).
	void DrawPlayground()
	{
#if PH_ENABLE_DEBUG_UI
		ImGui::SetNextWindowPos(ImVec2(16.0f, 420.0f), ImGuiCond_FirstUseEver);
		ImGui::Begin("Playground", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
		int style = int(settings.style);
		bool changed = ImGui::RadioButton("Auto", &style, int(sn::Style::Auto));
		ImGui::SameLine();
		changed |= ImGui::RadioButton("Desktop", &style, int(sn::Style::Desktop));
		ImGui::SameLine();
		changed |= ImGui::RadioButton("Mobile", &style, int(sn::Style::Mobile));
		ImGui::SameLine();
		ImGui::TextDisabled("style: %s", sn::StyleName(settings.ResolvedStyle(shell.GetWindow())));
		int size = int(std::lround(settings.interfaceSize * 10.0f));
		changed |= ImGui::SliderInt("Interface size", &size,
		                            int(std::lround(sn::MIN_INTERFACE_SIZE * 10.0f)),
		                            int(std::lround(sn::MAX_INTERFACE_SIZE * 10.0f)), "%d0%%");
		if (changed)
		{
			settings.style = sn::Style(style);
			settings.interfaceSize = f32(size) / 10.0f;
			settings.Save();
		}
		ImGui::Text("Last input: %s", sn::InputKindName(input.Last()));
		ImGui::SeparatorText("Go to");
		constexpr struct
		{
			const char* label;
			Destination destination;
		} BUTTONS[] = {
			{"Main menu", Destination::Menu},  {"Settings", Destination::Settings},
			{"Credits", Destination::Credits}, {"Colony", Destination::Colony},
			{"Fitting", Destination::Fitting}, {"Inventory", Destination::Inventory},
			{"Star map", Destination::Map},    {"Battle", Destination::Battle},
		};
		for (u32 i = 0; i < std::size(BUTTONS); ++i)
		{
			if (i == 1 || i == 2 || i == 4 || i == 5 || i == 6)
				ImGui::SameLine();
			if (ImGui::Button(BUTTONS[i].label))
				destination = BUTTONS[i].destination;
		}
	#if !PH_OS_ANDROID
		ImGui::TextDisabled("A phone or a Deck, touch, a pad: the pith window's Device (F1)");
	#endif
		ImGui::End();
#endif
	}

	// The playground's jump: the menu and its pages, or the hub's tabs and a
	// battle, on the test account (kept in memory across the menu).
	void Jump()
	{
		const Destination to = destination;
		destination = Destination::None;
		switch (to)
		{
			case Destination::None: return;
			case Destination::Menu:
			case Destination::Settings:
			case Destination::Credits:
				OpenMenu();
				if (to != Destination::Menu)
					menu.OpenPage(to == Destination::Credits);
				return;
			default: break;
		}
		if (screen == Screen::Flight)
			OpenMenu();
		if (screen == Screen::Menu)
		{
			menu.Leave();
			OpenCampaign();
		}
		hub.ShowTab(to == Destination::Fitting     ? "hangar"
		            : to == Destination::Inventory ? "storage"
		            : to == Destination::Map       ? "map"
		                                           : "colony");
		if (to == Destination::Battle)
			hub.LaunchOnce();
	}

	// Before any input: a pad on the Deck or with a pad there, touch on phones
	// (and a desktop pretending to be one), else the mouse.
	sn::InputKind FirstInput() const
	{
		const DeviceSimulation& simulation = GetDeviceSimulation();
		if (simulation.touch || PH_OS_ANDROID)
			return sn::InputKind::Touch;
		if (simulation.pad)
			return sn::InputKind::Pad;
		// Screenshots (tests) look the same with a pad plugged in or not.
		GamepadState pads[MAX_GAMEPADS];
		if (!screenshot && (std::strcmp(platform::GetHardwareName(), "Steam Deck") == 0 ||
		                    GetGamepads(pads, MAX_GAMEPADS) > 0))
			return sn::InputKind::Pad;
		return sn::InputKind::Mouse;
	}

	// Once the packs, the device and the audio are there. False on failure.
	bool Load()
	{
		if (!shell.GetSwapchain() || audio::Poll() == AsyncStatus::Pending)
			return true;
		switch (resources.pack.Poll())
		{
			case AsyncStatus::Pending: return true;
			case AsyncStatus::Failed: return false;
			case AsyncStatus::Ready: break;
		}
		if (ART_PACK && resources.art.Poll() == AsyncStatus::Pending)
			return true;
		if (!resources.Load(shell.GetSceneFormat()))
			return false;
		// Without its splash (a build that lacks the pack), the menu shows
		// its items on the dark.
		if (ART_PACK && resources.art.Poll() == AsyncStatus::Ready && resources.LoadArt())
			PH_LOG_INFO("game: splash %ux%u", resources.splashWidth, resources.splashHeight);
		else if (ART_PACK)
			PH_LOG_WARN("game: no splash (stellanova-art.pak)");
		settings.Apply(shell.GetWindow());
		if (muted)
			audio::SetMasterVolume(0.0f);
		input.Reset(FirstInput());
		ready = true;
		switch (start)
		{
			case Screen::Menu: menu.Enter(resources, settings, shell.GetWindow()); break;
			case Screen::Hub: OpenCampaign(); break;
			case Screen::Flight: StartSkirmish(address); break;
		}
		return true;
	}

	// On the local server, which starts here, or on another one.
	void StartSkirmish(const char* where)
	{
		PH_LOG_INFO("game: flight on %s", where);
		if (std::strcmp(where, LOCAL_SERVER) == 0)
			server.Start(LOCAL_SERVER, match);
		connection.Open(where, sim::Joining::Skirmish);
		screen = Screen::Flight;
		flight.Enter(resources, connection, shell.GetWindow());
	}

	// The campaign's hub: on the game's server, or on this machine with a
	// practice account (--local).
	void OpenCampaign()
	{
		PH_LOG_INFO("game: the campaign on %s", campaignAddress);
		if (std::strcmp(campaignAddress, LOCAL_SERVER) == 0)
		{
			if (!store && inventoryTest)
				store = std::make_unique<sn::server::MemoryStore>();
			if (!store)
			{
#if PH_OS_WEB
				store = std::make_unique<sn::server::MemoryStore>();
#else
				store = std::make_unique<sn::server::FileStore>(std::string(GetDataDirectory()) +
				                                                "practice");
#endif
			}
			sn::server::HubDesc desc;
			desc.store = store.get();
			desc.keepGuests = true; // practice: no sign-in, and kept
			if (inventoryTest)
				desc.seed = [](sim::Account& account)
				{ sn::SeedInventoryTest(sim::GetCatalog(), account); };
			sn::server::MatchDesc quiet = match;
			quiet.waves = false;
			server.Start(LOCAL_SERVER, quiet, desc);
		}
		connection.Open(campaignAddress, sim::Joining::Hub);
		screen = Screen::Hub;
		hub.Enter(resources, connection);
	}

	// What came from the server, to the screen it is for: a battle's Welcome
	// takes the hub to its flight, its Result brings it back.
	void Pump()
	{
		if (connection.Receive([](void* self, const u8* data, u32 size)
		                       { static_cast<Game*>(self)->Dispatch(data, size); }, this))
			return;
		sim::RefusalReason reason;
		const char* notice = connection.WasRefused(reason)
		                         ? (reason == sim::RefusalReason::Full ? "menu.refused_full"
		                                                               : "menu.refused_version")
		                     : connection.WasAnswered() ? "menu.server_gone"
		                                                : "menu.unreachable";
		OpenMenu();
		menu.Notice(notice);
	}

	void Dispatch(const u8* data, u32 size)
	{
		switch (sim::TypeOf(data, size))
		{
			case sim::MessageType::Signed:
			case sim::MessageType::Profile: hub.OnMessage(data, size); break;
			case sim::MessageType::Result:
				hub.OnMessage(data, size);
				if (screen == Screen::Flight)
				{
					flight.Leave();
					screen = Screen::Hub;
				}
				break;
			case sim::MessageType::Welcome:
				if (screen == Screen::Hub)
				{
					hub.Leave();
					flight.Enter(resources, connection, shell.GetWindow());
					screen = Screen::Flight;
				}
				flight.OnMessage(data, size);
				break;
			case sim::MessageType::Chat:
				if (screen == Screen::Flight)
					flight.OnMessage(data, size);
				else
					hub.OnMessage(data, size);
				break;
			default:
				if (screen == Screen::Flight)
					flight.OnMessage(data, size);
				break;
		}
	}

	void OpenMenu()
	{
		PH_LOG_INFO("game: menu");
		flight.Leave();
		hub.Leave();
		connection.Close();
		server.Stop();
		screen = Screen::Menu;
		menu.Enter(resources, settings, shell.GetWindow());
	}

	AppResult Act(sn::Menu::Action action)
	{
		switch (action)
		{
			case sn::Menu::Action::None: break;
			case sn::Menu::Action::Campaign:
				menu.Leave();
				OpenCampaign();
				break;
			case sn::Menu::Action::Play:
				menu.Leave();
				StartSkirmish(LOCAL_SERVER);
				break;
			case sn::Menu::Action::PlayOnline:
				menu.Leave();
				StartSkirmish(ONLINE_SERVER);
				break;
			case sn::Menu::Action::Quit: return AppResult::Success;
		}
		return AppResult::Continue;
	}

	shell::Shell shell;
	sn::Resources resources;
	sn::Settings settings;
	sn::Ui ui;
	sn::Menu menu;
	sn::Hub hub;
	sn::Flight flight;
	sn::Connection connection;
	sn::server::Server server;                       // the local game's
	std::unique_ptr<sn::server::AccountStore> store; // the local campaign's
	sn::server::MatchDesc match;
	Screen screen = Screen::Menu;
	Screen start = Screen::Menu;
	const char* address = LOCAL_SERVER;          // where --flight, --online or --server fly
	const char* campaignAddress = ONLINE_SERVER; // where the campaign plays
	bool ready = false;
	bool shown = false;
	bool muted = false;
	bool screenshot = false;    // --screenshot
	bool inventoryTest = false; // --inventory-test: a seeded account in memory
	f32 lagMs = 0.0f;           // --lag, --loss
	f32 lossPercent = 0.0f;
	sn::InputTracker input;
	bool playground = false; // --playground
	Destination destination = Destination::None;
};
} // namespace

ph::os::App& ph::os::GetApp()
{
	static Game game;
	return game;
}
