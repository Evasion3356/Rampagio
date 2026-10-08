# Porting status

Each Rampage submenu (`Submenus::SubXxx`, see `tools/rampage_inventory.md`) and where Rampagio has it.
"Options" counts Rampage's static interactive rows. Statuses: Done (every row has an equivalent), Partial, Pending, Tabled (set aside for now), Dropped (online only, or Rampage infrastructure with no Rampagio counterpart). Nothing is live-tested yet.

Submenus: 155 done, 4 partial, 0 pending, 6 tabled, 2 dropped.

## Debug

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubDebug | 21 | Tabled |  |

## Horse

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubHorseBlip | 3 | Done | Horse > Blip |
| SubHorseLoader | 2 | Done | Horse > Horse Loader: model, meta tags and gender in Rampagio_Horses.json (ours) |
| SubHorsePedMetaExpressions | 0 | Done | Horse > Meta Ped Expressions: the horse expressions of the shared menu |
| SubHorsePedMetaTags | 6 | Done | Horse > Meta Ped Tags: the shared menu with Index / Load Data from Index |
| SubHorseStats | 6 | Done | Horse > Horse Stats: ranks 0-10 per attribute |
| SubMobileStable | 3 | Done | Horse > Mobile Stable: 280 HORSE_EQUIPMENT_ items from the game scripts, grouped by kind |
| SubMobileStableComponent | 10 | Done | Horse > Mobile Stable > <kind>: the item tables of Rampage (data/MobileStable.inc), a tint pick per named family plus Disable; All Tack lists every tack item the scripts name (ours) |
| SubSelfHorse | 33 | Done | Horse |

## Misc

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubCutscenePlayer | 7 | Done | Miscellaneous > Cutscene Player: the story and Red Dead Online lists of Rampage plus the cutscenes the scripts name; Try to Populate casts peds from the model table of Rampage (data/CutsceneCast.inc) and Stop Current deletes them (ours) |
| SubEditVolume | 2 | Done | Miscellaneous > Volume Editor: edits volumes created there (ours); relationship groups from the game scripts |
| SubFriendlist | 1 | Dropped | Online only (Social Club friends) |
| SubGameMusic | 8 | Done | Miscellaneous > Game Music: 314 music events from the game scripts |
| SubMinigames | 1 | Done | Miscellaneous > Minigames > Undead Nightmare II: own wave logic (kill count spawns bosses) on the models and bosses Rampage uses |
| SubMiscellaneous | 18 | Done | Miscellaneous: Air Walk holds a fixed height (ours); About lives in Settings; the Dev rows Global Editor and Script Tools open the Debug ImGui tools |
| SubMobileTheater | 11 | Done | Miscellaneous > Mobile Theater: screen position and size rows (ours) |
| SubMusicPlayer | 8 | Done | Miscellaneous > Music Player: Windows MCI on files in a RampagioMusic folder (ours; Rampage bundles FMOD) |

## Object Editor

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubObjEditor | 21 | Tabled |  |
| SubObjSelectAttachment | 4 | Tabled |  |
| SubObjectFinder | 3 | Tabled |  |
| SubObjectFinderList | 3 | Tabled |  |

