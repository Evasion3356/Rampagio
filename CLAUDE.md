# Rampagio

A ScriptHookRDR2 ASI singleplayer trainer menu for RDR2 (target build
1491.50). The plan is to rebuild Rampage's feature set one feature at a
time: reverse how Rampage (the closed-source trainer) does something, then
write our own version. Started 2026-10-07 from ChallengeCheat's
infrastructure (menu framework, INI config, logging, pattern scanning,
MinHook, release workflow). PokerCheat uses the same submodule layout.

## Ground rules

- **Re-implement, don't copy.** Rampage is closed-source. Use its
  decompiled code to learn *which* natives, script functions, script
  locals and memory patterns a feature uses, then write our own code.
  Never paste its decompiled functions into this repo. Never commit
  its code or the reversing outputs (the deobfuscator's CSV/IDA outputs
  and the inventory are gitignored in `tools/`). Names and location
  data from its tables may be carried over (user, 2026-10-07), as
  generated `src/data/*.inc` built into Rampagio: nothing may depend on
  Rampage's files at runtime.
- **Singleplayer only.** `script.cpp` switches every toggle off and
  stops opening the menu once `net_main_online` is running (see
  `GameUtil::IsOnline`), the same check Rampage uses. Keep it that way,
  and keep online-only features out.
- **Verify live before calling a feature done.** The user wants each
  claim proven in-game, not just plausible from decompiled code. Track
  feature status in the table below and never round "untested" up to
  "works".

## Build / deploy

```
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Rampagio.vcxproj /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal
```

The PostBuildEvent deploys to the game folder through
`BuildTools\Find-RDR2GameDir.ps1`, the same as every sibling project. The
user keeps the game running and uses ScriptHookRDR2's eject/re-inject
instead of restarting. Tests (build Debug, run the exe from `bin\Debug`):
`tests\LogFallbackTests.vcxproj`, `tests\SettingsTests.vcxproj` (the
settings and command core in `src/core`) and `tests\XmlTests.vcxproj`.

Menu key: **F5** by default (Settings > Core > Menu Key, saved as
`settings.menukey` in `Rampagio.json`), the same key Rampage uses, at the
user's request. Don't load Rampage and
Rampagio together with default keys. Controls: NUMPAD 8/2 to move,
NUMPAD 5 to select, NUMPAD 0/Backspace/F5 to go back; on a gamepad RB +
Left opens it and the d-pad, A and B navigate. F11 on a command row binds
a hotkey: the keys held together, released to finish (Settings > Hotkey
Manager lists and removes them).

## Layout

- `src/script.cpp`: builds the root menu from the area builders, loads
  `Rampagio.json` and runs the main loop (looped commands, hotkeys, the
  throttled settings writer), including the online kill switch
  (`Commands::Suspend`, which undoes features without saving).
  `ScriptUnload` (from `DllMain` detach) does the same on eject, but only
  on the script's own OS thread while a game script thread is active
  (`GamePointers::Cached()->CurrentScriptThread`); otherwise it logs that
  features stay applied. Untested.
