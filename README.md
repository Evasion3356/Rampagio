# Rampagio

A singleplayer trainer menu for Red Dead Redemption 2, built as a
[ScriptHookRDR2](http://www.dev-c.com/rdr2/scripthookrdr2/) `.asi` plugin.
Targets game build **1491.50**.

> **Singleplayer only.** Rampagio switches itself off, and stays off for the
> rest of the session, as soon as it detects Red Dead Online. Do not use any
> trainer online.

It is an open, from-scratch alternative to the existing singleplayer
trainers: nothing is copied from another trainer, and nothing depends on
another trainer's files at runtime.

Most of the menu is ported but not yet live-tested by everyone. If a row
does nothing or misbehaves, open an issue with `Rampagio.log` from your game
folder.

## Features

- **Player**: godmode and other toggles, healing, bounty and honor, wardrobe
  and outfits, model changer, animations, emotes, speech, effects, posse.
- **Horse**: toggles, stats and stat locking, mobile stable, horse loader.
- **Weapons**, **Vehicles**: ammo, weapon mods and visuals, vehicle and train
  options.
- **Teleport**: common locations, regions, shops, saved places and an
  interactive map window (pan, zoom, click to teleport).
- **Spawners**: peds, animals, horses, objects, legendary animals and fish.
- **World**: time, weather, law, ped and object managers.
- **Recovery**: Give Items from the game's full singleplayer item catalog,
  money, honor, unlocks, and collectible and legendary trackers that know
  what you have already found or killed.
- **Miscellaneous**: cutscene player, music, minigame helpers (Five Finger
  Fillet, poker, blackjack, dominoes), Undead Nightmare II.
- **Debug**: Script Monitor, Global Editor, in-game log window.
- **Hotkeys**: bind any command to keys, mouse buttons, the wheel or gamepad
  buttons, with combinations, gestures and presets.
- Themes, 13 languages (follows the game), a description under each row.

## Install

1. Install ScriptHookRDR2 (matching build 1491.50).
2. Put `Rampagio.asi` next to `RDR2.exe`.
3. Start the game in singleplayer and press **F5**.

Don't load it together with Rampage on default keys (both use F5), and
remove the standalone FishingFix, YEEAHSM, FFFCheat, ChallengeCheat,
PokerCheat, BlackjackCheat and DominoCheat `.asi` files: Rampagio includes
them.

## Controls

| Input | Keys |
|---|---|
| Keyboard | F5 open/hide, NUMPAD 8/2 move, 4/6 change value, 5 select, 0/Backspace back |
| Gamepad | RB + Left opens; D-pad navigates, A selects, B goes back |
| Mouse | Optional: Settings > Core > Mouse Controls |
| Hotkeys | F11 on a command row (Y on a gamepad) |

Settings, toggles, themes and hotkeys are saved in `Rampagio.json` next to
the `.asi`.

## Building

Requires Visual Studio 2026 (MSVC, v145 toolset) on Windows. Clone with
submodules:

```
git clone --recurse-submodules https://github.com/Evasion3356/Rampagio.git
MSBuild.exe Rampagio.vcxproj /p:Configuration=Release /p:Platform=x64 /nologo /v:minimal
```

Debug builds deploy to the game folder through `BuildTools\Find-RDR2GameDir.ps1`.
Unit tests are the `tests\*.vcxproj` projects. See `CLAUDE.md` and `docs/` for
architecture and design notes, and `docs/CHANGELOG.md` for version history.

## Credits

Built on Alexander Blade's ScriptHookRDR2, alloc8or's
[native database](https://alloc8or.re/rdr3/nativedb/), MinHook, Dear ImGui,
nlohmann/json and spdlog.

## License

[MIT](LICENSE). Bundled third-party libraries under `external/` keep their
own licenses.