## Ped Editor

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubPedEditor | 19 | Done | Ped Editor (from Spawner > Ped Database and World > Ped Manager): Attach To Something offers player, horse and nearest entities (ours) |
| SubPedEditorAnimationDictsList | 4 | Done | Ped Editor > Animations > Dictionaries > (dictionary): the shared Player menu, opened from the Ped Editor (Menus::Target) |
| SubPedEditorAnimationsCustom | 6 | Done | Ped Editor > Animations > Custom Animations: the shared Player menu, opened from the Ped Editor (Menus::Target) |
| SubPedEditorAnimationsDicts | 1 | Done | Ped Editor > Animations > Dictionaries: the shared Player menu, opened from the Ped Editor (Menus::Target) |
| SubPedEditorCombat | 4 | Done | Ped Editor > General > Combat Style: 27 styles and 10 mods from the game scripts |
| SubPedEditorCombatAttributes | 0 | Done | Ped Editor > General > Combat Style > Combat Attributes: by number, 0-127 |
| SubPedEditorDamagePacks | 3 | Done | Ped Editor > Wardrobe > Apply Damage Packs: the shared Player menu, opened from the Ped Editor (Menus::Target) |
| SubPedEditorEffects | 25 | Done | Ped Editor > Effects: the shared Player menu, opened from the Ped Editor (Menus::Target) |
| SubPedEditorEmotes | 8 | Done | Ped Editor > Emotes: the shared Player menu, opened from the Ped Editor (Menus::Target) |
| SubPedEditorFacialAnimations | 4 | Done | Ped Editor > Animations > Facial Animations: the shared Player menu, opened from the Ped Editor (Menus::Target) |
| SubPedEditorGeneral | 13 | Done | Ped Editor > General |
| SubPedEditorMetaExpressions | 0 | Done | Ped Editor > Wardrobe > Meta Ped Expressions: the shared Player menu, opened from the Ped Editor (Menus::Target) |
| SubPedEditorMetaTags | 7 | Done | Ped Editor > Wardrobe > Meta Ped Tags: the shared Player menu, opened from the Ped Editor (Menus::Target) |
| SubPedEditorPlaySpeech | 1 | Done | Ped Editor > Play Speech: the shared Player menu, opened from the Ped Editor (Menus::Target) |
| SubPedEditorPlaySpeechCustom | 3 | Done | Ped Editor > Play Speech > Custom Speeches: the shared Player menu, opened from the Ped Editor (Menus::Target) |
| SubPedEditorPlaySpeechFlowgreet | 3 | Done | Ped Editor > Play Speech > Flow Greets: the shared Player menu, opened from the Ped Editor (Menus::Target) |
| SubPedEditorPlaySpeechRegular | 2 | Done | Ped Editor > Play Speech > Regular Speeches: the shared Player menu, opened from the Ped Editor (Menus::Target) |
| SubPedEditorPlaySpeechVignettes | 3 | Done | Ped Editor > Play Speech > Vignettes: the shared Player menu, opened from the Ped Editor (Menus::Target) |
| SubPedEditorScenarios | 7 | Done | Ped Editor > Scenarios: the shared Player menu, opened from the Ped Editor (Menus::Target) |
| SubPedEditorVoiceChanger | 1 | Done | Ped Editor > Play Speech > Voice Changer: the shared Player menu, opened from the Ped Editor (Menus::Target) |
| SubPedEditorWalkStyles | 2 | Done | Ped Editor > Wardrobe > Walk Styles: the shared Player menu, opened from the Ped Editor (Menus::Target) |
| SubPedEditorWardrobe | 10 | Done | Ped Editor > Wardrobe: own rows plus links to the shared wardrobe menus |
| SubPedEditorWardrobeComponent | 3 | Done | Ped Editor > Wardrobe > Components: the shared Player menu, opened from the Ped Editor (Menus::Target) |
| SubPedEditorWardrobeWearableState | 1 | Done | Ped Editor > Wardrobe > Components > Wearable State: the shared Player menu, opened from the Ped Editor (Menus::Target) |
| SubPedEditorWeapons | 6 | Done | Ped Editor > Weapons |
| SubPedEditorWeaponsGive | 0 | Done | Ped Editor > Weapons > Give Weapon |
| SubPedPositioning | 0 | Done | Ped Editor > Positioning: position and heading, or attachment offsets while attached |
| SubPedSelectAttachment | 3 | Done | Ped Editor > Attach To Something (ours: player, horse, nearest ped/vehicle/object) |

