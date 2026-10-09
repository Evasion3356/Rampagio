# Hotkeys: keyboard, mouse and gamepad

Built 2026-10-09; the F11 window is live-tested (checklist item 0), the
rest isn't yet. Our own design (Rampage's code wasn't
read for it); it grows HorseMenu's `HotkeySystem`, already in
`src/core/commands`.

## Decisions (user, 2026-10-09)

- **Many keys, one command.** A command can have several bindings (a
  keyboard chain and a pad chain, say), but a chain runs one command.
  Binding a chain another command already has asks first: bind it again to
  replace. One key doing several things is a **preset**: a command whose
  steps run other commands (`preset.<slug>`), bound like any other.
- Every extra goes in, each a per-binding choice the user makes: hold
  mode, next/previous/set value, the pad layer button, tap vs long press.

## Model

- **Inputs** share one 32-bit id (`Rampagio::InputId`, `HotkeySystem.h`):
  `0x01..0xFE` are Windows virtual keys, which already include the mouse
  buttons (`VK_LBUTTON`, `VK_XBUTTON1`, ...); `0x10000 + n` is pad
  button `n`; `0x20000`/`0x20001` are the mouse wheel up/down. Old saved
  chains (plain VK arrays) load unchanged.
- **A binding** (`Hotkey`): the chain (a set: order doesn't matter), a
  gesture (`press` or `long`), and an action: `press` (`Command::Call`:
  flip/run), `hold` (a toggle is on while held, then back to what it was),
  `next`/`previous` (step a number or list; repeats while held), `set`
  (a toggle on/off, a number or a list index to `value`).
- **Uniqueness:** (chain as a set, gesture) is unique across all commands.
  The same chain can have a press and a long-press binding; then the press
  one fires on release if it was let go before the long-press time (a tap).
- **Specificity:** chains completed on the same frame where one contains
  the other: the longer wins (Shift+G doesn't also fire G).

## Per frame (`HotkeySystem::Update`)

The host polls only the inputs some binding uses (`WatchedInputs()`) and
passes those held. The system diffs against the last frame; with nothing
newly pressed or released and no chain active it returns at once. Newly
pressed inputs look up the bindings that contain them (an index rebuilt
when bindings change), keep the complete ones, drop the ones another
complete one contains, then fire or start a long-press/hold timer.

## Game side (`src/menus/Hotkeys.cpp`, `src/HotkeyInput.{h,cpp}`)

- Keyboard and mouse buttons: `GetAsyncKeyState`, only while the game
  window is in front (so typing elsewhere doesn't fire them, and no key
  can get stuck from a missed key-up).
- Wheel: `INPUT_CURSOR_SCROLL_UP/DOWN` (just pressed). Unverified that
  these report during gameplay; the game still acts on the wheel too.
- Pad: the frontend controls (group 2) for A B X Y LB RB LT RT LS RS,
  d-pad and Select, only while `IS_USING_KEYBOARD_AND_MOUSE(2)` is false,
  so any pad the game supports works. Which frontend input is which
  button, and that LS/RS are the stick clicks, is unverified.
- **Pad layer** (Settings > Hotkeys > Gamepad Layer, default RB): while
  it's held and some pad binding uses it, the player's game controls are
  disabled each frame except moving and looking, so RB+Y runs the hotkey
  and not Y's own action. The layer button's own first frame still
  reaches the game.
- Hotkeys don't run while the menu, the overlay or the pause menu is open,
  while the game window isn't in front, or online.

## Binding UI

- **F11** on a command row opens the **Hotkey window** (ImGui overlay,
  user's request 2026-10-09): the row's bindings in a table (keys, a
  Gesture drop-down, an Action drop-down, the value for Set, Clear), then
  Add Key / Clear All. With no binding yet it starts listening at once. A
  chain another command has shows "X already runs Y" with Replace / Keep.
  Left click isn't captured there (it clicks the window); the menu key
  closes it. English only, like the other overlay tools.
- **Y** on a pad (the overlay has no pad input) captures straight from the
  native menu, as before; a used chain asks to bind it again to replace.
- A capture waits until everything is let go, then collects what's held
  and finishes when all of it is released; Esc or 10 s cancels.
- Settings > Hotkeys: Enabled, Long Press Time, Gamepad Hotkeys, Gamepad
  Layer, the Hotkey Manager (each binding: action, gesture, value, remove)
  and Presets (create, rename, add steps: pick "Add Step", then F11/Y on
  any command row; each step's action and value; delete).

## Live checklist

0. F11 opens the window over the menu; Add Key, the drop-downs, Clear and
   Replace work; the menu key closes it and the native menu comes back.
   User tested the window in-game on 2026-10-09: works. Items 1-9 aren't
   reported individually yet.
1. Keyboard chain fires once per press; Shift+G doesn't fire G.
2. Mouse4/Mouse5 and Shift+wheel fire; nothing fires while alt-tabbed.
3. Pad: each button's label matches the button (fix the table in
   `HotkeyInput.cpp` if not); RB+Y with the layer on doesn't do Y's game
   action; the menu open combo still works.
4. Hold: toggle on while held, back off on release.
5. Next/Previous on a list (weather), repeating while held; Set on a
   number.
6. Tap and long press on the same key run their two bindings.
7. Conflict: binding a used chain warns; binding it again replaces it.
8. Presets: steps run in order; a deleted step's command is skipped.
9. `Rampagio.json` keeps all of it across an eject/re-inject.