- `src/core/`: HorseMenu's settings and command system, adapted
  (`docs/CONFIG_REWRITE_PLAN.md` has the design and its decisions).
  `settings/Settings` keeps `Rampagio.json` as `IStateSerializer`
  components (`general`, `style`, `themes`, `commands`, `hotkeys`),
  writing at most once a second and on eject. `Tick` snapshots dirty
  components into the JSON every frame; `TryFlush` (detach) only writes
  that snapshot and never runs component code. `commands/` holds
  `Command` (stable dotted id, label, `std::function` hooks),
  `Commands` (registry, the `commands` component, `ApplyLoaded`,
  `Suspend`), `BoolCommand`/`LoopedCommand`, `ValueCommands.h`
  (`IntCommand`, `FloatCommand`, `ListCommand`, `StringCommand`,
  `ColorCommand`, optionally writing through the feature's own variable),
  `ActionCommand` and `HotkeySystem` (key chains by command id). Only
  `src/core/*.cpp`, `DataFile.cpp`, `Settings.cpp`, the four collection
  menus and `Spawner.cpp` (vehicle JSON Loader) include the full `<nlohmann/json.hpp>`; headers use
  `json_fwd.hpp`.
- `src/menus/<Area>.cpp`: one file per top-level menu (Player, Horse,
  Teleport, World, ...), declared in `src/menus/Menus.h`. Each holds both
  its rows and their implementations, with the Rampage submenu it ports
  named in the header comment. New files there are picked up by the
  `src\menus\*.cpp` wildcard in the vcxproj.
- `src/Menu.{h,cpp}`: the row builder API (`Ui::Submenu`, `ListMenu`,
  `Action`, `Do`, `Toggle`, `Looped`, `Number`, `Choice`, `Section`,
  `NameList`: a picker over a name list with Custom Input and Search rows,
  `Text`: a "Caption: value" row edited with the on-screen keyboard).
  `Toggle` takes `onChange(bool)` plus an optional `onTick()` that runs
  every frame while on, menu open or not; `Looped` is a tick-only toggle.
  Rows built once in a `BuildXxx` use the overloads that take a command
  id first (`Ui::Toggle(self, "player.godmode", "Godmode", ...)`): the
  row is a command, saved in `Rampagio.json` under the id and bindable.
  Ids are `<area>.<feature>`, lowercase, never renamed (the caption can
  be); loop-built rows get theirs from `Ui::Id(prefix, caption)`. Rows
  built inside a `ListMenu`/`DetachedListMenu` build, `NameList` entries
  and the Ped Editor's per-ped toggles use the id-less overloads (plain
  rows: not saved, not bindable; a Debug assert catches an id row in a
  list build). `SetDefault` sets a toggle's starting state, `Sync` shows
  a game state without running hooks, `SetTransient`/`Ui::Transient`
  keep runs and mirrors of game state out of the file, and
  `SetHotkeyable(false)` marks actions that open the keyboard. Toggles and
  values with a change hook only come back on start when
  `settings.restoretoggles` is on (the file keeps their saved values until
  they're changed); plain values (parameters), `settings.*` and commands
  marked `SetAlwaysRestore()` (option-like toggles, e.g. the sibling
  mods' rows) always do.
  Load / Save > Load Settings restores everything.
  `ListMenu` rebuilds its rows each time it opens. Don't nest a `ListMenu`
  or `Submenu` inside a `ListMenu`'s build (each rebuild would register a
  new menu); use one `DetachedListMenu` built once and open it with
  `Ui::Push` from a row (see the animation dictionaries in
  `PlayerActions.cpp`).
- `src/scriptmenu.{h,cpp}`: the SDK NativeTrainer menu framework, same as
  the siblings', plus ChallengeCheat's item types and this repo's
  additions: `MenuItemToggle` (plain toggles; command rows are in
  `Menu.cpp`), `MenuItemNumber<T>`, `MenuItemChoice`,
  `MenuItemSection`, NUMPAD 4/6 left/right input, `MenuBase::SetOnOpen`,
  gamepad input, and `MenuStyle` (`Style()`: colors, position, rows per
  page, sounds), which items read at draw time, plus `MenuKey()` and
  `WrapWidth()`. The look is Rampage's, re-implemented from its draw code with its
  measurements and default theme (merged 2026-10-08; the user saw it
  in-game and approved the look): `MenuBase::OnDraw` draws the header, the subheader (menu name,
  counter), base, gliding scroller and footer; rows draw only their text
  and sprites (checkbox, submenu arrow, "<- value ->"). `DrawMenuText`
  sizes text with `_BG_SET_TEXT_SCALE` and centers/right-aligns it
  through the game's text format struct (pattern in `scriptmenu.cpp`),
  as Rampage does. Sections are skipped by the selection, like
  Rampage's breaks.
- `src/menus/Settings.cpp`: Settings, the `general`/`style`/`themes`
  components and `settings.*` commands (`RegisterSettings`,
  `ApplyLoadedSettings`), plus the F11 binding flow, hotkeys and
  overlays every frame (`Menus::TickSettings`).
- `src/menus/PedEditor.cpp`: the Ped Editor and `Menus::Target`. The
  Player submenus in `PlayerSubmenus.cpp`, `PlayerActions.cpp` and
  `Wardrobe.cpp` act on `Target::Get()` (their `Me()`), which is the ped
  of the innermost menu on the open stack bound with `Target::Bind` (the
  Ped Editor's ped, the Horse menu's horse), else the player. The Ped
  Editor and Horse menu reach those menus with `Ui::Link` through
  `Menus::Shared()` instead of building copies. Ticks run with the menu
  closed, so they always act on the player.
- `src/Localization.{h,cpp}`: the menu in RDR2's 13 languages (user,
  2026-10-08), as PokerCheat does it: the game's language
  (`GET_CURRENT_LANGUAGE`, re-read every 3 s) unless Settings > Language
  (`settings.language`) picks one. Tables are compiled in, one
  `src/lang/<code>.inc` per language, `{ English, translation }` keyed by
  the exact English text; `es-MX.inc` holds only what differs from
  `es.inc`. The menu translates what it draws (captions, titles, choice
  values, sections, descriptions, status text, keyboard titles), so rows
  need nothing; text built at runtime uses `Tr`/`TrFormat` with a literal
  template where it's built (`TrFormat("Spawned {} cards", n)`), never
  `"Added " + name`. Captions computed in a static menu's build would be
  fixed in the start-up language, so compute them only in list builds or
  caption callbacks. Translations are written by hand (user: no XML
  language files, no copying), with the community Rampage translations in
  `..\RampageTranslations` as a reference only.
  `tools/lang_sync.py` extracts the English strings (literals that read as
  text, plus the descriptions, minus `src/lang/ignore.txt`), reports
  missing/stale entries and `{}`/`~code~` mismatches, and does the batch
  workflow (`--batch N out.json --ref ..\RampageTranslations`, then
  `--merge-batch out.json translations.json` with
  `{"<i>": {"fr": ..., ...}}`). Chinese, Japanese and Korean only render
  while the game itself runs in one of them (PokerCheat
  `docs/PITFALLS.md`).