## Player

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubAbilities | 8 | Done | Player > Abilities: recharge toggles restore 1.0 when off (ours) |
| SubAnimPostFx | 3 | Done | Player > Vision > Screen Effects: list from the game scripts |
| SubAnimationDictsList | 5 | Done | Player > Animations > Dictionaries > (dictionary): picking one also fills in Custom Animations (ours) |
| SubDamagePacks | 3 | Done | Player > Wardrobe > Apply Damage Packs: list from the game scripts |
| SubEffects | 29 | Done | Player > Effects: the 25 Rampage presets (data/EffectPresets.inc), Scale, Loop, Custom, plus 6 effects from the game scripts |
| SubEmotes | 11 | Done | Player > Emotes: every emote type listed under its own heading instead of an Emote Type choice; adds gun twirls (alloc8or eEmote list) |
| SubModelChangerAnimal | 2 | Done | Player > Wardrobe > Model Changer > Animals: one list from the game scripts |
| SubModelChangerHorses | 0 | Done | Player > Wardrobe > Model Changer > Horses: one list from the game scripts |
| SubModelChangerHorsesList | 1 | Done | Player > Wardrobe > Model Changer > Horses |
| SubModelChangerPeds | 0 | Done | Player > Wardrobe > Model Changer > Humans: models from the game scripts, grouped by prefix |
| SubModelChangerPedsList | 1 | Done | Player > Wardrobe > Model Changer > Humans > <group> |
| SubMoods | 2 | Done | Player > Moods: mood_ anims from the game scripts |
| SubPlaySpeech | 6 | Done | Player > Play Speech |
| SubPlaySpeechCustom | 3 | Done | Player > Play Speech > Custom Speeches: Rampagio_SpeechList.txt, re-read on open |
| SubPlaySpeechFlowgreet | 3 | Done | Player > Play Speech > Flow Greets: Rampagio_SpeechFlowGreets.txt, re-read on open |
| SubPlaySpeechRegular | 2 | Done | Player > Play Speech > Regular Speeches: 208 lines from the game scripts (Rampage's table has 502); one Display All for every list |
| SubPlaySpeechVignettes | 3 | Done | Player > Play Speech > Vignettes: Rampagio_SpeechVignettes.txt, re-read on open |
| SubPlayerConfigFlags | 2 | Done | Player > Config Flags: flag by number (no names), shows current state (ours) |
| SubPlayerPosse | 11 | Done | Player > Posse: commands are menu rows, bindable to keys with F11 (Settings > Hotkey Manager); Add Aimed/Nearest Ped, Teleport, Dismiss and Delete rows (ours) |
| SubPlayerProofs | 9 | Done | Player > Player Proofs: re-applied every frame (ours) |
| SubSelf | 38 | Done | Player |
| SubSelfAnimationsCustom | 7 | Done | Player > Animations > Custom Animations: the Custom Flag submenu is a typed Custom Flags row (decimal or 0x hex) |
| SubSelfAnimationsDicts | 1 | Done | Player > Animations > Dictionaries: script list plus Rampagio_PedAnimList.txt, re-read on open instead of Reload List |
| SubSelfCustomizations | 4 | Done | Player > Wardrobe > Overlay Textures: TX Id and Palette Id pick from the tables of Rampage (data/OverlayTextures.inc); the hashes can also be typed in |
| SubSelfFacialAnimations | 4 | Done | Player > Animations > Facial Animations |
| SubSelfFacialHair | 4 | Done | Player > Wardrobe > Hair and Weight |
| SubSelfModelChanger | 8 | Partial | Player > Wardrobe > Model Changer: Force Player Type not ported |
| SubSelfOutfitSaver | 3 | Done | Player > Wardrobe > Outfits: saved in Rampagio_Outfits.json (ours; Rampage writes an XML per outfit) |
| SubSelfPedMetaExpressions | 0 | Done | Player > Wardrobe > Meta Ped Expressions: names from the MetaPedExpression list alloc8or links |
| SubSelfPedMetaTags | 7 | Done | Player > Wardrobe > Meta Ped Tags |
| SubSelfScenarios | 7 | Done | Player > Scenarios: list from the game scripts plus Custom Input/Search; no Scenarios.txt reload |
| SubSelfWalkStyles | 2 | Done | Player > Wardrobe > Walk Styles: list from the game scripts |
| SubSelfWardrobe | 17 | Done | Player > Wardrobe: clothing list from a user-supplied Rampagio_ClothingDb.xml (same format as Rampage); Keep Facial Hair is a toggle instead of holding Shift (ours) |
| SubSelfWardrobeComponent | 3 | Done | Player > Wardrobe > Components > <category>: picking an item applies it, then opens its wearable states (ours) |
| SubSelfWardrobeWearableState | 3 | Done | Player > Wardrobe > Components > Wearable State |
| SubTimecycleMod | 3 | Done | Player > Vision > Timecycle Modifiers: list from the game scripts |
| SubVoiceChanger | 3 | Done | Player > Play Speech > Voice Changer: Set Voice lists the script voices plus Custom Input (ours) |

## Recovery

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubCollectiblesCigaretteCards | 13 | Done | Recovery > Collectibles > Cigarette Cards: sets and card models from the game (CARD_SET_* collectables, s_inv_cigcard_* models); Auto Collect All runs as a stoppable per-frame job (ours) |
| SubCollectiblesCigaretteCardsSet | 1 | Done | Recovery > Collectibles > Cigarette Cards > <set>: cards by in-game name with owned count; Give Missing Cards (ours) |
| SubCollectiblesDinoBones | 2 | Done | Recovery > Collectibles > Dino Bones: read from the dino_bones collectable category with _COLLECTABLE_GET_PLACEMENT_LOCATION; locations in a Locations list. Ours: found is NUM_FOUND (Rampage checks nothing), Show on Map re-reads it every 3 s and Hide Found leaves found ones off the map; the same code adds legendary fish, gator eggs, Carolina parakeets, wilderness chests, treasure and herbs, and Legendary Animals lists the kill state (untested) |
| SubCollectiblesDreamcatchers | 2 | Done | Recovery > Collectibles > Dreamcatchers: coordinates from discoverable_generic_location (tools/extract_collectibles.py), found state from Global_40.f_8863.f_148; Show on Map refreshes and honours Hide Found (ours) |
| SubCollectiblesRockCarvings | 2 | Done | Recovery > Collectibles > Rock Carvings: read from the rock_carvings collectable category; locations in a Locations list; found is NUM_FOUND, Show on Map refreshes and honours Hide Found (ours) |
| SubMapDiscoverables | 2 | Done | Recovery > Unlocks > Map Discoverables: the 284 of Rampage discoveries (data/RampageMapDiscoveries.inc) plus any more the scripts name |
| SubRecoveryAddItems | 13 | Done | Recovery > Add Items: Unlimited Items also blocks by-GUID removals (ours) |
| SubRecoveryBounty | 9 | Done | Recovery > Bounty: state rows also show each state bounty (ours) |
| SubRecoveryCores | 9 | Done | Recovery > Cores: temporary rank applies on every step (ours) |
| SubRecoveryGiveItemsList | 1 | Done | Recovery > Add Items > Give Items: the game's whole SP item catalog (catalog_sp.ymt, tools/extract_catalog.py), one list per item type plus Search (ours); method is a visible choice instead of Shift (ours) |
| SubRecoveryHonor | 5 | Done | Recovery > Honor: all actions; current honor reads honor_current; the honor HUD meter shows while the menu is open |
| SubRecoveryMoney | 5 | Done | Recovery > Money: drop is a timed toggle (ours) instead of every tick |
| SubRecoveryUnlocks | 8 | Done | Recovery > Unlocks: own lists from the game scripts (tools/extract_unlocks.py); Unlock Checks lists every unlock the scripts name; Add Entries writes every journal entry the game allows; Discover Fish skips legendary outfit presets (ours) |
| SubStatEditor | 6 | Done | Miscellaneous > Stat Editor |
| SubUnlockCheats | 1 | Done | Recovery > Unlocks > Cheat Codes: activates each cheat through the game cheat state instead of showing its phrase (ours) |

## Script Tools

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubGlobalEditor | 7 | Done | Debug > Global Editor (ImGui, ours): addresses as the decompiled scripts write them, a saved watch list, INT/FLOAT/BOOL/HASH/VECTOR3/TEXT_LABEL/char* |
| SubScriptEditor | 3 | Done | Debug > Script Monitor > Thread: Restart, Kill / Kill all, Force Cleanup defaulting to the flags the script checks (ours; Rampage passes 0x800, which 9 scripts check), plus Pause/Resume (ours) |
| SubScriptPatcher | 7 | Done | Debug > Script Monitor > Functions: hooks named func_N or by Position, argument and return counts read from the bytecode (ours) |
| SubScriptTools | 3 | Done | Debug > Script Monitor (ImGui, ours): its Script Monitor, Script Patcher, Script Loader (Start Script, stack size preselected from the table of Rampage), Script Terminator (Kill / Kill all) and Force Cleanup All Scripts (flags the scripts check, not only 0x800) |

## Settings

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubAbout | 12 | Done | Settings > About Rampagio (our own credits) |
| SubCreatorSettings | 2 | Done | Spawner > Object Spawner > Cam Settings |
| SubLanguageManager | 2 | Dropped | The label translation files of Rampage; Rampagio has no translated labels |
| SubOverlaySettings | 17 | Done | Settings > Overlay Settings |
| SubSettings | 1 | Partial | Settings: Search, Core, Theme, Hotkey Manager (F11 on a row binds it, ours), Load / Save; Gamepad Open Key is a combo choice; Language picks one of the 13 compiled-in translations or follows the game (ours); Plugins has no Rampagio counterpart |
| SubSettingsColor | 12 | Done | Settings > Theme: eight colors, Main Font and Body Font (the faces of Rampage), Menu Title, menu position and Max Display Options |
| SubSettingsCore | 10 | Done | Settings > Core: Gamepad Controls, Menu Sounds, Mouse Controls (ours: hover, click, right-click back, wheel), Show Controller Screen; welcome/ToS/update/landing rows dropped |
| SubSettingsCustomThemes | 2 | Done | Settings > Theme > Custom Themes: the themes component of Rampagio.json |
| SubSettingsLoadSave | 7 | Done | Settings > Load / Save: Rampagio.json |
| SubSettingsPremadeThemes | 26 | Done | Settings > Theme > Premade Themes: our own seven presets (Rampage has 26 of its own) |
| SubSettingsXUI | 6 | Partial | Settings > Theme: Max Display Options, Invert Colors and Centered Title; Teleport Map, Spawner Previews and Ink Rendering are overlay windows that wait for the ImGui overlay (with the tabled Window Manager) |
| SubWindowManager | 7 | Tabled | ImGui windows (log, sysinfo, performance, hotkeys): waits for the ImGui overlay |

## Spawners

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubObjSpawnerAllObjs | 1 | Done | Spawner > Object Spawner > All Objects: game script objects plus Rampagio_ObjectList.txt |
| SubObjSpawnerDatabase | 1 | Done | Spawner > Object Spawner > Object Database: per-object actions (ours) until the Object Editor is back |
| SubObjSpawnerLoadSave | 7 | Done | Spawner > Object Spawner > Load / Save: the spooner database XML of Rampage (objects, spawned vehicles, the Ped Spawner database) in Rampagio_Spooner; reads Rampage files too; older Rampagio_Spooner.json sets still load |
| SubObjSpawnerPropsets | 2 | Done | Spawner > Object Spawner > Propsets: pg_ names from the game scripts |
| SubObjectSpawner | 3 | Done | Spawner > Object Spawner: objects from the game scripts; creator cam is our own free cam |
| SubPedSpawner | 2 | Done | Spawner > Ped Spawner: models from the game scripts; Hijack Ped adds the first ped of a typed model to the database |
| SubPedSpawnerAddon | 3 | Done | Spawner > Ped Spawner > Addon Peds: Rampagio_AddonPeds.txt, re-read on open |
| SubPedSpawnerAnimal | 1 | Done | Spawner > Ped Spawner > Animals: Legendary Animals are the table of Rampage (model plus outfit preset, data/LegendaryAnimals.inc), the story ones marked when killed (ours); Fishes gets its legendary fish too |
| SubPedSpawnerDatabase | 2 | Done | Spawner > Ped Spawner > Ped Database: per-ped actions, and Ped Editor opens the full editor |
| SubPedSpawnerDispatch | 1 | Done | Spawner > Law Dispatch Spawner: LAW_ responses from the game scripts plus the table of Rampage, which also sets the law region of each response (data/LawDispatchRegions.inc) |
| SubPedSpawnerFish | 1 | Done | Spawner > Ped Spawner > Fishes |
| SubPedSpawnerHorses | 2 | Done | Spawner > Ped Spawner > Horses: one list from the game scripts |
| SubPedSpawnerPeds | 0 | Done | Spawner > Ped Spawner > Humans: grouped by model prefix |
| SubPedSpawnerPedsList | 1 | Done | Spawner > Ped Spawner > Humans > <group> |
| SubPedSpawnerSettings | 13 | Done | Spawner > Ped Spawner > Spawner Settings: Scale and Health are a toggle plus a value row each |
| SubPlantSpawner | 1 | Done | Spawner > Plant Spawner: the 84 COMPOSITE_LOOTABLE_ composites the scripts name |

## Teleport

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubTeleport | 41 | Done | Teleport: Common Locations, Camps and Safe Houses and the region submenus from the Rampage lists (data/Teleports.inc); Blips reads the location and mission blip globals Rampage reads (1491.50 indices) and names them from its table (data/BlipLabels.inc) |
| SubTeleportCustom | 2 | Done | Teleport > Load Custom / Delete Custom |
| SubTeleportShopsandStuff | 0 | Done | Teleport > Shops and Services: the Rampage list, 14 categories, 67 locations (data/Teleports.inc) |

## Vehicles

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubTrainCreator | 7 | Done | Vehicle > Train Creator: the 23 train configs the game scripts use, shown by hash and car count (Rampage has 28 named ones in its own table) |
| SubTrainWhistle | 8 | Done | Vehicle > Train Creator > Whistle |
| SubVehicle | 19 | Done | Vehicle: Invincible Vehicle re-applies to the current vehicle every frame (ours); Fly Speed and Ground Force are separate rows |
| SubVehicleAI | 2 | Done | Vehicle > Vehicle AI |
| SubVehicleChaffeur | 4 | Done | Vehicle > Chauffeur |
| SubVehicleExtras | 0 | Done | Vehicle > Customization |
| SubVehiclePaintOptions | 0 | Done | Vehicle > Paint Options |
| SubVehiclePropsets | 3 | Done | Vehicle > Propsets: every vehicle propset the game scripts name (tools/extract_vehicles.py) instead of a per-model table |
| SubVehiclePv | 4 | Done | Vehicle > Blip |
| SubVehicleSpawner | 2 | Done | Spawner > Vehicle Spawner: lists by type from the game scripts; settings are ours; JSON Loader reads Rampage vehicle files from Rampagio_Vehicles |

## Weapons

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubWeaponModifiers | 5 | Done | Weapon > Weapon Modifiers: Weapon Scale re-applies on weapon switch (ours); Skill sets the current weapon stat |
| SubWeaponVisuals | 12 | Done | Weapon > Weapon Visuals: crosshair sprites and colour, arrow trails, condition; Disable Hitmarker / Hit Feedback patch the jz at the signatures of Rampage (BytePatch, guarded; the signatures could only be checked in memory, not against the protected exe) |
| SubWeapons | 33 | Done | Weapon: Rope Gun pulls toward the impact and Portal Gun uses markers (ours) |
| SubWeaponsAimbot | 7 | Done | Weapon > Aimbot: target filter (all/humans/animals) and Ignore Dying Peds are ours |
| SubWeaponsAmmunition | 5 | Done | Weapon > Ammunition: Drop Ammo offers six PICKUP_AMMO_ kinds from the game scripts |
| SubWeaponsBullets | 8 | Done | Weapon > Weapon Bullets: Particle Gun uses the script effects list; Ped / Vehicle Gun models and Remote Cannonball steering are ours |
| SubWeaponsGive | 0 | Done | Weapon > Manage Weapons > Give Weapon |
| SubWeaponsManage | 10 | Done | Weapon > Manage Weapons: Upgrade Weapon tries the COMPONENT_ names from the game scripts |

## World

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubIMAPCustom | 3 | Done | World > IPL Loader > Custom: Rampagio_IPLList.xml (same format as Rampage) |
| SubIMAPCustomSet | 3 | Done | World > IPL Loader > IPL Sets: XML set files in a RampagioIPLS folder, same format as the IPLS folder of Rampage |
| SubWorld | 12 | Done | World: also the Cloud Editor and Ambient Light submenus (unnamed builders in Rampage) |
| SubWorldDoorManager | 2 | Done | World > Door Manager: the doors the game scripts name, listed when registered (Rampage uses its own door table) |
| SubWorldIMAPLoader | 6 | Done | World > IPL Loader: Map Sets lists the 173 IPLs the game scripts load (Rampage has its own set table); interior entity sets by name |
| SubWorldLocalObjects | 3 | Partial | World > Object Manager: Object Finder belongs to the tabled Object Editor area |
| SubWorldLocalPeds | 19 | Done | World > Ped Manager: scanner labels instead of the ESP toggle; Hostile Peds gives a repeater (Rampage picks from its own weapon list) |
| SubWorldLocalVehicles | 11 | Done | World > Vehicle Manager: scanner labels instead of the ESP toggle |
| SubWorldOcean | 5 | Done | World > Water |
| SubWorldStates | 1 | Done | World > World States: by state id (Global_40.f_283 bitset), no names |
| SubWorldTime | 10 | Done | World > Time |
| SubWorldTornado | 10 | Done | World > Tornado & Black Hole: own force maths; meteors use script rock models |
| SubWorldWeather | 13 | Done | World > Weather |

