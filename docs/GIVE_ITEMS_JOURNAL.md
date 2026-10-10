# Give Items journal (2026-10-09)

Where Recovery > Items > Give Items stands after the first live tests, what
was learned, and what's left. Read this before touching Give Items or
`tools/extract_catalog.py`. Background: `docs/COLLECTIBLES_AND_ITEMS_PLAN.md`
(step B), memories `item-catalog-extraction` and `item-catalog-tags`.

## State

Built clean (Debug), deployed. Of the changes below, the user has only seen
the earlier rounds in-game; the tag-based grouping (round 4) is untested.
Uncommitted at the time of writing, except the first round (commit
`473dfe2`) and `.gitignore` (`9ca8085`).

Menu path: Recovery > **Items** (renamed back from "Add Items"; its
descriptions come from Rampage's "Give Items" title through `kMenuAliases`
in `src/Descriptions.cpp`) > Give Items: Amount, Method, Search, then
"Categories":

| List | Source in the catalog |
|---|---|
| Provisions, Remedies, Ingredients, Kit, Valuables | satchel page tag `CI_TAG_CATEGORY_*` |
| Materials (Pelts and Hides, Carcasses, Fish, Feathers, Legendary Animal Parts, Horns, Claws and Teeth) | materials page tag `0x85B3CEDE` (name not found), split by `CI_TAG_ITEM_*`/`CI_TAG_SHOP_*` |
| Documents (Cigarette Cards, Letters, Notes, Newspapers, Books and Pamphlets, Recipes, Maps, Posters, Photographs and Drawings, Collector Notes, Other) | `CI_TAG_CATEGORY_DOCUMENT` + folder tag `CI_TAG_FOLDER_*` |
| Trinkets and Talismans | `CI_TAG_ITEM_TRINKET`, `CI_TAG_ITEM_TALISMAN` (talismans are clothing type) |
| Story Items (warning description) | folders `CI_TAG_FOLDER_KIT_KEEPSAKES`/`_KEYCHAIN`, plus provisions with no acquire cost and no sell price, minus orchids, used leftovers, materials, jewelry boxes |
| Fishing Bait and Lures | `CI_TAG_ITEM_FISHING_BAIT`/`_LURE`, `UPGRADE_FSH_*` |
| Satchel Upgrades | `KIT_POUCH_*`, `CUSTOM_SATCHEL` |

Overrides: the gold tooth goes in Valuables (the catalog files it as a
material; the user wants it with the gold, 2026-10-09). `KIT_WARDROBE`,
`KIT_CAMP*` are inventory containers and aren't listed. Items the game has
no name for (`GET_STRING_FROM_HASH_KEY`) and invalid keys are hidden at
runtime. Left out on purpose: clothing, weapons, horses (own menus), horse
equipment (needs a horse as parent; the game refuses it under the
character), weapon mods/decorations (per weapon, belong under weapon
customization), camp upgrades and story kit (one-of, already owned),
core_item, money, advert, other (containers, debts, Online currencies),
ammo (moved to Weapon > Ammunition, 2026-10-09: grouped by weapon, rows
named by the game, each fills its type to the maximum).

## How it works

- `tools/extract_catalog.py` reads `catalog_sp.ymt` (or the checked-in dump
  `tools/data/catalog_sp_items.json`, which now keeps each item's tags and
  buy/sell counts) and writes `src/data/ItemCatalog.inc` rows
  `{ key, type, category, name, group, subgroup }`. `classify()` holds every
  grouping rule; change groups there and regenerate, not in C++.
  `tools/data/ci_tags.txt` names 210 of 765 tag hashes (brute force over
  item-name tokens; see `item-catalog-tags` memory for the layout).
- `src/menus/Recovery.cpp`: `kItemLists`/`kMaterialGroups`/`kDocumentGroups`
  map group names to captions; `GiveItem`.
- `GameUtil::AddInventoryItem` (`src/GameUtil.cpp`): slot by item type as
  flow_controller `func_698` and Rampage do (upgrades in `SLOTID_UPGRADE`
  or under the wardrobe; clothing that doesn't fit the wardrobe slot under
  the wardrobe item; else satchel, wardrobe, currency, default slot), into
  `ActiveSpInventory()` (5 while the backup inventory is in use, else 1).
  Success is judged by the item count rising (the native can return false
  and still add, seen with bait). Every add is logged:
  `[Inventory] Add <hash> x<n> (type ..) to inventory .., slot ..: accepted|refused (count a -> b)`.
