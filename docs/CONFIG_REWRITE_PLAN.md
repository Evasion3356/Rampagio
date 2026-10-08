# Config rewrite: HorseMenu's settings and command system

Plan written 2026-10-07. Phases 1-6 implemented the same day on the
`config-rewrite` branch (see "Implementation notes" at the end); Phase 7's
live test is still open. This replaces Rampagio's INI-based
config (ChallengeCheat's `Config` plus hand-written save/load in
`src/menus/Settings.cpp`) with HorseMenu's design: one JSON settings file
made of `IStateSerializer` components, and a registry of named commands
that own feature state, are saved by name and are what hotkeys bind to.
HorseMenu is the user's own code, so lift it directly (no license
concern).

Work through the phases in order. Build after every step (the MSBuild line
in `CLAUDE.md`), and commit at the end of each phase, or each file in
Phase 4. Nothing here is live-tested until Phase 7.

## Where things stand

### Rampagio today

- `src/Config.{h,cpp}`: `Rampagio.ini` with `[General] MenuKey` and
  `WrapWidth`, loaded lazily, rewritten with resolved values. Used by
  `scriptmenu.h:510` (menu key), `scriptmenu.cpp:234` (wrap) and
  `Settings.cpp:335`. `tools/IniGen` plus the vcxproj's
  `GenerateDefaultIni` target write `bin\<Config>\Rampagio.ini`, and
  `release.yml` ships it (`INI: bin/Release/Rampagio.ini`).
- `src/DataFile.{h,cpp}`: `inipp` wrapper (`Load`, `Save`) plus
  `LoadLines` for user-supplied list files.
- `src/menus/Settings.cpp` writes three INI files by hand:
  - `Rampagio_Settings.ini`: `MenuStyle`, the toggle-saving flags,
    hotkeys.
  - `Rampagio_Toggles.ini`: which toggles are on, keyed by
    `Ui::Key` ("Menu Title > Caption").
  - `Rampagio_Themes.ini`: saved themes.
- State lives in the rows. `MenuItemToggle` holds its on/off state;
  `Ui::Number`, `Ui::Choice` and `Ui::Text` point at the menu file's own
  globals. Only toggles are saved. Number, choice and text values are
  never saved. Renaming a row or its menu silently drops its saved state
  and hotkey.
- Hotkeys (`Settings.cpp` `HotkeyTick`): F11 on any row binds one key,
  stored as `Ui::Key` → VK. Firing looks the row up with `Ui::Find`, so a
  row in a menu that hasn't been opened yet "isn't built yet".
- User data collections, each its own INI: `Rampagio_Teleports.ini`
  (`Teleport.cpp:105`), `Rampagio_Outfits.ini` (`Wardrobe.cpp:470`),
  `Rampagio_Horses.ini` (`Horse.cpp:367`), `Rampagio_Spooner.ini`
  (`ObjectSpawner.cpp:270`).
- User-supplied list files (Rampage's formats, read with `LoadLines` or
  their own parser): `Rampagio_AddonPeds.txt`, `Rampagio_ClothingDb.xml`,
  `Rampagio_IPLList.xml`, `Rampagio_ObjectList.txt`,
  `Rampagio_PedAnimList.txt`, `Rampagio_Shops.txt`, `Rampagio_Speech*.txt`.
  These are not config and stay as they are.

### HorseMenu (`..\HorseMenu\src\core`, last commit 8727e0b)

- `settings/Settings.{hpp,cpp}`: singleton holding one `nlohmann::json`.
  `Initialize(file)` reads it and calls `LoadState` on every registered
  component; `Tick()` re-serializes dirty components and rewrites the
  whole file. Components added after the first load go through a
  late-loader queue (`LoadComponent` is marked "TODO: this is broken").
- `settings/IStateSerializer.hpp`: a component with a name (its JSON
  key), `SaveStateImpl`/`LoadStateImpl` and a dirty flag.
- `commands/Command.hpp`: name, label, description, `joaat(name)` hash;
  self-registers in `Commands`; `Call()` → `OnCall()`; virtual
  `SaveState`/`LoadState(json&)`; `MarkDirty()`.
- `commands/Commands.hpp`: the registry, itself the `"commands"`
  component (`{ "<name>": <state>, ... }`). Also `EnableBoolCommands()`
  (runs `OnEnable` for commands loaded as on), `RunLoopedCommands()`,
  `Shutdown()`.
