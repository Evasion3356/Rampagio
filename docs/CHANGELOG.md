# Changelog

All notable user-facing changes to Rampagio are recorded here. Format
loosely follows [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

## [1.0.0] - 2026-10-09

First release. A singleplayer trainer menu for RDR2 (game build 1491.50,
ScriptHookRDR2), opened with F5.

### Added
- Menus for Player, Horse, Weapons, Vehicles, Teleport, Spawners, World,
  Recovery and Miscellaneous, with a description under the selected row.
- Teleport map window: pan, zoom, click a place or right-click to teleport.
- Recovery > Give Items from the game's full singleplayer item catalog,
  and collectible and legendary lists that know what you have already
  found or killed.
- Hotkeys: bind any command to keys, mouse buttons, the wheel or gamepad
  buttons, as combinations, with gestures and presets that run several
  commands (F11 on a row; Y on a gamepad).
- Themes, menu position and size, sounds and optional mouse controls.
- Debug tools: Script Monitor, Global Editor and an in-game log window.
- Keyboard, mouse and gamepad control.
- The menu in the game's 13 languages, following the game's language or
  the one picked in Settings.
- Fishing and Dead Eye fixes, Keep Weapons on Dismount, Five Finger Fillet,
  the poker, blackjack and dominoes advisors and the challenge helpers,
  built in from their standalone mods.
- Settings, toggle states, themes and hotkeys are saved in one file,
  Rampagio.json, as you change them. Toggles you left ticked come back on
  at the next start; Settings > Load / Save > Load Settings re-reads the
  file. Saved teleports, outfits, horses and spooner sets are JSON files
  (Rampagio_Teleports.json, ...).
- Switches itself off, and stays off for the session, in Red Dead Online.
- Ejecting Rampagio (ScriptHookRDR2's Ctrl+R reload) switches its features
  off first, where the game allows it.
