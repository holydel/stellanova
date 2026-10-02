// Stella Nova's client (docs/vision.md): the main menu, and a ship flown by
// hand in a starfield (roadmap M1.1). pith's shell runs the window, the
// device, the frame and the platform (Steam, as the game's own app). Options:
// the shell's (ph/shell/shell.h), and
//   --flight     start flying, past the menu (for screenshots and tests)
//   --online     start flying on the game's server (docs/adr/0010-online-server.md)
//   --server ADDRESS  start flying on that server: udp:<host>:<port>, or in
//                browsers ws://... or wss://...
//   --autopilot  our ship flies itself, as the bots do (demos, tests)
//   --mute       no sound (tests; --screenshot runs are silent too)

#include "flight.h"
#include "menu.h"
#include "resources.h"
#include "settings.h"

#include <sn/server/server.h>

#include <ph/audio/audio.h>
#include <ph/core/log.h>
#include <ph/core/profile.h>
#include <ph/core/time.h>
#include <ph/shell/shell.h>

#include <cstring>

namespace
{
using namespace ph;
using namespace ph::os;

constexpr u32 STEAM_APP_ID = 1096260;
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
				screen = Screen::Flight;
			else if (std::strcmp(args.argv[i], "--online") == 0)
			{
				screen = Screen::Flight;
				address = ONLINE_SERVER;
			}
			else if (std::strcmp(args.argv[i], "--server") == 0 && i + 1 < args.argc)
			{
				screen = Screen::Flight;
				address = args.argv[++i];
			}
			else if (std::strcmp(args.argv[i], "--autopilot") == 0)
				match.autopilot = true;
			else if (std::strcmp(args.argv[i], "--mute") == 0 ||
			         std::strcmp(args.argv[i], "--screenshot") == 0)
				muted = true;
		}
		audio::Init();
		settings.Load();
		resources.pack.Request("stellanova.pak");
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
		if (screen == Screen::Menu)
			return Act(menu.OnEvent(event));
		if (flight.OnEvent(event))
			OpenMenu();
		return AppResult::Continue;
	}

	AppResult Tick() override
	{
		PH_PROFILE_SCOPE("Game.Tick");
		if (!ready && !Load())
			return AppResult::Failure;
		audio::Update();
		const f32 dt = f32(shell.GetFrameTime().delta);
		AppResult result = AppResult::Continue;
		if (ready && screen == Screen::Menu)
			result = Act(menu.Update(dt));
		rhi::CommandList commands;
		if (shell.BeginFrame(commands, {0.0f, 0.0f, 0.0f, 1.0f}))
		{
			if (ready && screen == Screen::Menu)
				menu.Draw(commands, shell.GetFrameTime(), ui);
			else if (ready)
			{
				// Our controls, the local server's ticks (none online), the
				// answers.
				flight.SendControls(commands.size);
				{
					PH_PROFILE_SCOPE("Server.Update");
					server.Update(dt);
				}
				if (!flight.Receive())
				{
					const bool welcomed = flight.WasWelcomed();
					OpenMenu();
					menu.Notice(welcomed ? "menu.server_gone" : "menu.unreachable");
				}
				else
					flight.Draw(commands, shell.GetFrameTime(), ui);
			}
			if (ready && !shown)
			{
				shown = true;
				PH_LOG_INFO("game: starfield shown %.0f ms after startup",
				            UptimeSeconds() * 1000.0);
			}
			shell.EndFrame(commands, [](void* self) { static_cast<Game*>(self)->DrawUi(); }, this);
		}
		return result != AppResult::Continue ? result : shell.TickResult();
	}

	void Quit(AppResult result) override
	{
		flight.Leave();
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
		Flight,
	};

	// Beside F1's diagnostics (Debug and Dev builds): the connection's, in
	// flight.
	void DrawUi()
	{
		if (ready && screen == Screen::Flight)
			flight.DrawNetworkWindow();
	}

	// Once the pack, the device and the audio are there. False on failure.
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
		if (!resources.Load(shell.GetSceneFormat()))
			return false;
		settings.Apply(shell.GetWindow());
		if (muted)
			audio::SetMasterVolume(0.0f);
		if (screen == Screen::Menu)
			menu.Enter(resources, settings, shell.GetWindow());
		else
			StartSkirmish(address);
		ready = true;
		return true;
	}

	// On the local server, which starts here, or on another one.
	void StartSkirmish(const char* where)
	{
		PH_LOG_INFO("game: flight on %s", where);
		screen = Screen::Flight;
		if (std::strcmp(where, LOCAL_SERVER) == 0)
			server.Start(LOCAL_SERVER, match);
		flight.Enter(resources, where);
	}

	void OpenMenu()
	{
		PH_LOG_INFO("game: menu");
		flight.Leave();
		server.Stop();
		screen = Screen::Menu;
		menu.Enter(resources, settings, shell.GetWindow());
	}

	AppResult Act(sn::Menu::Action action)
	{
		switch (action)
		{
			case sn::Menu::Action::None: break;
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
	sn::Flight flight;
	sn::server::Server server; // the local game's
	sn::server::MatchDesc match;
	Screen screen = Screen::Menu;
	const char* address = LOCAL_SERVER; // where --flight, --online or --server fly
	bool ready = false;
	bool shown = false;
	bool muted = false;
};
} // namespace

ph::os::App& ph::os::GetApp()
{
	static Game game;
	return game;
}