- Typed commands: `BoolCommand` (`OnEnable`/`OnDisable`), `LoopedCommand`
  (`OnTick` while on), `IntCommand`/`FloatCommand` (optional min/max,
  `OnChange`), `ListCommand` (index into `pair<int,const char*>` list),
  `StringCommand`, `ColorCommand` (`ImVec4`), `Vector3Command`. State
  changes are pushed to the `FiberPool`.
- `commands/HotkeySystem.{hpp,cpp}`: the `"hotkeys"` component; a key
  chain (several keys held together) per command hash; `Update()` calls
  the command. Marked "TODO: serialization isn't stable".
- Features are subclasses defined as static globals
  (`static AntiAfk _AntiAfk{"antiafk", "Anti Afk", "..."}`), and UI items
  refer to them by hash (`BoolCommandItem("godmode"_J)`).

## Decisions (confirm at the start of the implementing session)

These are recommendations. Ask the user about any marked **(ask)**
before Phase 2.

1. **Commands are created by the builders, not as one class per feature.**
   (ask) HorseMenu's subclass-per-feature style would mean rewriting
   about 790 rows as classes. Instead, add Rampagio command types that
   take `std::function` hooks (`onEnable`, `onDisable`, `onTick`,
   `onChange`, `onCall`) and are constructed inside `Menus::BuildXxx`
   alongside their row. The core (`Settings`, `IStateSerializer`,
   `Command`, `Commands`, `HotkeySystem`) is HorseMenu's; only how a
   command gets its behaviour differs. Subclassing remains available
   for features that want it. Registration still happens before
   settings load, because `BuildMenu()` runs before `Settings::Initialize`
   in `ScriptMain`.
2. **Commands can bind to existing storage.** `IntCommand`,
   `FloatCommand`, `ListCommand` and `StringCommand` take an optional
   `T* storage`; when set, the command reads and writes through it
   instead of its own member. This keeps the menu files' globals
   (`g_scale`, ...) and their readers unchanged, so Phase 4 is
   mechanical. The command still owns the saving and the dirty flag.
3. **Stable dotted ids.** `"<area>.<feature>"`, lowercase, no spaces:
   `player.godmode`, `horse.godmode`, `weapon.infiniteammo`,
   `settings.menukey`. The label stays the visible caption, so captions
   can be renamed freely. `Commands::AddCommand` logs and, in Debug,
   asserts on a duplicate id.
4. **Which rows become commands.** Every *static* row (built once in a
   `BuildXxx`, not inside a `ListMenu`/`DetachedListMenu` build) that
   holds state or can be hotkeyed:
   Toggle, Looped, Number, Choice, Text and Action/Do. Rows built inside
   list builds, the Ped Editor's per-ped toggles (`EditorToggle`,
   currently `SetPersist(false)`) and `NameList` entries stay plain rows:
   not saved, not bindable. F11 on one says "This row can't be bound".
5. **Restoring toggles on start.** (ask) HorseMenu always restores saved
   on-states. Rampagio currently has "Enable Toggle Saving" and "Auto
   Save Toggles" (default off). Recommendation: always save state, and
   replace both rows with one `settings.restoretoggles` BoolCommand
   (default off): when off, bool commands load as off but numbers,
   choices and text still load.