- `src/GameUtil.{h,cpp}`: shared helpers (`IsOnline`, `PlayerMount`,
  `PlayerHorse`, `TeleportToGround`, entity pools, script globals,
  model/anim loading, `PromptText` on-screen keyboard, `Joaat`).
- `src/DataFile.{h,cpp}`: `LoadJson`/`SaveJson` for the user's saved
  collections, one file each (`Rampagio_Teleports.json`,
  `Rampagio_Outfits.json`, `Rampagio_Horses.json`,
  `Rampagio_Spooner.json`), stored where `Rampagio.json` is, plus the
  `Rampagio_Vehicles` and `Rampagio_Spooner` folders of Rampage-format
  vehicle files and spooner databases (`ListFiles`, `LoadText`/`SaveText`); `LoadLines` reads a
  plain list file from there (user-supplied lists such as
  `Rampagio_PedAnimList.txt`, `Rampagio_Speech*.txt` and
  `Rampagio_ClothingDb.xml`, the same formats as Rampage's
  `RampageFiles\Lists` files).
- `src/Xml.{h,cpp}`: a minimal XML DOM (elements and text) for Rampage's
  spooner database files; tested by `tests\XmlTests.vcxproj`.
- `src/GamePointers.{h,cpp}`: engine pointers by AOB scan (HorseMenu's
  signatures): script threads, script programs, current thread, script
  VM, script globals; `FindScriptThread`, `FindScriptProgram`,
  `ScriptLocal`. Resolved together on first use, retried every 5 s.
- `src/ScriptFunction.{h,cpp}`: calls a function inside a game script,
  adapted from HorseMenu's `ScriptFunction` (direct `ScriptVM` call on the
  script's live thread, else on a copy of the current thread). Functions
  are found by bytecode pattern: start at the ENTER (`0x22`), wildcard
  CALL (`0x39`) targets, check uniqueness against the 1491.50 `.ysc` in
  `..\SciptsCompile\script_rel` (the decompile's `// Position - 0x...`
  comment is the offset). Arguments are zero-extended 8-byte slots.
- `src/NativeHooks.{h,cpp}`: replaces natives for game scripts, adapted
  from HorseMenu's `NativeHooks`. Swaps the native's entry in each
  `scrProgram`'s native table (one script or `kAllScripts`), so our own
  ScriptHook calls still reach the real native; Rampage instead detours
  the handler globally. Programs that load later are caught by a MinHook
  detour on `InitNativeTables`; a destructor vtable swap unregisters
  unloaded ones. Hooks can be removed (for toggles); `Shutdown` (from
  `DllMain` detach) restores every table. Replacements call
  `NativeHooks::Original(hash)(ctx)` to run the real native. Untested.
