# Porting status

Each Rampage submenu (`Submenus::SubXxx`, see `tools/rampage_inventory.md`) and where Rampagio has it.
"Options" counts Rampage's static interactive rows. Statuses: Done (every row has an equivalent), Partial, Pending, Tabled (set aside for now), Dropped (online only, or Rampage infrastructure with no Rampagio counterpart). Nothing is live-tested yet.

Submenus: 134 done, 21 partial, 0 pending, 10 tabled, 2 dropped.

## Debug

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubDebug | 21 | Tabled |  |

## Horse

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubHorseBlip | 3 | Done | Horse > Blip |
| SubHorseLoader | 2 | Done | Horse > Horse Loader: model, meta tags and gender in Rampagio_Horses.ini (ours) |
| SubHorsePedMetaExpressions | 0 | Done | Horse > Meta Ped Expressions: the horse expressions of the shared menu |
| SubHorsePedMetaTags | 6 | Done | Horse > Meta Ped Tags: the shared menu with Index / Load Data from Index |
| SubHorseStats | 6 | Done | Horse > Horse Stats: ranks 0-10 per attribute |
| SubMobileStable | 3 | Done | Horse > Mobile Stable: 280 HORSE_EQUIPMENT_ items from the game scripts, grouped by kind |
| SubMobileStableComponent | 10 | Partial | Horse > Mobile Stable > <kind>: picking applies the item; no per-item tint picks |
| SubSelfHorse | 33 | Done | Horse |

## Misc

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubCutscenePlayer | 7 | Partial | Miscellaneous > Cutscene Player: 428 mission cutscenes from the game scripts; Try to Populate (Rampage model table) and the Red Dead Online list not ported |
| SubEditVolume | 2 | Done | Miscellaneous > Volume Editor: edits volumes created there (ours); relationship groups from the game scripts |
| SubFriendlist | 1 | Dropped | Online only (Social Club friends) |
| SubGameMusic | 8 | Done | Miscellaneous > Game Music: 314 music events from the game scripts |
| SubMinigames | 1 | Done | Miscellaneous > Minigames > Undead Nightmare II: own wave logic (kill count spawns bosses) on the models and bosses Rampage uses |
| SubMiscellaneous | 18 | Partial | Miscellaneous: Air Walk holds a fixed height (ours); Take a Photo saves without the district/state photo stats; About lives in Settings; Global Editor and Script Tools rows wait for the tabled Script Tools area |
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
| SubPedEditorEffects | 25 | Partial | Ped Editor > Effects: the shared Player menu, opened from the Ped Editor (Menus::Target); the 25 presets of Rampage not ported |
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
| SubEffects | 29 | Partial | Player > Effects: Scale, Loop, Custom asset/effect, 6 effects from the game scripts; Rampage's 25 named presets are its own table and not ported |
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
| SubSelfCustomizations | 4 | Partial | Player > Wardrobe > Overlay Textures: texture hashes typed in; the per-overlay texture tables of Rampage (TX Id) not ported |
| SubSelfFacialAnimations | 4 | Done | Player > Animations > Facial Animations |
| SubSelfFacialHair | 4 | Partial | Player > Wardrobe > Hair and Weight: Go to Barber not ported (coordinate from a Rampage table) |
| SubSelfModelChanger | 8 | Partial | Player > Wardrobe > Model Changer: Force Player Type not ported |
| SubSelfOutfitSaver | 3 | Done | Player > Wardrobe > Outfits: saved in Rampagio_Outfits.ini (ours; Rampage writes an XML per outfit) |
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
| SubCollectiblesDinoBones | 2 | Done | Recovery > Collectibles > Dino Bones: read from the dino_bones collectable category with _COLLECTABLE_GET_PLACEMENT_LOCATION; locations in a Locations list |
| SubCollectiblesDreamcatchers | 2 | Done | Recovery > Collectibles > Dreamcatchers: coordinates from discoverable_generic_location (tools/extract_collectibles.py), found state from Global_40.f_8863.f_148 |
| SubCollectiblesRockCarvings | 2 | Done | Recovery > Collectibles > Rock Carvings: read from the rock_carvings collectable category; locations in a Locations list |
| SubMapDiscoverables | 2 | Partial | Recovery > Unlocks > Map Discoverables: 79 discoveries the scripts name; Rampage lists 284 (incl. animal/fish map discoverables) |
| SubRecoveryAddItems | 13 | Done | Recovery > Add Items: Unlimited Items also blocks by-GUID removals (ours) |
| SubRecoveryBounty | 9 | Done | Recovery > Bounty: state rows also show each state bounty (ours) |
| SubRecoveryCores | 9 | Done | Recovery > Cores: temporary rank applies on every step (ours) |
| SubRecoveryGiveItemsList | 1 | Done | Recovery > Add Items > Give Items: own item list from the game scripts (tools/extract_items.py); method is a visible choice instead of Shift (ours) |
| SubRecoveryHonor | 5 | Partial | Recovery > Honor: all actions; current honor reads honor_current; honor HUD meter not shown |
| SubRecoveryMoney | 5 | Done | Recovery > Money: drop is a timed toggle (ours) instead of every tick |
| SubRecoveryUnlocks | 8 | Done | Recovery > Unlocks: own lists from the game scripts (tools/extract_unlocks.py); Unlock Checks lists every unlock the scripts name; Add Entries writes every journal entry the game allows; Discover Fish skips legendary outfit presets (ours) |
| SubStatEditor | 6 | Done | Miscellaneous > Stat Editor |
| SubUnlockCheats | 1 | Done | Recovery > Unlocks > Cheat Codes: activates each cheat through the game cheat state instead of showing its phrase (ours) |