6. **Hotkeys keyed by command name, not hash** (the readable fix for
   HorseMenu's "serialization isn't stable" TODO), with HorseMenu's
   key chains. Keep Rampagio's F11 binding flow and Hotkey Manager menu.
7. **Online kill switch must not save.** `Ui::DisableAllToggles` today
   flips toggles off through `onChange`. With commands, that would mark
   them dirty and overwrite the user's saved states. Instead, add
   `Commands::Suspend()`: call `OnDisable` on every active bool command,
   stop running looped commands, keep `m_State`, don't mark dirty.
8. **Throttle writes.** HorseMenu rewrites the file on every dirty tick;
   holding NUMPAD 6 on a number would write every frame. Write at most
   once a second, plus a final flush from `DllMain` detach (from the
   script thread if possible; see Phase 3).
9. **No FiberPool.** Every command change already happens on the
   ScriptHook script thread, so call hooks directly instead of pushing
   them to a fiber pool. (The ImGui overlay will add a FiberPool later;
   commands changed from ImGui would go through it then.)
10. **No migration.** Nothing is live-tested and there are no user
    configs in the wild, so old INI files are ignored and can be deleted.
11. **One settings file:** `Rampagio.json`, resolved with
    `LogFallback::ResolveSettings(ModuleDirectory(), L"Rampagio.json",
    FallbackDirectory())` (don't change `LogFallback.h`; it's shared).
    Components: `general`, `style`, `themes`, `commands`, `hotkeys`.
    User collections (Phase 6) get their own JSON files, because they are
    content, not settings.

## Phase 1: dependency and core

1. Add `nlohmann/json` as a submodule: `external/json`, pinned to tag
   `v3.11.3`. Add `external\json\include` to the vcxproj include path
   (both configurations). Header-only, nothing to build.
2. Headers include only `<nlohmann/json_fwd.hpp>`; include the full
   `<nlohmann/json.hpp>` only in `src/core/**/*.cpp`, so the 22 menu files
   don't pay for it at compile time.
3. Create `src/core/settings/` and `src/core/commands/`, lifted from
   HorseMenu, in namespace `Rampagio` (not `YimMenu`). Adjust:
   - `Settings::Initialize` takes a `std::wstring` path (no FileMgr).
     Missing or corrupt file: start from `{}` and log through `Log::Write`
     (HorseMenu's `LOG(...)` macros don't exist here).
   - Fix the late-loader path: `AddComponent` after the first load calls
     `LoadComponentImpl` directly under the mutex, instead of the queue.
   - Write throttle and `Flush()` (decision 8).
   - `Command`: hash with `GameUtil::Joaat`; add `bool Hotkeyable()`;
     keep name, label, description.
   - `Commands`: add duplicate-id check (decision 3), `Suspend()` and
     `Resume()` (decision 7), `ForEach` for the Hotkey Manager and
     Search, and `GetCommand<T>(std::string_view name)`.
   - Typed commands: `BoolCommand`, `LoopedCommand`, `IntCommand`,
     `FloatCommand` (add `step`), `ListCommand` (options as
     `std::vector<std::string>`, to match `Ui::Choice`), `StringCommand`,
     `ColorCommand` (`ColorRgba` from `scriptmenu.h`, not `ImVec4`),
     `ActionCommand` (new: `std::function<std::string()>`, returns a
     status line, like `Ui::Action`). Skip `Vector3Command` until
     something needs it. Each gets the `std::function` hooks and the
     optional `T* storage` from decisions 1 and 2. Out-of-range loaded
     values are clamped to min/max.
   - `HotkeySystem`: keyed by command name (decision 6); keep chains;
     drop the `"chathelper"` special case.
4. New files under `src\core` must be added to the vcxproj (only
   `src\menus\*.cpp` is a wildcard).
5. Unit test: add `tests\SettingsTests.vcxproj` next to
   `LogFallbackTests` (same style). Cover: round-trip of every command
   type, clamping on load, unknown keys kept in the file, corrupt file
   recovery, duplicate id detection, `Suspend` not marking dirty.

## Phase 2: bind rows to commands

1. Add command-backed row items in `scriptmenu.h` (or make the existing
   ones accept a command): toggle, number (int/float), choice, text and
   action rows whose state and caption come from a `Command*`. Drawing
   and input stay as they are. The toggle's per-frame `OnFrame` tick
   goes away for commands; `Commands::RunLoopedCommands()` ticks them
   from the main loop instead, so they keep working with the menu closed
   and in menus never opened.
2. New `Ui::` overloads that take an id first, create the command,
   register it and add the row, e.g.:

   ```cpp
   Ui::Toggle(self, "player.godmode", "Godmode", onChange, onTick);
   Ui::Looped(self, "player.autoheal", "Auto Heal", AutoHealTick);
   Ui::Number(self, "player.scale", "Player Scale", &g_scale, 0.1f, 10.0f, 0.05f, ApplyScale);
   Ui::Choice(self, "world.weather", "Weather", kWeatherNames, &g_weather, SetWeather);
   Ui::Action(tp, "teleport.waypoint", "Teleport to Waypoint", ToWaypoint);
   ```

   They return the command (so code like `ModelChanger.cpp`'s
   `g_birdControls` keeps a handle and calls `SetState`). Keep the old
   id-less overloads for the dynamic rows of decision 4.
3. `Ui::Link` needs nothing: a linked menu shows the same command rows.
4. Settings > Search: also search `Commands` by label, so rows in menus
   that were never opened are found (removes "lists only count once
   opened" for command rows).

## Phase 3: Settings, Config and the main loop

1. `ScriptMain` order: `BuildMenu()` → register the `general`, `style`,
   `themes`, `hotkeys` components → `Settings::Initialize(path)` →
   `Commands::EnableBoolCommands()` (honouring decision 5). Each frame:
   `Commands::RunLoopedCommands()`, `HotkeySystem::Update()` (menu
   closed only, as today), `Settings::Tick()`. Remove
   `Menus::LoadSettings`.
2. Online kill switch: `Commands::Suspend()` instead of
   `Ui::DisableAllToggles()`; still pop every menu. If Rampagio should
   come back after leaving online, `Resume()`; today it stays off for the
   session, so keep that.
3. Replace `Config` with commands: `settings.menukey` (IntCommand holding
   a VK, shown through `KeyNames::Format`, validated as `Config` does
   now) and `settings.wrapwidth`. Point `scriptmenu.h:510`,
   `scriptmenu.cpp:234` and `Settings.cpp:335` at them. Delete
   `src/Config.{h,cpp}`.
4. `MenuStyle` becomes the `style` component (field by field, colors as
   `[r,g,b,a]`), marked dirty by the Theme rows. Saved themes become the
   `themes` component (`{ "<name>": { style fields } }`); keep the
   premade themes in code.
5. Rewrite the hotkey part of `Settings.cpp` on `HotkeySystem`: F11 on a
   command row starts binding (chain: keys held together, released to
   finish; Esc cancels), Hotkey Manager lists `Commands` with a binding
   and lets you clear one. Keep `IsNavigationKey` as the blacklist.
6. Load / Save submenu: "Save Settings" becomes `Settings::Flush()`,
   "Load Settings" re-reads the file, "Restore Defaults" resets every
   command and the style to defaults. Remove "Enable Toggle Saving" and
   "Auto Save Toggles"; add `settings.restoretoggles` (decision 5).
7. `DllMain` detach (eject): flush settings. `Settings::Flush()` only
   writes a file, so it's safe under the loader lock as long as it
   doesn't call natives; check that.
8. Remove the `GenerateDefaultIni` target and `tools/IniGen`; set the
   `INI:` line in `.github/workflows/release.yml` empty (check the
   workflow skips the INI step when it's empty, as it does for mods
   without one). Settings > About: credit nlohmann/json instead of inipp
   once inipp is gone (Phase 6).

## Phase 4: convert the menu files

One file at a time, build, commit (`Convert <Area> rows to commands`).
For each static stateful or hotkeyable row: add an id (decision 3),
switch to the id overload, and drop now-unused toggle plumbing (`g_godmode
= on` style mirrors can often read the command instead, but leave them if
in doubt: decision 2 keeps them valid). Rows inside list builds stay as
they are (decision 4).

Stateful rows per file (Toggle + Looped + Number + Choice + Text), largest
first; Action/Do rows (about 386 in total) come on top:

| File | Stateful rows |
|---|---|
| WorldSubmenus.cpp | 47 |
| Misc.cpp | 44 |
| WeaponSubmenus.cpp | 42 |
| Weapons.cpp | 32 |
| World.cpp | 29 |
| Settings.cpp | 29 (mostly done in Phase 3) |
| Player.cpp | 27 |
| Vehicle.cpp | 25 |
| Wardrobe.cpp | 24 |
| Horse.cpp | 18 |
| PedEditor.cpp | 17 (per-ped `EditorToggle` rows stay plain) |
| PlayerSubmenus.cpp | 15 |
| PlayerActions.cpp | 15 |
| Spawner.cpp | 11 |
| Recovery.cpp | 11 ("Use Backup Inventory" is in a list build: check) |
| ObjectSpawner.cpp | 10 |
| ModelChanger.cpp | 5 (`g_birdControls` etc. become command handles) |
| Collectibles.cpp | 3 (the card-run toggle handle) |
| Posse.cpp | 2 |
| Unlocks.cpp | 1 |
| Teleport.cpp | 1 |

Some of these counts include rows inside list builds; skip those.
Settings/actions that must not run from a hotkey with the menu closed
(anything that opens the on-screen keyboard, e.g. Custom Input) get
`Hotkeyable() == false`.

## Phase 5: remove the old system

- `Ui::AllToggles`, `Ui::Key`, `Ui::Find` (if Search no longer needs
  them), `MenuItemToggle::SetPersist/Persist`, `SetOn`, the
  `Rampagio_Settings/Toggles/Themes.ini` code in `Settings.cpp`.
- `Ui::DisableAllToggles`, if nothing but the kill switch used it.
- Grep for `.ini` and `inipp` to make sure only Phase 6 users remain.

## Phase 6: user collections to JSON

Teleports, outfits, horses and the spooner each move to their own JSON
file next to `Rampagio.json` (`Rampagio_Teleports.json`, ...), through a
small `DataFile::LoadJson`/`SaveJson` pair that replaces `DataFile::Ini`.
Keep `DataFile::LoadLines` for the user-supplied list files. Then remove
the `external/inipp` submodule and its vcxproj `ClInclude`.

This phase is separable from the rest; do it last.

## Phase 7: docs and live test

1. `CLAUDE.md`: rewrite the Layout bullets for `Config`, `DataFile`,
   `Settings.cpp` and the `Menu.h` row API (id overloads, which rows are
   commands); add `src/core/`; drop `GenerateDefaultIni`/IniGen from
   Build/deploy; add the new test project. `docs/CHANGELOG.md` entry.
2. Live test checklist (record results; don't round "untested" up):
   - `Rampagio.json` created on first start, next to the .asi (or in the
     fallback folder when the game folder isn't writable).
   - Toggle, number, choice, text, theme and hotkey changes survive
     eject/re-inject and a game restart; `restoretoggles` off leaves bool
     commands off.
   - A looped command bound to a hotkey works without ever opening its
     menu.
   - Going online switches everything off and the file still holds the
     pre-online states.
   - Holding NUMPAD 6 on a number doesn't write every frame (watch the
     file's modified time).
   - Corrupt `Rampagio.json` (truncate it) → defaults, logged, no crash.
   - Menu key change takes effect without restart.

## Implementation notes

Decisions 1 and 5 were confirmed as recommended. Where the code differs
from the plan above:

- **Restoring values.** Decision 5 said numbers, choices and text always
  load. A value with a change hook acts on the game (weather, minimap
  zoom), so it only comes back when `settings.restoretoggles` is on, like
  a toggle; otherwise the row would show a value the game isn't in.
  Plain values (parameters read by an action) always load. Toggles that
  stay off start at their build default (`SetDefault`), not forced off.
  Commands whose id starts with `settings.` always restore.
  `Commands::ApplyLoaded(restore)` replaces `EnableBoolCommands`.
- **Not-saved commands** (new): `Command::SetTransient` and
  `Ui::Transient(menu)` keep runs (Auto Collect All, the vehicle drive
  tasks, Pause Game) and mirrors of game state (menus re-read on open:
  hair, meta tags, horse stats, cores, config flags, world states, the
  backup-inventory flag) out of the file. They can still be bound.
- **Late components** load on the next `Settings::Tick`/`Flush`, not in
  `AddComponent`: that runs inside `IStateSerializer`'s constructor,
  where the derived `LoadStateImpl` doesn't exist yet (a pure virtual
  call). `Flush` saves every component; `Tick` only dirty ones. A corrupt
  file is kept as `<file>.bad`. Startup writes the file once after
  loading, so it always exists with every value.
- **DllMain detach** calls `Settings::TryFlush` (a try-lock), since a
  thread killed at process exit could have died holding the lock.
- **Style toggles.** "Gamepad Controls", "Menu Sounds", "Gamepad Open
  Key", the position rows and the color editor edit `MenuStyle` directly
  and mark the `style` component dirty; they aren't commands. The
  premade themes are action commands (`settings.theme.*`).
- **Menu key.** A row that captures the next key press (validated with
  `KeyNames`), backed by `settings.menukey`; `settings.wrapwidth` is a
  0-120 number row in Settings > Core. `general` only holds a format
  `version`.
- **Hotkeys** fire once per press (HorseMenu repeated every 100 ms while
  held), a chain held as part of a longer bound chain doesn't fire, and
  bindings for unknown ids are kept. Key names come from
  `GetKeyNameTextA`.
- **Search** (2.4) needed no change: every command row is in a static
  menu, all built at start, so searching built menus finds them.
- **Rows inside `NameList` extras** (Stop Playing, Clear all, Timecycle
  Strength, ...) run once at build time, so they became commands.
- **Phase 4 counts:** 578 rows converted by a script (literal caption,
  static context; it also turned `->SetState(x)` on new toggles into
  `->SetDefault(x)`), plus about 30 loop-built rows by hand with
  `Ui::Id`. `Unlocks.cpp`'s `StateToggle` rows stay plain on purpose.
- **Collections** (Phase 6) store numbers and arrays, not formatted
  strings; a damaged entry is skipped instead of failing the load.
- The core `Settings.cpp` shares a file name with `menus/Settings.cpp`,
  so the vcxproj gives it its own object path.
