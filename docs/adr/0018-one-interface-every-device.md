# 0018. One interface for every screen and every way of playing

- Status: Proposed; the playground built (2026-10-06)
- Date: 2026-10-05

## Context

- The developer (2026-10-05): menus and their controls should be written
  once, at a high level, and work on
  - big screens (PC), small landscape handheld screens (the Steam Deck), and
    phones held either way;
  - keyboard and mouse, gamepads (the Deck's, PC pads) and touch.
- These mix. The Deck has a touchscreen as well as its controls, a phone can
  take a Bluetooth pad, and a PC can have a pad. Steam's Deck Verified
  checks ask that the default controls reach everything, that the glyphs
  match the device, that text reads at 1280x800, and that mouse and pad
  work together (pith's `docs/targets/steam-linux.md`).
- Today each screen handles devices by itself:
  - **Main menu, settings, credits** (`menu.cpp`): their own lists, drawn
    on the canvas by the game's `Ui`, with their own keys, pad (polled
    stick with repeat), mouse and tap handling.
  - **Hub** (ph::ui): pith moves focus with the arrows, d-pad and stick,
    and Return, Space or A press. A pad cannot switch tabs or drag, and
    only Back (Esc, B) is the hub's own.
  - **Flight:** polls keys, the pad and the mouse; its HUD buttons are
    pixel rectangles.
- pith already provides:
  - events for keys, mouse, touch (the first finger also moves the mouse,
    marked `touch`) and pads (buttons named by position, the pad's type
    with its labels);
  - `GetWindowScale`, `GetSafeInsets`, and `platform::GetHardwareName`
    ("Steam Deck");
  - ph::ui's directional focus, touch rules (swipe, fling, hold) and
    `Theme::touchSize`.
- pith lacks:
  - actions and bindings (its planned `ph::input`, pith ADR 0027);
  - a focus look apart from hover, and focus scopes.
- The developer's answers (2026-10-06):
  - two clear styles, desktop and mobile, with a size setting on top;
  - prompts for the pad's buttons (A B X Y, the bumpers), and for keys at
    most Enter and Esc;
  - no carrying items with a pad ("no idea how people can carry items with
    a gamepad");
  - a playground to try the menus as each device, on Windows.

## Decision

Each frame the game knows two things:

- **The style:** how the screen lays out.
- **The input:** what the player used last.

Screens are built from a small kit that reads both, so they never ask
which device the player has.

### The style: Desktop or Mobile

- **Desktop:** big screens and the Deck. Tabs across the top, panels side
  by side, hover and tooltips, room for a mouse's precision.
- **Mobile:** phones, held either way. Touch first:
  - tabs in a bar at the bottom;
  - panels one under another;
  - targets at least 48 units (`Theme::touchSize`);
  - nothing shown only on hover: tooltips also open by a hold or the Info
    action.
- **Auto** (the default) picks by the device:
  - Mobile on Android;
  - Mobile on the web when the screen's short side is under 600 units;
  - Mobile on a desktop pretending to be a phone, with touch (pith's
    device simulation);
  - Desktop elsewhere.

  `Settings::style` can force either style (the playground does).
- **Units** stay the hub's: pixels per unit is the larger of fitting
  1600x900 and the OS's density (a phone's dp). The **Interface size**
  setting multiplies it: 80 to 150% in tenths, in the main menu's Settings
  and in the playground.
- **Phones held sideways** are Mobile too. Their screen is short (about 360
  units), so the bottom bar takes much of it; a rail of tabs on the left
  may suit them better (open, below).

### The input: the device used last

- **Kinds:** Mouse, Touch, Pad and Keys, set by the last meaningful event
  (`client/src/input_kind.h`):

  | Input | Set by |
  | --- | --- |
  | Touch | a touch, or mouse events marked `touch` |
  | Mouse | the mouse moving a few pixels, a click or the wheel |
  | Pad | a pad button, or a stick or trigger past half |
  | Keys | a key (not while typing in a text field) |

- **At start:** Touch on phones (and with simulated touch); Pad with a
  simulated pad, on the Deck, or with a pad connected (not in screenshot
  runs); Mouse otherwise.
- **Every device always works** (the Deck Verified rule). The input
  changes only how screens look:
  - **Pad:**
    - a focus ring, distinct from hover, on an item that keeps its focus
      across frames;
    - prompts with the pad's own buttons, from its type: A, B, X, Y, LB,
      RB, View, Menu on Xbox pads and the Deck.
  - **Keys:** the focus ring; prompts for Enter and Esc only.
  - **Mouse:** hover, tooltips, a right-click for context; no prompts:
    every action has a visible button.
  - **Touch:** no hover, a hold for context, bigger targets; no prompts but
    a tap's hint.

### Actions: what screens read, with every binding in one table

- **Interface actions:**

  | Action | Keys | Pad | Mouse | Touch |
  | --- | --- | --- | --- | --- |
  | Confirm | Enter, Space | A (south) | click | tap |
  | Back | Esc, Backspace | B (east) | the back button | the back button, Android's Back |
  | Context | Shift+Enter, the Menu key | X (west) | right-click | hold, then let go |
  | Info | I | Y (north) | hover | hold |
  | Tab before, after | Q, E (Ctrl+Tab) | LB, RB | the tabs | the tabs |
  | Move focus | arrows, WASD | d-pad, left stick | (none) | (none) |
  | Scroll | wheel, Page Up/Down | right stick | wheel | swipe |
  | Pause | Esc in flight | Start | a button | a button |

- **Flight's actions** join the same table later: turn, thrust, reverse,
  fire, chat, zoom, and the move and attack orders. The table moves into
  pith as `ph::input` when pith builds it (with Steam Input's action
  manifest); the game then keeps only its list of actions.
- **Rebinding** comes later, from the same table (Settings > Controls).
- **Glyphs:** to start, the button's name in a small rounded box
  (`GamepadButtonName` by pad type). Later, drawn glyphs in the icon font.

### The kit: each screen written once

- **On ph::ui**, as the hub is. The main menu, settings and credits move
  onto it from their own lists and drawing, so every screen shares the
  same focus, touch and mouse rules. The splash stays behind them.
- **A screen** has a title, a body, its actions with labels, and what Back
  does. The kit draws, by input and style:
  - the title and a back button (Mouse, Touch);
  - the prompts (Pad, Keys).

  It takes Back from every source: Esc, B, Android's Back and the button.
- **Pieces:**
  - **Menu:** a list of entries: action, submenu, toggle, slider, choice,
    back.
    - A slider steps with left and right (Pad, Keys), drags (Mouse), and
      has - and + buttons (Touch).
    - Pages stack. Back pops and gives the focus back to the entry that
      opened the page.
  - **Tabs:** placed by style; LB/RB and Q/E switch them; the focus goes
    into the tab's content.
  - **Split:** a list with its details beside it (Desktop), or the details
    over the list after a pick (Mobile, where Back returns).
  - **Grid:** tiles (the inventory, blueprints), with focus and an actions
    list.
  - **Dialog:** keeps the focus inside it; Back cancels.
  - **Prompts.**
- **Default focus:** the first entry, or the entry the screen names (such
  as Launch). It applies when a screen opens, and when Pad or Keys takes
  over from the mouse or touch.
- **Example: settings, written once:**

  ```cpp
  void MainMenu::Settings(Kit& kit)
  {
      const Screen screen = kit.BeginScreen("menu.settings");
      kit.Slider("settings.music", settings.music);
      kit.Slider("settings.effects", settings.effects);
      kit.Toggle("settings.fullscreen", settings.fullscreen);
      kit.Slider("settings.interface_size", settings.interfaceSize, 0.8f, 1.5f);
      if (kit.EndScreen(screen) == ScreenResult::Back)
          Pop();
  }
  ```

### Moving items without a mouse: an actions list

- Dragging stays for the mouse and for touch (a hold, then moving;
  ADR 0017).
- **An item's actions** cover the same moves, for every input: Confirm on a
  tile (Pad, Keys), a double click (Mouse), or a hold let go without moving
  (Touch) opens a list such as
  - "Fit to Lancer #1", "Fit to Lancer #2";
  - "Load into Lancer #1";
  - "Move to storage", "Unfit";
  - "Details".

  Each is one of the server's operations, as a drop is. Console games do
  the same; nothing is carried.

### pith's part (generic)

- In ph::ui:
  - a focus look apart from hover; `Navigating()` made public;
  - focus scopes: a dialog keeps the focus, and the tab bar and the content
    are separate scopes;
  - setting the focus (the default focus);
  - `Reveal` scrolling sideways too;
  - B and Esc reaching the app as Back, not only as a drag's cancel.
- Device simulation (pith ADR 0027's addendum of 2026-10-06), for the
  playground.

### The playground

The developer (2026-10-06): a Windows build that can pretend to have a pad
or a touchscreen, and switch between the mobile and desktop interfaces.

- **`stellanova --playground`** (Debug and Dev builds) opens:
  - the hub on the seeded account of `--inventory-test` (in memory, kept
    across the menu);
  - pith's diagnostics window, whose **Device** section pretends to be
    - a Steam Deck (1280x800, with its pad);
    - a phone upright (360x780 dp, with a finger);
    - a phone sideways (780x360 dp, with a finger);
    - or this computer;

    It also turns touch and a pad (Xbox or Deck, driven by the keyboard)
    on or off on their own.
  - a **Playground** window: the style (Auto, Desktop, Mobile, and what
    Auto chose), the interface's size, the input seen last, and a jump to
    any screen (main menu, settings, credits, each hub tab, a battle).
- **The same from the command line**, for screenshots and tests: pith's
  `--device`, `--touch` and `--pad`, for example
  `--playground --device phone-wide --tab storage`.
- The pretended window keeps its shape (no fullscreen), and the debug
  windows keep the computer's size.

### Tests

- **Screenshot runs of each device** (`--device`, `--touch`, `--pad`): the
  main menu, each hub tab and a dialog.
- **Input injected** (`os::PushEvent`) in `sn_tests`:
  - a pad walks the main menu to Settings and back;
  - Q and E switch the hub's tabs;
  - an item's action fits a module.

## Consequences

- Each screen has one code path. A new screen is built from the kit and
  never asks about devices.
- The main menu's own drawing goes: its look becomes ph::ui's theme (the
  hub's house style), over the splash.
- Flight's Return and Chat buttons become kit buttons; its steering moves
  to the action table later.
- A narrow desktop window keeps the Desktop style (it no longer turns
  compact by its width); Mobile is for phones, or chosen.
- **Work order:**
  1. Done: the playground; the style and the interface's size; the input
     seen last; the main menu's hint by input.
  2. In pith's ph::ui: the focus look, focus scopes and the default focus.
  3. The main menu, settings and credits on the kit, with the prompts.
  4. The hub: tabs switched by shoulder buttons, items' actions lists,
     sideways phones.
  5. Flight's actions and HUD buttons.
  6. Rebinding, drawn glyphs, Steam Input actions.

## Open questions

- Phones held sideways: tabs as a left rail, or the bottom bar as upright?
  (Try both in the playground: `--device phone-wide`.)

## Built on 2026-10-06

- pith: device simulation (`ph/os/simulation.h`), the shell's Device
  section and `--device`, `--touch`, `--pad`; tested in pith's
  `input_test.cpp`.
- The game:
  - `Settings::style` and `Settings::interfaceSize` (`settings.txt`), and
    the Interface size entry in the main menu's settings;
  - the hub lays out by the style, no longer by its width;
  - `InputTracker` (`input_kind.h`);
  - the main menu's hint by input: the pad's buttons, Enter and Esc for
    keys, a tap for fingers, none for a mouse;
  - `--playground`.
