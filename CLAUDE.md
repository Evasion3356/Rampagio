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
  anything derived from its binary (the deobfuscator's CSV/IDA outputs
  are gitignored in `tools/`).
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
`tests\LogFallbackTests.vcxproj` and `tests\SettingsTests.vcxproj` (the
settings and command core in `src/core`).

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
- `src/core/`: HorseMenu's settings and command system, adapted
  (`docs/CONFIG_REWRITE_PLAN.md` has the design and its decisions).
  `settings/Settings` keeps `Rampagio.json` as `IStateSerializer`
  components (`general`, `style`, `themes`, `commands`, `hotkeys`),
  writing at most once a second and on eject. `commands/` holds
  `Command` (stable dotted id, label, `std::function` hooks),
  `Commands` (registry, the `commands` component, `ApplyLoaded`,
  `Suspend`), `BoolCommand`/`LoopedCommand`, `ValueCommands.h`
  (`IntCommand`, `FloatCommand`, `ListCommand`, `StringCommand`,
  `ColorCommand`, optionally writing through the feature's own variable),
  `ActionCommand` and `HotkeySystem` (key chains by command id). Only
  `src/core/*.cpp`, `DataFile.cpp`, `Settings.cpp` and the four collection
  menus include the full `<nlohmann/json.hpp>`; headers use
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
  `settings.restoretoggles` is on; plain values (parameters) and
  `settings.*` always do.
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
  `WrapWidth()`.
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
- `src/GameUtil.{h,cpp}`: shared helpers (`IsOnline`, `PlayerMount`,
  `PlayerHorse`, `TeleportToGround`, entity pools, script globals,
  model/anim loading, `PromptText` on-screen keyboard, `Joaat`).
- `src/DataFile.{h,cpp}`: `LoadJson`/`SaveJson` for the user's saved
  collections, one file each (`Rampagio_Teleports.json`,
  `Rampagio_Outfits.json`, `Rampagio_Horses.json`,
  `Rampagio_Spooner.json`), stored where `Rampagio.json` is; `LoadLines` reads a
  plain list file from there (user-supplied lists such as
  `Rampagio_PedAnimList.txt`, `Rampagio_Speech*.txt` and
  `Rampagio_ClothingDb.xml`, the same formats as Rampage's
  `RampageFiles\Lists` files).
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
- `src/PatternScan.*` (used by GamePointers) and `external/minhook`
  (used by NativeHooks).
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
Data Rampage keeps in tables in its binary (teleport coordinates, ...)
is not copied: we source our own.

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
builders `Submenus__SubXxx`. Result for the 2026-01-04 build: 167
submenus, 1,514 rows, 1,302 static options, 225 data-driven list rows.
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

### Goal B: merge the sibling projects as submodules (needs recon)

Bring BlackjackCheat, ChallengeCheat, DominoCheat, FFFCheat, FishingFix,
GoldHorse, HorseMenu, HorseStatLock, PokerCheat and YEEAHSM in as git
submodules, so Rampagio is the one menu. Status: research only, no work
started. Open questions:

- Each sibling is a standalone ASI with its own `DllMain`, script loop,
  menu, INI and log. Each needs a library/feature entry point split out
  of its ASI shell, without breaking the standalone build.
- GoldHorse and HorseStatLock have no remote; a submodule needs one.
- GoldHorse runs online through Exodus, which conflicts with the
  singleplayer-only rule.
- HorseMenu (upstream `YimMenu/HorseMenu`) was written by the user, so
  its code can be adapted into Rampagio directly (no license concern).
  It has its own framework and an online focus, so decide whether it's
  a dependency at all or only a source to lift pieces from (e.g.
  `ScriptFunction`, `FiberPool`, `Pointers.cpp` patterns).
- YEEAHSM uses `deps/minhook`; the others use `external/`. Shared
  submodules (ScriptHookSDK, spdlog, json, RDR-Classes, minhook) would
  nest; pick one copy and check version skew.
- Conflicting hooks/patches (several use MinHook or byte patches) when
  all run in one process.

### Goal C: improve on Rampage

Go past parity by reading the decompiled scripts (`..\Scripts\1491.50`)
and game files for things Rampage doesn't expose or does poorly. Each
idea gets a short write-up (script, function/local, mechanism) before
code. The sibling projects (minigame advisors, challenge completion,
fishing/dead-eye fixes, horse stat lock) are the first examples of this
kind of work. GoldHorse (including moving it to the alloc8or native
header) belongs here too, later.

## Next steps

Resume point (2026-10-07, end of the porting loop). Every non-tabled
Rampage submenu is ported: 133 done, 22 partial, 10 tabled, 2 dropped,
0 pending (`docs/PORTING.md`). The last batch added Miscellaneous,
Settings (theme via `MenuStyle`, toggle save/load, F11 hotkeys, search,
overlays, gamepad input), the Ped Editor with `Menus::Target`, the rest
of Horse, Weapon Visuals/Aimbot/Bullets and the leftover Weapon rows, IPL
Sets and Teleport > Shops. The Partial rows are mostly deliberate: data
Rampage keeps in its own tables (effect presets, location lists,
outfit-preset legendaries, overlay textures) that we don't copy, plus a
few items needing live testing first (Force Player Type) or engine
patches (Disable Hitmarker). Everything builds clean (Debug); nothing is
live-tested. Menu key is F5.

0. **Config rewrite: built, not live-tested** (branch `config-rewrite`,
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
   them as-is. Leftovers listed in
   `docs/PORTING.md` rows marked Partial.
4. Script-function caller: built (`src/ScriptFunction.h`), untested.
   Its first live test should check the four GamePointers signatures
   match (`Rampagio.log`) before trusting any "via Game Script" row. The
   other Rampage users of `sub_18001C900` (e.g. `flow_controller`
   `func_290` from `sub_1800626A0`) get ported with their submenus.
5. Live-test once the user asks for it.
6. UI direction (decided 2026-10-07): the native menu stays the main UI
   and must be drivable by controller, keyboard and mouse. Mouse support
   (cursor via `SET_MOUSE_CURSOR_THIS_FRAME`, hover/click/wheel
   hit-testing in `scriptmenu.cpp`) is still to do. No ImGui-only fork
   and no dual-renderer row model. ImGui is added only for complicated
   desk tools (script monitor, global/local editor, the reworked Debug >
   Scripts) as an optional overlay, with a native fallback where one
   makes sense; its actions go through a FiberPool.
7. ImGui overlay (when the first such tool needs it). Reference: `..\GoldenHorseCores\HerbSpawner`
   (user's, not a git repo): `overlay\overlay*.cpp` hooks Vulkan first,
   DX12 as fallback, plus WndProc and SetCursorPos/ClipCursor, ImGui 1.92.9.
   It passes UI state through atomics only, so Rampagio would add a
   HorseMenu-style `FiberPool` to run UI actions on the script thread.
   Known gaps: eject/re-inject doesn't release the ImGui context or
   backends, and `MH_Uninitialize` runs from DllMain while the render
   thread can still be inside a hook.
