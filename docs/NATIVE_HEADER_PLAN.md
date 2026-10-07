# Plan: one native header (alloc8or's)

Status: steps 1-2 done 2026-10-07 (fork commit `086e1ed`, not yet
pushed). The generator reproduces `7eed8e0` exactly from alloc8or's data
at `b3c5d5e`, apart from DominoCheat's hand-renamed
`_FIND_PLAYABLE_HAND_TILES`. Steps 3-4 pending. Written 2026-10-07.

## Goal

Every project builds against a single native header: alloc8or's RDR3
native DB (https://alloc8or.re/rdr3/nativedb/), generated, with nothing
merged in. A native that alloc8or doesn't have goes in that project's
`src/ExtraNatives.h`, and only then.

## Background

- `external/ScriptHookSDK` is the user's fork
  (`https://github.com/Evasion3356/ScriptHookRDR2-SDK`). History of
  `inc/natives.h`:
  - `31226bb` replaced the stock 2019 header with an alloc8or generation
    (2026-09-16).
  - `7eed8e0` made it self-include `types.h` / `nativeCaller.h`.
    **This is the last pure-alloc8or state.**
  - `ceca117` merged in 45 namespaces from the old stock SDK, and
    `dfe5275` merged 2,317 more natives. These are the source of the
    duplicate alias namespaces (`AI`, `CONTROLS`, `GAMEPLAY`, `CAM`,
    `UI`, ...) and of names alloc8or doesn't use.
- alloc8or's data: `https://raw.githubusercontent.com/alloc8or/rdr3-nativedb-data/master/natives.json`
  (86 namespaces, 7,132 natives on 2026-10-07). Each entry has `name`,
  `comment`, `params` (`type`, `name`), `return_type`, `build`.
- Every sibling pins its own submodule commit, so changing the fork
  breaks nothing until a project bumps it.

## Step 1: generator in the SDK fork

Work in `external/ScriptHookSDK` (the fork), on its default branch.

1. Add `tools/gen_natives.py`: reads alloc8or's `natives.json` (path or
   URL argument) and writes `inc/natives.h` in exactly the format of
   `7eed8e0`: header comment with generation time and URL, the
   self-includes, the `NATIVE_DECL` macro, then
   `namespace NS { NATIVE_DECL <ret> NAME(<params>) { [return] invoke<<ret|Void>>(0xHASH, <args>); } }`,
   with each native's comment as `//` lines above it.
2. Verify the generator against the baseline:
   `git show 7eed8e0:inc/natives.h > baseline.h`, generate from the
   JSON as it was then if obtainable (otherwise from the current JSON),
   and diff. Differences should only be natives alloc8or renamed or
   added since 2026-09-16; fix any format difference in the generator.
3. Generate from the current JSON, replacing `inc/natives.h` (this drops
   the `ceca117` / `dfe5275` merges). Commit the generator and the
   header together: "natives.h: pure alloc8or generation (tools/gen_natives.py)".
4. Pushing the fork is outward-facing: ask the user before `git push`.

## Step 2: move Rampagio onto it

1. Bump `external/ScriptHookSDK` to the new commit.
2. Rename (all verified by hash on 2026-10-07):

   | Old (merged header) | alloc8or | Where |
   |---|---|---|
   | `AUDIO::STOP_SOUND_FRONTEND` | `AUDIO::_STOP_SOUND_WITH_NAME` | `src/scriptmenu.h` |
   | `GAMEPLAY::CREATE_STRING` | `MISC::VAR_STRING` | `src/scriptmenu.cpp` |
   | `PED::SET_PED_STAMINA` | `PED::_RESTORE_PED_STAMINA` | `src/menus/Horse.cpp` |
   | `PED::_TRACK_PED_VISIBILITY` | `PED::REQUEST_PED_VISIBILITY_TRACKING` | `src/menus/Horse.cpp` |
   | `PLAYER::RESTORE_SPECIAL_ABILITY` | `PLAYER::_SPECIAL_ABILITY_START_RESTORE` | `src/menus/Player.cpp` |

   Check each new signature in the generated header; argument types may
   differ (e.g. `Any` vs `Ped`).
3. `GRAPHICS::DRAW_LINE` (0x6B7256074AE34680) isn't in alloc8or: it
   stays in `src/ExtraNatives.h`. Add a comment there that every entry
   must be absent from alloc8or, with the date checked.
4. Build Debug and Release. Any other error is a native whose name
   changed between the 2026-09-16 and current alloc8or data: fix by
   hash, never by adding an alias.
5. Re-run `tools/handlers/shrink.py` once on a sample to confirm it now
   shows alloc8or names (its "first name wins" rule becomes moot with no
   duplicates).
6. Update CLAUDE.md: the "Natives" paragraph under Layout (drop the
   alias-namespace warning, state the alloc8or-only rule and the
   ExtraNatives.h rule), and remove item 1 from Next steps. Commit.

## Step 3: siblings (when each is next touched)

Bump the submodule, then rename. Verified by hash on 2026-10-07:

| Project | Renames |
|---|---|
| PokerCheat, BlackjackCheat | `STOP_SOUND_FRONTEND`, `CREATE_STRING` (as above); `LANGUAGE::_GET_CURRENT_LANGUAGE_ID` -> `LOCALIZATION::GET_CURRENT_LANGUAGE`; `TEXTURE::HAS_STREAMED_TEXTURE_DICT_LOADED` / `REQUEST_STREAMED_TEXTURE_DICT` -> `TXD::` same names |
| DominoCheat | as Poker, plus `MINIGAME::_FIND_PLAYABLE_HAND_TILES`: alloc8or has it only as `MINIGAME::_0x3AE451860F03CA8A`; call that, or keep a named wrapper in its `ExtraNatives.h` |
| ChallengeCheat | `STOP_SOUND_FRONTEND`, `CREATE_STRING`, `LANGUAGE::_GET_CURRENT_LANGUAGE_ID` |
| FishingFix | `PLAYER::_ACTIVATE_DEAD_EYE` isn't in the merged header either; confirm it's already in its own `ExtraNatives.h` |
| HorseStatLock, FFFCheat | nothing |
| GoldHorse | uses its own older header with a custom naming style: needs a separate look before converting |
| YEEAHSM | no natives |

The shared `scriptmenu.h/.cpp` copies (Poker, Blackjack, Domino,
ChallengeCheat, Rampagio) carry the same two renames; port the fix to
every copy, as with `LogFallback.h`.

## Step 4: later, RDR2-Native-Menu-Base's UI

Not a submodule: it's a standalone ASI with its own (older) copy of the
alloc8or header, which would make a second native header. Instead, port
its UI layer (in-game sprite drawing, per-option descriptions,
controller input, fast scroll) into `src/scriptmenu.*` behind
`src/Menu.h`, rewritten against alloc8or names, crediting Halen84 (MIT).
It has no line drawing, so it doesn't replace `DRAW_LINE`.

## Done when

- The fork's `inc/natives.h` is generated by `tools/gen_natives.py`,
  with no merged namespaces.
- Rampagio builds against it with only `DRAW_LINE` in `ExtraNatives.h`.
- CLAUDE.md states the rule.