- After a native add, `GameUtil::ShowItemToast` calls flow_controller
  `func_610` (pc 0x158B4), the game's pickup toast. The Game Script method
  (`func_290`) shows it by itself.
- Refused with count > 0 reads "You already have as many X as the game
  allows" (one-of items, full stacks).

## Live findings so far (Rampagio.log)

- Consumables add fine (Aged Pirate Rum 98 -> 99). Earlier "nothing
  happened" reports were the missing toast, not a failed add.
- Old Upgrades list: camp upgrades already owned (count 1, refused); some
  went to `SLOTID_NONE`. Now dropped except bait.
- Old Kits list: pouches and story tools, mostly owned. Now Satchel
  Upgrades / Story Items.
- Ammo as an inventory item did nothing (counts capped at 8/40); now ped ammo.
  Then moved to Weapon > Ammunition (below).
- "Other > Gold Bar" was `CURRENCY_GOLD_BAR`, Online gold. Now hidden; the
  SP gold bars are `PROVISION_GOLDBAR_*` in Valuables.
- The broken pistol (wiki: Jeremiah Compson stranger) is
  `PROVISION_RCM_OLD_GUN`, granted by `rcm_slave_catcher2` with
  `PROVISION_RC_SLVCATCHER_WATCH` and `PROVISION_RCM_BLACK_BOOK`. The game
  files it under Valuables; no tag marks quest items, hence the
  no-buy/no-sell rule.

## Ammo (Weapon > Ammunition, 2026-10-09)

Moved out of Give Items; `kAmmo`/`kAmmoGroups`/`FillAmmoType` in
`src/menus/Weapons.cpp`. Seven weapon-group submenus, rows named by the game
(`GameUtil::ItemName` on the ammo hash, refreshed when the submenu opens).
Drop Ammo and Ped Editor > Weapons > Remove use `Ui::ChoiceAction`: stepping
only picks, selecting runs.

How the game adds ammo (1491.50; `flow_controller` func_740/563/560/559/556
and the `short_update` copies func_740/1989/1986/1985/1965/3188/3189): the
AMMO branch ends in `_ADD_AMMO_TO_PED_BY_TYPE(ped, ammo, n, ADD_REASON_DEFAULT)`
after `_IS_AMMO_VALID`, clamping n to `GET_MAX_AMMO(weapon) - GET_PED_AMMO_BY_TYPE`
(a bow clip is 5). Nothing else is needed for guns and arrows. For a thrown
or melee type the weapon comes first: func_556 maps ammo to weapon, func_559
`GIVE_WEAPON_TO_PED` with 0 ammo (melee ammo is forced to 0), then func_560
adds. We copy that (`AmmoType::weapon`).

Findings from the live log (`[Ammo]` lines):
- Arrow - Dynamite/Fire/Poison logged `max 40, 8 -> 8`: not a failure. The
  engine stops each special type at a per-type cap (8 here), below the bow's
  `GET_MAX_AMMO` (40), likely hit by the old Fill Ammo (All). A type that
  doesn't grow while held is reported "already full". The item's slot max
  (`_GET_ITEM_SLOT_MAX_COUNT(ammo, SLOTID_SATCHEL)`, func_3188) is logged as
  `slot max`; whether that slot is the right one for ammo is unverified.
- Hatchets logged `given`, `1 -> 1`: they are melee weapons, so giving the
  weapon is the whole add. The count was measured after the give; now before,
  and melee rows say "Gave you"/"You already have".
- Removed as not in SP (no script or catalog entry): Rifle Varmint/Elephant
  (those are weapons), Shotgun Express Explosive, Poison Bottle, Bolas.
- Not yet seen in the log: throwing knives, tomahawks, dynamite, molotovs.

## Next (live checklist)

1. Re-inject; walk every list and note junk or misfiled items (the user
   decides what's a story item; adjust `classify()`).
2. The pickup toast after an Inventory-method add (`func_610`): shows?
3. Story Items: give the broken pistol; check it lands under Valuables in
   the satchel and nothing breaks.
4. Talismans (clothing type, wardrobe path): do they add?
5. Weapon > Ammunition: each group's rows show the game's names; a type
   fills to the maximum, "already full" message when it is. Check the
   thrown rows (knives, tomahawks, dynamite, molotovs) and read their
   `[Ammo]` lines, including `slot max`.
6. Satchel Upgrades: if giving a pouch does nothing useful, drop the list.
7. Then commit (CRLF; translations are synced, `tools/lang_sync.py` clean).

Open ideas: weapon mods/decorations under the Weapon menu per weapon;
naming the materials page tag and the other ~550 unnamed tags.