- `src/BytePatch.{h,cpp}`: one-byte code patches by AOB pattern, applied
  only on a unique match of the expected original byte and restored on
  toggle-off and eject (Weapon Visuals' Disable Hitmarker / Hit Feedback).
  RDR2.exe on disk is protected, so signatures can't be checked offline.
- `src/PatternScan.*` (used by GamePointers) and `external/minhook`
  (used by NativeHooks).
- `src/overlay/`: the ImGui overlay (the user's HerbSpawner overlay,
  adapted; `external/imgui` v1.92.9b, `external/Vulkan-Headers`). Tools
  register with `Overlay::Register`; nothing is hooked until one opens.
  While one is open it takes keyboard and mouse and `script.cpp` skips the
  native menu; the menu key closes it. Tools draw on the render thread from
  a snapshot and send work back with `MainThread::Post` (`src/MainThread.h`,
  run at the top of the main loop): no natives on the render thread.
- `src/debug/`: Debug > Script Monitor (`docs/SCRIPT_MONITOR.md`; row in
  `src/menus/Debug.cpp`). English only, on purpose. `ScriptData` (script
  names by hash, `src/data/ScriptNames.inc` from
  `tools/extract_script_names.py`; natives, `src/data/NativeList.inc` from
  `tools/extract_native_list.py`), `ScriptHooks` (function and native
  hooks), `ScriptMonitor` (the window). Built on `src/ScriptBytecode.{h,cpp}`
  (functions numbered as the decompiler's func_N; checked by
  `tools/check_script_functions.py`) and `src/ScriptVM.{h,cpp}` (a script VM
  detour: private patched code copies, HorseMenu's ScriptPatches way, and
  per-thread VM time).
- `src/data/ItemNames.inc`: the Give Items list, generated by
  `tools/extract_items.py` from every item-prefixed `joaat("...")` name
  in the decompiled 1491.50 scripts (consumable_, provision_, document_,
  ...). Regenerate rather than edit. Invalid names are filtered at
  runtime with `_ITEMDATABASE_IS_KEY_VALID`. Rows show the game's own
  name: an item hash is also its text label, so
  `HUD::GET_STRING_FROM_HASH_KEY(item)` (`GameUtil::ItemName`, the same
  thing Rampage's `sub_1801E3080` does) gives it in the game's language.
- `src/data/{MapDiscoveries,UnlockNames,Compendium}.inc`: the Recovery >
  Unlocks lists (map discoveries, unlock hashes, herb/animal/horse/fish/
  bait/journal names), generated by `tools/extract_unlocks.py` from the
  decompiled scripts. Regenerate rather than edit.
- `src/data/{Scenarios,WalkStyles,DamagePacks,Timecycles,PostFx,Moods,Voices,Speeches,Animations,Effects}.inc`:
  the Player submenus' name lists, generated by
  `tools/extract_player_lists.py` from literal native arguments in the
  decompiled scripts (its docstring says how each is found). Regenerate
  rather than edit. `Emotes.inc` comes from alloc8or's `eEmote` enum via
  `tools/extract_emotes.py`. Animation flag names (`PlayerActions.cpp`)
  are the game's `eScriptedAnimFlags`/`eIkControlFlags` as listed in
  Halen84's RDR3-Native-Flags-And-Enums, which alloc8or's
  `TASK_PLAY_ANIM` comment links. femga's rdr3_discoveries
  (`..\Githubs\rdr3_discoveries`, has a full anim list) has no license, so
  nothing from it is bundled.
- `src/data/{PedModels,HorseModels,AnimalModels}.inc`: ped model names
  (humans grouped by prefix) from every `joaat("...")` in the decompiled
  scripts, by `tools/extract_models.py`; filtered at runtime with
  `IS_MODEL_IN_CDIMAGE`/`IS_MODEL_A_PED`. For the Model Changer and the
  spawners. `MetaPedExpressions.inc` (`_SET_CHAR_EXPRESSION` ids) comes
  from the list alloc8or's native comment links, by
  `tools/extract_expressions.py`.
- `src/data/Teleports.inc`: the Teleport menu's location submenus
  (Common Locations, regions, Shops and Services), Rampage's names and
  coordinates, generated by `tools/extract_rampage_teleports_ida.py`
  (headless IDA on a copy of the `.i64`). Regenerate rather than edit.
- `src/data/{RampageMapDiscoveries,LawDispatchRegions,LegendaryAnimals,OverlayTextures,BlipLabels,CutsceneCast,RampageCutscenes,MobileStable}.inc`: tables read
  from Rampage's `.rdata` by `tools/extract_rampage_tables.py` (pefile, no
  IDA; add further Rampage tables there): its 284 map discovery names
  (Unlocks.cpp merges them with `MapDiscoveries.inc` by hash) and the law
  region each dispatch response sets, and the legendary animals and fish
  (model plus outfit preset), and the overlay TX Id/palette tables (read
  from its static initializers' stack stores, see the tool's docstring)
  and the Teleport > Blips type labels, and the Cutscene Player's lists and
  Try to Populate cast, and the Mobile Stable's tack families and tints.
- `src/data/EffectPresets.inc`: Player > Effects' 25 named presets,
  Rampage's, read from its preset thunks by
  `tools/extract_rampage_effects.py` (pefile + capstone, no IDA).
- `src/data/Descriptions.inc`: the text Rampage shows under the selected
  row (568 rows), keyed by menu title and label, generated by
  `tools/extract_rampage_descriptions.py` from the inventory JSON (each
  menu API call takes a `vector<string>` of lines; `row_description` in
  `rampage_inventory.py` reads them). `src/Descriptions.cpp` looks a
  row's up by its menu's title and caption: its `kOurs` (our rows and
  rows that work differently) wins, and `kMenuAliases` maps our menu
  titles that differ from Rampage's (Weapon/Weapons, Ped Manager/Local
  Peds, ...). There's no caption-only match, since the same caption means
  different things in different menus. `Ui::Describe` sets a row's own.
  `MenuBase::OnDraw` draws it under the footer as Rampage does.
  `tools/description_coverage.py --missing/--unused` checks coverage from
  the source (rows with literal captions only).
- `src/data/Dreamcatchers.inc`: the 20 dreamcatcher coordinates, generated
  by `tools/extract_collectibles.py` from `discoverable_generic_location`.
  Cigarette cards, dino bones and rock carvings need no data file: the
  menu reads the game's collectable categories (`CIGARETTE_CARDS`,
  `dino_bones`, `rock_carvings`) and `_COLLECTABLE_GET_PLACEMENT_LOCATION`
  at runtime. Card models are `s_inv_cigcard_<set>_<NN>x` (set codes in
  `src/menus/Collectibles.cpp`). The user's earlier probe
  `..\CigCardTest` documents the card tracking layers (found, turned in,
  inventory).
- `tools/rampage_deob.py`, `tools/rampage_inventory*.py`, `tools/handlers/`: Rampage
  reversing tools (below). `tools/porting_status.py` regenerates
  `docs/PORTING.md`.

Natives: `external/ScriptHookSDK/inc/natives.h` is alloc8or's native DB
(https://alloc8or.re/rdr3/nativedb/) and nothing else, generated by the
fork's `tools/gen_natives.py` from
`https://raw.githubusercontent.com/alloc8or/rdr3-nativedb-data/master/natives.json`.
Use alloc8or's names; never add aliases. A native alloc8or doesn't have
goes in `src/ExtraNatives.h` (currently only `GRAPHICS::DRAW_LINE`), and
only after checking its hash isn't in alloc8or under another name. To pick
up alloc8or updates, regenerate in the fork, commit, bump the submodule,
and fix renames by hash. The deobfuscator's native DB names differ from
alloc8or's for some hashes (e.g. its `_SET_ATTRIBUTE_OVERPOWER_AMOUNT` is
`ENABLE_ATTRIBUTE_OVERPOWER`), so always match by hash.

