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
instead of restarting. `GenerateDefaultIni` writes
`bin\<Config>\Rampagio.ini` through `tools\IniGen`. Tests:
`tests\LogFallbackTests.vcxproj` (build Debug, run
`bin\Debug\LogFallbackTests.exe`).

Menu key: **F5** by default (`[General] MenuKey` in `Rampagio.ini`), the
same key Rampage uses, at the user's request. Don't load Rampage and
Rampagio together with default keys. Controls: NUMPAD 8/2 to move,
NUMPAD 5 to select, NUMPAD 0/Backspace/F5 to go back.

## Layout

- `src/script.cpp`: builds the menu and runs the main loop, including the
  online kill switch. To add a feature, add a row with `AddAction` (a
  one-shot that returns a status string; empty = no popup) or `AddToggle`
  (`onChange(bool)`, plus an optional `onTick()` that runs every frame
  while the toggle is on, menu open or not).
- `src/Features.{h,cpp}`: the feature implementations.
- `src/GameUtil.{h,cpp}`: shared helpers (`IsOnline`, `PlayerMount`,
  `TeleportToGround`).
- `src/scriptmenu.{h,cpp}`: the SDK NativeTrainer menu framework, same as
  the siblings', plus ChallengeCheat's item types and this repo's
  `MenuItemToggle`.
- `src/PatternScan.*` and `external/minhook`: carried over for features
  that need memory patterns or hooks. Nothing uses them yet.
- `tools/rampage_deob.py`: Rampage's native-hash deobfuscator (below).

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
no feature branches. There's no GitHub remote yet.

## Features

| Menu | Feature | Mechanism | Source | Live-tested |
|---|---|---|---|---|
| Player | Invincible | `SET_PLAYER_INVINCIBLE` + `SET_ENTITY_INVINCIBLE` on the ped | Rampage `sub_1800AE0B0` | No |
| Player | Heal | max health + `_SET_ATTRIBUTE_CORE_VALUE` 0/1/2 = 100 + `RESTORE_PLAYER_STAMINA` | ours | No |
| Player | Clean | `CLEAR_PED_WETNESS` / `_BLOOD_DAMAGE` / `_ENV_DIRT` | ours | No |
| Player | Clear Bounty | `LAW::SET_BOUNTY(0)` + `LAW::SET_WANTED_SCORE(0)` | Rampage `sub_180058920`'s last step | No |
| Horse | Invincible | `SET_ENTITY_INVINCIBLE` on the current mount, moved along when the mount changes | ours | No |
| Horse | Heal | max health + cores 0/1 | ours | No |
| Teleport | To Waypoint | `_GET_WAYPOINT_COORDS`, then ground probe with `REQUEST_COLLISION_AT_COORD` (moves the mount if riding) | ours | No |
| World | Time +1 Hour | `CLOCK::ADD_TO_CLOCK_TIME` | ours | No |

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
  Porting these needs our own script-function caller. That's the first
  piece of non-native infrastructure worth building.
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
4. Write our own version in `Features.cpp`, note the Rampage function in
   the table above, build, and have the user test it live.

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
- HorseMenu is YimMenu's repo (upstream `YimMenu/HorseMenu`), not ours:
  its own framework, license and online focus. Decide whether it's a
  dependency at all or only a reference.
- YEEAHSM uses `deps/minhook`; the others use `external/`. Shared
  submodules (ScriptHookSDK, spdlog, inipp, RDR-Classes, minhook) would
  nest; pick one copy and check version skew.
- Conflicting hooks/patches (several use MinHook or byte patches) when
  all run in one process.

### Goal C: improve on Rampage

Go past parity by reading the decompiled scripts (`..\Scripts\1491.50`)
and game files for things Rampage doesn't expose or does poorly. Each
idea gets a short write-up (script, function/local, mechanism) before
code. The sibling projects (minigame advisors, challenge completion,
fishing/dead-eye fixes, horse stat lock) are the first examples of this
kind of work.

## Next steps

- Pick the porting order from `tools/rampage_inventory.md`. Most options
  are plain natives; start with the Player, Horse, Weapons and World
  toggles.
- Script-function caller (for cash/honor and anything else Rampage runs
  through `sub_18001C900`). Only 13 options need it, so it can wait.
- Live-test the starter features.
