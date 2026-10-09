# Plan: found-aware collectibles and the full item catalog (Goal C)

Status: researched 2026-10-08; A1-A6 and B1-B3 built 2026-10-08 (compiled
clean since, not live-tested; see "Implementation notes"). B4 and the
legendary zone locations were done 2026-10-09. The live tests (C) are
open. Two independent workstreams;
A is smaller and fixes a real bug, so it goes first. Nothing here is
Rampage's code or data: it comes from the 1491.50 scripts and the game's
own files.

## Goal

1. Collectible and legendary blips and lists show what the player has
   already found or killed, the way the game itself decides it, and can
   hide them.
2. Give Items offers every item in the game's SP item catalog (5,049),
   grouped by the game's own item types and named in the game's language,
   instead of the 1,403 names the scripts happen to mention.

## Background

### What Rampage does

Its Dino Bones / Dreamcatchers / Rock Carvings "Show on Map" toggles
(`_BLIP_ADD_FOR_RADIUS` per location) don't check found state. Its
Legendary Animals submenu is a spawner only: no locations, no kill state.
Its Give Items list is a hand-picked table.

### What Rampagio does now

- `src/menus/Collectibles.cpp`: `BlipSet::Show` blips every location.
  List rows show "(Found)", but dino bones and rock carvings test
  `_COLLECTABLE_GET_NUM_TURNED_IN`, which is the wrong field (see below).
- `src/data/ItemNames.inc` (`tools/extract_items.py`): every
  item-prefixed `joaat("...")` in the scripts. 995 of its 1,403 names are
  in the catalog; the other 408 (skinned carcasses, `document_abandoned_*`,
  `document_card_*`, `satchel_nav_*`, `herb_*`, ...) aren't items and are
  dropped at runtime by `_ITEMDATABASE_IS_KEY_VALID`.

### How the game tracks found/killed (1491.50 scripts)

| Thing | State | Source |
|---|---|---|
| Dino bones | `_COLLECTABLE_GET_NUM_FOUND(item) > 0` (`dino_bones` func_16 gates the spawn); TURNED_IN is the later turn-in | `dino_bones.ysc` |
| Rock carvings | same | `rock_carvings.ysc` |
| Legendary fish | collectable category `legendary_fishing_spot` (`0xC7EEA672`, -940661134), items `legendary_fishing_spot_01..14`, same natives | `rare_fish.ysc`, `rcm_collect_rare_fish1.ysc` |
| Dreamcatchers | bit `2 << i` of `Global_40.f_8863.f_148` (already read) | `discoverable_generic_location.ysc` |
| Legendary animals | `Global_40.f_9319[i /*4*/]`, i = 0..15: `.f_0` zone revealed, `.f_1` killed, `.f_2` respawn time, `.f_3` carcass/pelt pending | `hunting_zone_*.ysc`, `short_update.ysc` (~line 11326) |
| Gator eggs | `gator_eggs` / `689918374` | `gator_eggs.ysc` |
| Carolina parakeets | `carolina_parakeets` | `av_discoverable_parakeet.ysc` |
| Herb pickups | category `1777389635`, subcategory = herb (`_COLLECTABLE_CATEGORY_GET_NUM_FOUND(cat, herb)`) | `herb_*.ysc` |
| Wilderness chests | category `-1129417850` | `wilderness_chest.ysc` |
| Treasure hunter | `treasure_hunter` | `treasure_hunter.ysc` |

Legendary zone index order (`hunting_zone_bear_legendary` func_24 /
`short_update` func_1141): bear, beaver, ram, buffalo, boar, buck,
Tatanka bison, bull gator, cougar, coyote, elk, fox, moose, panther,
pronghorn, wolf. Zone coordinates are not in the scripts: each zone
launches from a world scenario point.

### The item catalog

- File: `x64/data/itemdatabase/catalog_sp.rpf` > `catalog_sp.ymt` inside
  `update_4.rpf`. It's a superset of the `data_0` (4,990) and
  `mp004`/`mp005` dlcpack (4,992 / 4,995) copies.