Shared files: `src/LogFallback.h` and `BuildTools/Find-RDR2GameDir.ps1`
are identical in all sibling repos, so port any fix to every copy.
`.github/workflows/release.yml` matches the siblings except for its env
block and one change: the Nexus step is skipped while `NEXUS_FILE_ID` is
empty, because Rampagio has no Nexus page yet. Fill in `NEXUS_MOD_ID` and
`NEXUS_FILE_ID` once it does.

Conventions (shared with the siblings): CRLF on disk
(`.gitattributes` `* text=auto eol=crlf`; Git Bash `sed -i` silently
writes LF, so edit with Python writing CRLF, or restore with
`rm <file>; git checkout -- <file>`). Commit straight to `master`, with
no feature branches. Remote: `origin` is https://github.com/Evasion3356/Rampagio.git.

## Features

Porting status per Rampage submenu is in `docs/PORTING.md` (regenerate
with `tools/porting_status.py` after updating its `STATUS` table).
Nothing has been live-tested yet; the user asked to finish porting first.
Rows marked "ours" in code comments deliberately differ from Rampage.
Names and location data Rampage keeps in tables in its binary may be
carried over as generated data (teleports are; see Ground rules).

## Reversing Rampage

Files:
- Live copy: `E:\SteamLibrary\steamapps\common\Red Dead Redemption 2\Rampage.asi`
  (image base `0x180000000`). It's a 2026-01-04 build that refuses
  game builds older than "1311".
- IDA database: `D:\Backup\Stuff\RDR2 Shit\RampageDeob\Rampage.asi.i64`,
  already annotated by the deobfuscator. (On 2026-10-07 this overwrote the
  user's own copy of that file; the user's labels from it may be lost.)
  `RampageNUI.asi` and its `.i64` in the same folder are the user's.
- Headless IDA: `"C:\Program Files\IDA Professional 9.3\idat.exe" -A
  -S"script.py" -L"log.txt" <idb>`. Call `ida_auto.auto_wait()`, then
  `ida_hexrays.decompile(ea)`, and end with `ida_pro.qexit(0)`.

### Feature inventory

`tools/rampage_inventory_ida.py` (headless IDA, on the deob-annotated
database) plus `tools/rampage_inventory.py` (joins with
`rampage_natives.csv`) list every Rampage menu option:

```
idat.exe -A -S"tools\rampage_inventory_ida.py <scratch>\inv.json" -L"log.txt" <copy of Rampage.asi.i64>
python tools/rampage_inventory.py <scratch>/inv.json ../RampageDeob/rampage_natives.csv tools/rampage_inventory
```