## Script Tools

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubGlobalEditor | 7 | Tabled |  |
| SubScriptEditor | 3 | Tabled |  |
| SubScriptPatcher | 7 | Tabled |  |
| SubScriptTools | 3 | Tabled |  |

## Settings

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubAbout | 12 | Done | Settings > About Rampagio (our own credits) |
| SubCreatorSettings | 2 | Done | Spawner > Object Spawner > Cam Settings |
| SubLanguageManager | 2 | Dropped | The label translation files of Rampage; Rampagio has no translated labels |
| SubOverlaySettings | 17 | Done | Settings > Overlay Settings |
| SubSettings | 1 | Partial | Settings: Search, Core, Theme, Hotkey Manager (F11 on a row binds it, ours), Load / Save; Gamepad Open Key is a combo choice; Plugins and Language have no Rampagio counterpart |
| SubSettingsColor | 12 | Partial | Settings > Theme: eight colors, menu position and Max Display Options; no font choice or menu title text |
| SubSettingsCore | 10 | Partial | Settings > Core: Gamepad Controls, Menu Sounds, Show Controller Screen; Mouse Controls still to come (CLAUDE.md UI direction); welcome/ToS/update/landing rows dropped |
| SubSettingsCustomThemes | 2 | Done | Settings > Theme > Custom Themes: Rampagio_Themes.ini |
| SubSettingsLoadSave | 7 | Done | Settings > Load / Save: Rampagio_Settings.ini and Rampagio_Toggles.ini |
| SubSettingsPremadeThemes | 26 | Done | Settings > Theme > Premade Themes: our own seven presets (Rampage has 26 of its own) |
| SubSettingsXUI | 6 | Partial | Settings > Theme > Max Display Options; teleport map, spawner previews, ink rendering, inverted colors and centered title not ported |
| SubWindowManager | 7 | Tabled | ImGui windows (log, sysinfo, performance, hotkeys): waits for the ImGui overlay |

## Spawners

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubObjSpawnerAllObjs | 1 | Done | Spawner > Object Spawner > All Objects: game script objects plus Rampagio_ObjectList.txt |
| SubObjSpawnerDatabase | 1 | Done | Spawner > Object Spawner > Object Database: per-object actions (ours) until the Object Editor is back |
| SubObjSpawnerLoadSave | 7 | Partial | Spawner > Object Spawner > Load / Save: objects only, in Rampagio_Spooner.ini (ours); no spooner XML, peds or vehicles |
| SubObjSpawnerPropsets | 2 | Done | Spawner > Object Spawner > Propsets: pg_ names from the game scripts |
| SubObjectSpawner | 3 | Done | Spawner > Object Spawner: objects from the game scripts; creator cam is our own free cam |
| SubPedSpawner | 2 | Done | Spawner > Ped Spawner: models from the game scripts; Hijack Ped adds the first ped of a typed model to the database |
| SubPedSpawnerAddon | 3 | Done | Spawner > Ped Spawner > Addon Peds: Rampagio_AddonPeds.txt, re-read on open |
| SubPedSpawnerAnimal | 1 | Partial | Spawner > Ped Spawner > Animals: Legendary Animals lists the legendary models the scripts name; the outfit-preset legendaries of Rampage are its own table |
| SubPedSpawnerDatabase | 2 | Done | Spawner > Ped Spawner > Ped Database: per-ped actions, and Ped Editor opens the full editor |
| SubPedSpawnerDispatch | 1 | Partial | Spawner > Law Dispatch Spawner: 49 LAW_ responses from the game scripts; the law region per response (Rampage table) is not set |
| SubPedSpawnerFish | 1 | Done | Spawner > Ped Spawner > Fishes |
| SubPedSpawnerHorses | 2 | Done | Spawner > Ped Spawner > Horses: one list from the game scripts |
| SubPedSpawnerPeds | 0 | Done | Spawner > Ped Spawner > Humans: grouped by model prefix |
| SubPedSpawnerPedsList | 1 | Done | Spawner > Ped Spawner > Humans > <group> |
| SubPedSpawnerSettings | 13 | Done | Spawner > Ped Spawner > Spawner Settings: Scale and Health are a toggle plus a value row each |
| SubPlantSpawner | 1 | Done | Spawner > Plant Spawner: the 84 COMPOSITE_LOOTABLE_ composites the scripts name |

## Teleport

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubTeleport | 41 | Partial | Teleport: Common Locations, Camps and Safe Houses and the region submenus from the Rampage lists (data/Teleports.inc); Blips not ported |
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
| SubVehicleSpawner | 2 | Partial | Spawner > Vehicle Spawner: lists by type from the game scripts; settings are ours; JSON Loader not ported |

## Weapons

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubWeaponModifiers | 5 | Done | Weapon > Weapon Modifiers: Weapon Scale re-applies on weapon switch (ours); Skill sets the current weapon stat |
| SubWeaponVisuals | 12 | Partial | Weapon > Weapon Visuals: crosshair sprites and colour, arrow trails, condition; Disable Hitmarker / Hit Feedback (Rampage byte patches) not ported |
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

