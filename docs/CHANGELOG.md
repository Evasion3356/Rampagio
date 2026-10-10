# Changelog

All notable user-facing changes to Rampagio are recorded here. Format
loosely follows [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Changed
- Settings, toggle states, themes and hotkeys are saved in one file,
  Rampagio.json, as you change them (it replaces Rampagio.ini,
  Rampagio_Settings.ini, Rampagio_Toggles.ini and Rampagio_Themes.ini).
  Number, choice and text values are saved too. Settings > Load / Save >
  Toggles you left ticked come back on at the next start.
  "Load Settings" re-reads the file and applies it.
- Ejecting Rampagio (ScriptHookRDR2's Ctrl+R reload) switches its
  features off first, where the game allows it.
- Hotkeys can be key combinations (F11 on a row, hold the keys, let go)
  and work for rows in menus that were never opened.
- The menu key is set in Settings > Core > Menu Key.
- Saved teleports, outfits, horses and spooner sets are JSON files
  (Rampagio_Teleports.json, ...). Old .ini files aren't read.

### Added
- F5 menu with Player (Invincible, Heal, Clean, Clear Bounty), Horse
  (Invincible, Heal), Teleport (To Waypoint) and World (Time +1 Hour).
- Switches itself off in Red Dead Online.