Output is `tools/rampage_inventory.{csv,md}` (gitignored): area,
submenu, kind, label, description, natives reached from the handler (and,
for toggles, from the functions that read the toggle's global), and a
script-call flag. Run IDA on a copy of the `.i64`; the script renames the
builders `Submenus__SubXxx`; builders with no lambdas (Home among them)
are found as any other function calling the title API and named
`Sub_<address>`. Result for the 2026-01-04 build: 201 submenus, 1,753
rows (before the `Sub_` builders: 167 and 1,514), 602 with a
description.
Only 13 options call game script functions. Known gaps: rows whose work
happens in a shared tick or table (e.g. the Effects list) show no
natives, and about 15 labels are fragments.

How it works: builder names survive only in the RTTI names of their
lambdas (`_Func_impl_no_alloc<Submenus::SubXxx(void)::_lambda_N_>`).
The builder is the function that references those vftables, directly
or through a tiny constructor helper. Each call to a menu API function
(`0x1801ED2B0` action, `0x1801EE1E0` toggle with a bool global and a
lambda, `0x1801EDEE0` submenu by index, and so on; see `MENU_API`)
closes a row. Labels come from the Hex-Rays lines before the call,
because short ones are packed into integer immediates. The deob CSV's
`function` column is a `.pdata` chunk, so natives are attributed by
IDA chunk ranges instead.

### Native-hash obfuscation (solved)

There is one key: `g_NativeHashKey` (`0x180426BA8`) is set to
`0x27AC828DBF073D57` in the main-loop function `sub_1801E0870`. Each call
site holds an encrypted constant `C` and a per-site rotation `r` (0..31):

```
k    = rol64(key, r)
hash = ~rol64(rol64(k ^ C, 32), (k & 0x1F) + 1)
```

The result is cached per site (`sub_180220FE0` / `sub_18021B6E0`) and
passed to ScriptHookRDR2's `nativeInit`. Some sites call the encrypt
helper `sub_18001B780` (copy at `sub_1801DEED0`) at runtime instead, with
the plain hash in `rcx`.

`python tools/rampage_deob.py <Rampage.asi> <natives-rdr.json> [prefix]`
statically decrypts every site (needs `pefile` and `capstone`). The
native DB is
`..\Decompiler\GTA-V-Script-Decompiler\GTA V Script Decompiler\Resources\natives-rdr.json`.
It writes `<prefix>.csv` (site, nativeInit call, function, hash, name)
and `<prefix>_ida.py`, which names the key and helpers and comments both
IDA View and Hex-Rays pseudocode. Result for the 2026-01-04 build:
4,392 sites; 4,341 named; 33 decode to hashes not in the DB; 18
unresolved (constant reaches the site through a jump or a hoisted
register). The DB is missing some SDK names; for example
`NETWORK::_0x1B89BC43B6E69107` is `NETWORK_IS_SCRIPT_ACTIVE_BY_HASH` in
`external\ScriptHookSDK\inc\natives.h`.

### Known Rampage internals

- **Scheduler:** Rampage runs its menu and features as Win32 fibers. The
  list is `qword_180427660..668`; the main loop does `SwitchToFiber` into
  each one, then `scriptWait(0)`. Waits inside a feature set the fiber's
  wake time (`+72`) and switch back.
- **Online kill switch:** if `NETWORK_IS_SCRIPT_ACTIVE_BY_HASH(joaat("net_main_online"), -1, 0, 0)`,
  it deletes every fiber and clears the list for the rest of the session.
- **Calling script functions:** `sub_18001C900(out, scriptHash, funcIndex)`
  looks up a script's program by hash and finds a function by index.
  The index matches the decompiler's `func_N` numbering in
  `..\Scripts\1491.50`. Then `sub_18021E3B0` / `sub_18021E0B0` run it
  with arguments. Known uses:
  - `flow_controller` `func_688`: cash add plus "FEED_MONEY_EARN" toast;
    called with amount × 100.
  - `short_update` `func_583`: honor change (-320..320 prompt).
  How Rampage runs them: it takes the `audiotest` thread, saves its
  context, `Reset`s it to the target script, pushes the arguments, sets
  the PC and calls `Run`, then restores it. We use HorseMenu's direct
  `ScriptVM` call instead (`src/ScriptFunction.h`). Rampage's arguments:
  `func_688(dollars*100, 0, 0, 1, "", 0, 1, 752097756)` and
  `func_583(value, 0, 9, 0xBEF3D776, "", 0, 0, 0)`. Both are ported in
  `src/menus/Recovery.cpp`.
- **Game cheats** (cheat_ui / short_update, ours in `src/menus/Unlocks.cpp`):
  `Global_1425247[0..1]` activation bits, `.f_12[id]` state (0 locked,
  2 off, 3 on, 4 activate, 5 deactivate; ids are cheat_ui's 0..36),
  `.f_53` "cheats used" (blocks saving/missions), unlock bits in
  `Global_40.f_12000`. Bitsets keep 31 bits per word.
- **Script thread list:** `qword_180428BC8` (pointer array, count
  `uint16` at `+8`). Each thread has its script hash at `+0x6D8` and its
  stack pointer at `+0x6B8`. Script stack slots are 8 bytes, so the
  "dword index 552" Rampage writes is `iLocal_276`.
  - `region_law_guama_fussar`: Rampage sets `iLocal_276 =
    GET_GAME_TIMER() + 6000` each tick, which stops the Guarma
    out-of-bounds sniper from firing (script around line 853).
- **`fishing_core` stuck-state fix** (`sub_1800B1FA0`): if task 628 is
  active and `fishing_core` isn't running, `CLEAR_PED_TASKS_IMMEDIATELY`.
- **joaat:** `sub_18001C230` lowercases a `std::string` and hashes it.
  The Script Loader/Terminator/Monitor menus use it on names the user
  types; it's not used for hidden checks.
- **Menu labels are plain text** in `.rdata` (e.g. "Script Loader",
  "Enter Value -320 / 320"), so find a feature's handler from its label's
  xrefs.

### Porting a feature from Rampage

1. Find the label string in IDA and follow its xrefs to the handler (the
   menu builders pass handlers as lambdas or `std::function`).
2. Decompile it. The annotations give each `nativeInit` its name; read
   the `nativePush64` arguments that follow.
3. If it calls script functions or touches thread locals, check the
   target in `..\Scripts\1491.50\script_rel` to see what it actually does.
4. Write our own version in the area's `src/menus/<Area>.cpp`, update
   `STATUS` in `tools/porting_status.py`, regenerate `docs/PORTING.md`,
   build, commit.

In practice, port a whole submenu at a time from condensed handler dumps
(`tools/handlers/`, usage in `dump_handlers_ida.py`'s docstring): run
`rampage_inventory_ida.py` then `dump_handlers_ida.py` on a copy of the
`.i64` into the session scratchpad, then `shrink.py`, `compact.py`, and
read with `view.py <hc dir> SubXxx ...`. Constants Rampage keeps in data
(slider ranges, choice tables) need a small IDA read of the address.

## Goals

### Goal A: parity with Rampage (minimum bar)

Rampagio does at least everything Rampage does in singleplayer.
Rampage's `.rdata` holds about 2,600 readable label strings. Many are
data lists (animals, horse coats, orchids, trains), but the feature
surface is still several hundred options across Player, Horse, Weapons,
Vehicles/Trains, World/Weather/Time, Teleport, Spawners (ped, animal,
horse, object, boat, legendary), Ped/Object managers, entity guns,
Posse/Bodyguards, Wardrobe/Outfits, Inventory/Money/Honor/Bounty,
Collectibles/Map, Script Tools (Loader, Terminator, Monitor, Patcher,
Global Editor), Hotkeys, Themes and Settings.

1. Inventory: done for the static rows (see "Feature inventory"). Still
   to do: mark SP-only versus online-only and drop the online-only ones.
2. Infrastructure first: script-function caller (`sub_18001C900`
   equivalent), script local/global access, entity enumeration (pools),
   number/text input, list submenus, toggle persistence, hotkeys.
3. Port submenu by submenu, live-testing each row (Features table).

### Goal B: the sibling mods as submodules (built 2026-10-08, untested)

The siblings are dependencies, not copies (user, 2026-10-08): each repo
builds its feature core as a static library, `<Name>Lib.vcxproj`, that
its own standalone ASI links, and Rampagio adds the repo as a submodule
under `external/` and links the same library through a
`ProjectReference` in `Rampagio.vcxproj`. Updating one means moving its
submodule pin (`git -C external/<Name> checkout <commit or tag>`, then
commit the pin) and rebuilding. Version tags in the siblings publish a
GitHub release and a Nexus upload, so the library refactors were pushed
to their default branch untagged and Rampagio pins those commits; move
to a tag once one exists. The rows live in the matching Rampage area and
own the options (saved in `Rampagio.json`), so nothing reads the
siblings' INIs. Don't load a sibling's standalone `.asi` next to
Rampagio: both would apply (a second stow hook just fails its pattern,
but the fishing wait and HUDs would double).

| Submodule | Library | Rampagio rows |
|---|---|---|
| `FishingFix` | `FishingFixLib` | Player > Fixes: Fishing Cast Fix, Dead Eye Fix (`PlayerFixes.cpp`) |
| `YEEAHSM` | `YEEAHSMLib` | Weapon > Keep Weapons on Dismount (`Weapons.cpp`) |
| `FFFCheat` | `FFFCheatLib` | Misc > Minigames > Five Finger Fillet (`Minigames.cpp`) |
| `ChallengeCheat` | `ChallengeCheatLib` | Recovery > Challenges (`Challenges.cpp`) |
| `PokerCheat`, `BlackjackCheat`, `DominoCheat` (repo `DominoesCheat`) | `<Name>Lib` | Misc > Minigames > Poker/Blackjack/Dominoes (`Minigames.cpp`) |

How a library stays linkable next to Rampagio and the others (each
repo's CLAUDE.md has a "Library" section with its specifics):

- Everything that could collide is in the library's namespace
  (`FishingFix::GameMemory`, `PokerCheat::GamePointers`,
  `ChallengeCheat::Localization`, the advisors' header-only
  `ScriptLocal`/`ScriptGlobal`, ...). Rampagio and the siblings all had
  global `PatternScan`, `GamePointers`, `Localization` and `Log`.
- No log file: `<Name>::Log::SetSink` forwards lines; Rampagio prefixes
  them (`[PokerCheat] ...`) into `Rampagio.log`.
- No INI: options come from the host. FFFCheat takes an options
  provider; the advisors keep live values in `Config::Mutable()` and list
  them in `Config::Options()` (stable id, section, label, description,
  kind, range), from which `Minigames.cpp` builds the rows, so an option
  added upstream appears with a pin bump (ids must never change).
  `tools/lang_sync.py` reads those tables too. ChallengeCheat's language
  comes from `Localization::SetLanguage`.
- Host-provided: MinHook (YEEAHSM and ChallengeCheat include
  `MinHook.h` but don't build it; Rampagio's `DllMain` removes their
  hooks and then calls `MH_Uninitialize`, which `NativeHooks::Shutdown` no
  longer does), keyboard state (`keyboard.cpp`, which gained
  `IsKeyWithAlt` for the bet hotkeys) and ScriptHookRDR2.lib.
  `DllMain` calls `Menus::ShutdownMinigames` first (dominoes' search
  worker, `DominoCheat::OnProcessDetach`), and `script.cpp`'s loop runs
  `Menus::TickChallenges`.
- The libraries compile against their own nested `external/`
  (ScriptHookSDK, RDR-Classes, minhook), all on the same fork commit as
  Rampagio's; clone with `--recurse-submodules` (the release workflow
  already does).
- Rows that stand in for a sibling's INI setting use
  `SetAlwaysRestore()` (Command.h) so they come back on start whatever
  `settings.restoretoggles` says; the fixes, Keep Weapons on Dismount,
  the fillet patches and the advisors default to on, as the standalone
  mods are.

Static instead, at the user's request: CigCardTest (Recovery >
Collectibles > Cigarette Cards' Complete Set / Complete All Sets) and
GoldHorse's SP horse features (Horse > Horse Stats: Keep Cores Golden,
Lock Stats, `HorseLock.cpp`); GoldHorse isn't updated anymore and its
Online parts stay out. HorseStatLock is covered by that port. HorseMenu
stays a source to lift code from, not a dependency.

### Goal C: improve on Rampage

Go past parity by reading the decompiled scripts (`..\Scripts\1491.50`)
and game files for things Rampage doesn't expose or does poorly. Each
idea gets a short write-up (script, function/local, mechanism) before
code. The sibling projects (minigame advisors, challenge completion,
fishing/dead-eye fixes, horse stat lock) are the first examples of this
kind of work. GoldHorse (including moving it to the alloc8or native
header) belongs here too, later.

Active Goal C work: `docs/COLLECTIBLES_AND_ITEMS_PLAN.md` (researched
2026-10-08, not built): collectible and legendary blips/lists that know
what's already found or killed, and Give Items built from the game's full
SP item catalog (`catalog_sp.ymt`, 5,049 items) instead of script names.
`tools/catalog_dump.py` reads the catalog (prototype for step B1).

## Next steps

Resume point (2026-10-08). Every non-tabled Rampage submenu is ported:
150 done, 5 partial, 10 tabled, 2 dropped, 0 pending (`docs/PORTING.md`).
The table data Rampage keeps in its binary (effect presets, legendaries,
overlay textures, ...) is carried over as `src/data/*.inc`, Disable
Hitmarker uses `BytePatch`, and the menu is translated into 13
languages. The 5 Partial rows wait on tabled areas or the ImGui overlay,
except Force Player Type (needs live testing first) and Settings >
Plugins (no counterpart). Everything builds clean (Debug); nothing is
live-tested. Menu key is F5. Goal B's submodules are wired in (see
Goal B); their rows are untested in Rampagio, though the libraries'
code is what the standalone mods already ran live.

0. **Config rewrite: built, not live-tested** (merged into `master`,
   2026-10-07). Phases 1-6 of `docs/CONFIG_REWRITE_PLAN.md` are done; its
   "Implementation notes" list where the code differs from the plan.
   Phase 7's live test checklist is still open.
1. **Native header: done except GoldHorse.** Steps 1-3 of
   `docs/NATIVE_HEADER_PLAN.md` are done and pushed (fork `086e1ed`;
   Poker, Blackjack, Domino, ChallengeCheat, FFFCheat, FishingFix pushed;
   HorseStatLock committed, no remote). GoldHorse (own older header)
   is deferred to Goal C at the user's request; don't raise it as a
   next step.
2. **RDR2-Native-Menu-Base** (`../Githubs/RDR2-Native-Menu-Base`, MIT,
   Halen84, last updated 2023): a standalone ASI bundling its own older
   alloc8or header; draws with `DRAW_SPRITE`/`DRAW_RECT`/`BG_DISPLAY_TEXT`,
   no line drawing. Not suitable as a submodule (second native header,
   own DllMain). Plan: port its UI layer (sprite look, per-option
   descriptions, controller input) behind `src/Menu.h`, against alloc8or
   names, with attribution.
3. Porting is done apart from the Partial rows. Tabled, so skip them
   until the user brings them back: the Debug, Object Editor and Script Tools areas
   (tabled 2026-10-07; `TABLED_AREAS` in `tools/porting_status.py`). For
   Debug > Scripts the user wants to rework the tools rather than port
   them as-is; that rework started 2026-10-08 with Debug > Script Monitor
   (`docs/SCRIPT_MONITOR.md`: built, not live-tested; its "Not yet" list is
   what's left of Rampage's Script Tools). Leftovers listed in
   `docs/PORTING.md` rows marked Partial.
4. Script-function caller: built (`src/ScriptFunction.h`), untested.
   Its first live test should check the four GamePointers signatures
   match (`Rampagio.log`) before trusting any "via Game Script" row. The
   other Rampage users of `sub_18001C900` (e.g. `flow_controller`
   `func_290` from `sub_1800626A0`) get ported with their submenus.
5. Live-test once the user asks for it. For Goal B, first remove the
   standalone sibling `.asi` files from the game folder, then check
   `Rampagio.log` for each library's signature lines (`[FishingFix]`,
   `[YEEAHSM]`, `[ChallengeCheat]`, ...) before trusting its rows.
6. UI direction (decided 2026-10-07): the native menu stays the main UI
   and must be drivable by controller, keyboard and mouse. Mouse support
   is built, untested (Settings > Core > Mouse Controls, `MenuBase::OnMouse`
   in `scriptmenu.cpp`: hover, click, right-click back, wheel, Shift+wheel
   on a value row). No ImGui-only fork
   and no dual-renderer row model. ImGui is added only for complicated
   desk tools (script monitor, global/local editor, the reworked Debug >
   Scripts) as an optional overlay, with a native fallback where one
   makes sense; its actions go through `MainThread::Post`.
7. ImGui overlay: built 2026-10-08 (`src/overlay`, from the user's
   `..\GoldenHorseCores\HerbSpawner` overlay), untested. Its eject path
   removes the hooks, waits for the render/window threads to leave them,
   then releases the renderer objects and ImGui (HerbSpawner's known gaps).
   First live test: the Script Monitor checklist in `docs/SCRIPT_MONITOR.md`.
8. Goal C: found-aware collectibles and the full item catalog
   (`docs/COLLECTIBLES_AND_ITEMS_PLAN.md`). Step A (collectibles) first;
   its step 1 fixes a real bug (dino bones/rock carvings test TURNED_IN
   instead of FOUND).