- Extraction: the user's RDR2 RPF Tool headless mode, run from
  `..\external-tools\RDR2-RPF-Tool\RDR2 RPF Tool\bin\Release\` (that folder
  has `oo2core_5_win64.dll`; `app.publish` doesn't):
  `--extract <update_4.rpf> x64/data/itemdatabase/catalog_sp.rpf a.rpf`,
  then `--extract a.rpf catalog_sp.ymt catalog_sp.ymt`.
- Format: binary PSO (PSIN/PMAP/PSCH/..., big-endian; CodeWalker's
  `CodeWalker.Core/GameFiles/MetaTypes/Pso.cs`). Item struct `0xEDD9A017`,
  208 bytes: key @8, category @12 (`ci_category_*`, mostly unnamed),
  item type @16, flags @24, model @28, tags @40, acquire costs @56. Stored
  in the PMAP block whose wrapper struct points at `0xEDD9A017` (216-byte
  entries, struct at +8).
- Item types (count): clothing 1,960, provision 639, document 610,
  horse_equipment 477, weapon_decoration 339, consumable 220, weapon_mod
  203, money 156, weapon 142, ammo 77, horse 77, advert 57, upgrade 50,
  kit 27, core_item 10, other 5.
- Names: 3,539 resolve from script joaat names plus
  `..\external-tools\RAGE-StringsDatabase\RDR2\TextKeys\*.txt`. The rest
  need none at runtime: an item hash is its own text label
  (`GameUtil::ItemName`).
- Reader: `tools/extract_catalog.py` (grown from the `catalog_dump.py`
  prototype, which was verified 2026-10-08 to reproduce the 5,049 items
  and type counts). `--schema` prints the PSO structs, `--dump-json`
  writes `tools/data/catalog_sp_items.json`.
- Coverage of our current list against the catalog, per type:

  | Type | Catalog | In ItemNames.inc | Missing |
  |---|---|---|---|
  | provision | 639 | 394 | 245 (carcasses, pelts, skins, ...) |
  | document | 610 | 271 | 339 |
  | consumable | 220 | 141 | 79 (seasoned cooked meats, Pearson stews, horse care package, ...) |
  | horse_equipment | 477 | 28 | 449 |
  | weapon_decoration | 339 | 0 | 339 |
  | weapon_mod | 203 | 0 | 203 |
  | money | 156 | 0 | 156 |
  | ammo | 77 | 74 | 3 |
  | upgrade | 50 | 21 | 29 |
  | kit | 27 | 14 | 13 |
  | core_item | 10 | 0 | 10 |
  | clothing / weapon / horse | 1,960 / 142 / 77 | 52 / 0 / 0 | own menus |

### Alternatives and dead ends (checked 2026-10-08)

- Runtime enumeration instead of a generated table:
  `_ITEMDATABASE_CREATE_ITEM_COLLECTION(filter, &size, comparisonType)` +
  `_ITEMDATABASE_GET_COMPONENT_ITEM` + `_ITEMDATABASE_RELEASE_ITEM_COLLECTION`,
  as the shops do (345 scripts). Filter is nine 8-byte slots (alloc8or's
  `ItemCollectionFilter`: slotId, slotId2, tag, ciCategory, cost, unk5,
  flags, itemType, ciTag), unused ones `-1`. Scripts always filter by tag
  (slot 8) or category; whether an all `-1` filter returns everything is
  untested. A live fallback/cross-check if the table goes stale.
- `itemdatabase_debug.ysc` is stubbed in both 1491.50 and
  `..\Scripts\DEV-SCRIPTS`: no item list there.
- `..\external-tools\RedM-AI-Knowledge-Brain\datasets\items.json` is RedM
  roleplay framework items (VORP/RSG), not game items.
- `RAGE-StringsDatabase`'s `ArchiveItems/.../catalog_sp.txt` holds only the
  file name.
- `..\external-tools\rpf-extract` lists entries by hash only; use the RDR2
  RPF Tool.
- `..\Items.txt` (293 hashes, collector categories: fossils, coins, wild
  flowers, heirlooms) matches none of the SP catalog: it's an Online list.
- Of our 408 non-catalog names: `provision_skinned_*` 155,
  `document_abandoned_*` 78, `document_card_*` 57, `horse_equipment_*` 17,
  `satchel_nav_*` 12, `saddle_lean_*` 7, `ammo_arrow*` 6, `ammo_throwing*` 5,
  `herb_*` ~15, plus `consumable_antidote`, `consumable_potent_antidote`.

## Steps

### A. Found-aware collectibles

1. [x] Fix "found" for dino bones and rock carvings: `NUM_FOUND > 0 ||
   NUM_TURNED_IN > 0`. Show turned-in separately if useful.
2. [x] `BlipSet` keeps one blip per item with its found test; a "Hide
   Found" option (default on) skips or recolors found ones; refresh on a
   timer (every few seconds) so a pickup removes its blip.
3. [x] Legendary fish: new Collectibles entry on category `0xC7EEA672`
   (locations from `_COLLECTABLE_GET_PLACEMENT_LOCATION`, like dino bones).
   Check names/models against `LegendaryAnimals.inc`.
4. [x] Legendary animals: kill state from `Global_40.f_9319[i].f_1` in a
   list (and in Spawner > Legendary Animals rows). Locations: find the 16
   zone positions (world scenario points, or the map discovery each zone
   enables via `_MAP_DISCOVERY_SET_ENABLED`); until then, state only.
5. [x] New categories on the shared code: gator eggs, Carolina parakeets,
   wilderness chests, treasure hunter, herb pickups. Verify each category's
   `_COLLECTABLE_GET_PLACEMENT_LOCATION` returns real coordinates first.
6. [x] Translations for new strings (`tools/lang_sync.py`), descriptions,
   `docs/PORTING.md` notes ("ours").

### B. Full item catalog

1. [x] `tools/extract_catalog.py <catalog_sp.ymt> <names...> src/data/ItemCatalog.inc`:
   grow `tools/catalog_dump.py` into it, writing `{ hash, type, name-or-null }` per item.
   Takes the extracted `.ymt` (the extraction stays a manual step with the
   RPF tool; document it in the docstring). Regenerate rather than edit.
2. [x] Give Items reads `ItemCatalog.inc`: groups by item type, skips the
   types other menus own (clothing, weapon, horse, maybe horse_equipment
   and weapon_mod/decoration if their menus cover them), labels with
   `GameUtil::ItemName`, keeps `_ITEMDATABASE_IS_KEY_VALID` as the filter.
   Search row over the whole list.
3. [x] Retire `ItemNames.inc` / `extract_items.py` once nothing uses them
   (check `Unlocks.cpp`, `Recovery.cpp`, the collectibles code first).
4. [x] Optional: name the `ci_category_*` hashes for finer groups
   (2026-10-09; see "B4" under Implementation notes).

### C. Live tests (none done)

- [ ] Found state flips when picking up a dino bone, rock carving,
  dreamcatcher, legendary fish; blip disappears without reopening.
- [ ] A save with some legendaries killed shows the right ones.
- [ ] Adding items from each new type works and shows in the satchel;
  note which types fail (story-locked, intrinsic, MP-only flags).

## Implementation notes (2026-10-08)

Written in a Linux session without the game, MSVC or the decompiled
scripts. A later Windows session built it: it compiles clean with MSVC
(Debug, 2026-10-08), but none of it has been run.

- A1/A2/A3/A5: `src/menus/Collectibles.cpp`. One `Category` table drives
  Dino Bones, Rock Carvings and the new entries under "More Collectibles"
  (Legendary Fish, Gator Eggs, Carolina Parakeets, Wilderness Chests,
  Treasure, Herb Pickups). Found is `NUM_FOUND > 0 || NUM_TURNED_IN > 0`;
  Locations lists show "n / total found". `BlipSet` re-reads the spots
  every 3 s from the Show on Map toggle's tick and rebuilds the blips only
  when the set (or a found state) changes. Recovery > Collectibles > Hide
  Found (`collectibles.hidefound`, default on, always restored) skips found
  ones; with it off, found ones are blipped named "<name> (Found)" (no
  recolor: no blip modifier hash was checked). Herbs respawn, so they
  never count as found. Treasure and Legendary Fish rows use the
  collectable's own text label, falling back to "<name> <n>"; herbs use
  their subcategory's label. Whether any of these labels exist is untested,
  as is whether the new categories return placement locations (rows
  without one say so when selected).
- Gator eggs: `689918374` is `joaat("gator_egg_nest")`, not
  `joaat("gator_eggs")`; the code tries `gator_eggs` first and falls back
  to the other when the first has no items. Wilderness chests'
  `-1129417850` is `joaat("wilderness_chests")`. The legendary fish
  category is `joaat("legendary_fishing_spot")` (its items are
  `legendary_fishing_spot_NN`; rare_fish.ysc picks one by the scenario
  point's radius). Rows are named by the fish each spot gives:
  rcm_collect_rare_fish1 func_642 pairs spot NN with a
  `provision_fish_*_legendary`, whose hash is its text label. Those 14
  match `LegendaryAnimals.inc`'s fish; its 15th, Legendary Channel
  Catfish, is the story one, with no fishing spot. The herb category
  (`1777389635`) is still unnamed (no hit from `herb`/`plant`/`composite`
  combinations, the strings database or the scripts' literals).
- A4: `src/menus/Legendaries.h` (header-only, so no project change):
  zone i's killed flag is `Global(40 + 9319 + 1 + i * 4 + 1)` (array size
  slot, then 4 slots per zone), read as nonzero. Collectibles > Legendary
  Animals lists all 16 with "n / 16 killed"; the Ped Spawner's Animals
  list marks killed story legendaries (matched by LegendaryAnimals.inc
  label). Locations (2026-10-09): each hunting_zone_* script starts from a
  world scenario point in the hunting_<region>.ymt scenario files
  (update_4.rpf, x64/levels/rdr3/scenario/); the 11 files hold exactly 16
  points. `tools/extract_legendary_zones.py` reads them and matches each
  to a script by the coordinates the script uses near its zone, giving
  `src/data/LegendaryZones.inc` in the scripts' own zone order (each
  script's `*uParam0 = N;`, the Global_40.f_9319 index). The panther and
  beaver points equal long_update func_492's restriction volume centres,
  which checks the method. Legendary Animals is now a submenu: Show on Map
  (blips, killed ones hidden by Hide Found) and Hunting Zones (teleport
  rows).
- A6: the 30 new strings are translated in all 12 tables (hand-written);
  `tools/lang_sync.py` reports none missing. The category table keeps its
  `Joaat` calls on their own lines because lang_sync skips any line with
  `Joaat(`.
- B1: `tools/extract_catalog.py <ymt|json> src/data/ItemCatalog.inc
  [--names ...]` (positional output instead of the plan's order). The .inc
  holds all 5,048 items (the root `character` item is dropped; `CURRENCY`
  and `Component` become `other`), 3,553 with an internal name.
- B2: Give Items lists consumable, provision, document, ammo, kit,
  upgrade, core_item, horse_equipment, weapon_mod, weapon_decoration,
  money ("Money Items"), advert and other; clothing, weapon and horse are
  left out. Rows are sorted by the game's name, unnamed ones last under
  their internal name or hex hash. Search matches game and internal
  names. Horse equipment and weapon mods/decorations are included because
  no other menu gives them as inventory items.
- B3: `ItemNames.inc`, `extract_items.py` and `catalog_dump.py` are gone;
  only Give Items used the list.
- B4 (2026-10-09): 181 of the 291 `ci_category_*` hashes are named in
  `tools/data/ci_categories.txt`, found by joaat over
  `ci_category_` + one or two tokens from the item names in each category
  (plus item types and a generic word list); the strings database and
  script literals gave nothing. `extract_catalog.py` reads that list by
  default and writes each item's category hash into `ItemCatalog.inc`.
  Give Items splits Consumables, Provisions and Horse Equipment into All,
  one list per group and Other; the other types stay single lists (their
  items are in one category, or, for weapon mods/decorations, in dozens of
  one- or two-item ones). Unnamed big ones: `0x83E3C493` and `0xDBB9ABD6`
  (ammo boxes and special ammo), `0x1F5EEA44` (18 upgrades).
