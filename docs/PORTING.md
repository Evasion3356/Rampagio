# Porting status

Each Rampage submenu (`Submenus::SubXxx`, see `tools/rampage_inventory.md`) and where Rampagio has it.
"Options" counts Rampage's static interactive rows. Statuses: Done (every row has an equivalent), Partial, Pending, Tabled (set aside for now), Dropped (online only). Nothing is live-tested yet.

Submenus: 86 done, 19 partial, 52 pending, 9 tabled, 1 dropped.

## Debug

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubDebug | 21 | Tabled |  |

## Horse

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubHorseBlip | 3 | Pending |  |
| SubHorseLoader | 2 | Pending |  |
| SubHorsePedMetaExpressions | 0 | Pending |  |
| SubHorsePedMetaTags | 6 | Pending |  |
| SubHorseStats | 6 | Pending |  |
| SubMobileStable | 3 | Pending |  |
| SubMobileStableComponent | 10 | Pending |  |
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
| SubPedEditor | 19 | Pending |  |
| SubPedEditorAnimationDictsList | 4 | Pending |  |
| SubPedEditorAnimationsCustom | 6 | Pending |  |
| SubPedEditorAnimationsDicts | 1 | Pending |  |
| SubPedEditorCombat | 4 | Pending |  |
| SubPedEditorCombatAttributes | 0 | Pending |  |
| SubPedEditorDamagePacks | 3 | Pending |  |
| SubPedEditorEffects | 25 | Pending |  |
| SubPedEditorEmotes | 8 | Pending |  |
| SubPedEditorFacialAnimations | 4 | Pending |  |
| SubPedEditorGeneral | 13 | Pending |  |
| SubPedEditorMetaExpressions | 0 | Pending |  |
| SubPedEditorMetaTags | 7 | Pending |  |
| SubPedEditorPlaySpeech | 1 | Pending |  |
| SubPedEditorPlaySpeechCustom | 3 | Pending |  |
| SubPedEditorPlaySpeechFlowgreet | 3 | Pending |  |
| SubPedEditorPlaySpeechRegular | 2 | Pending |  |
| SubPedEditorPlaySpeechVignettes | 3 | Pending |  |
| SubPedEditorScenarios | 7 | Pending |  |
| SubPedEditorVoiceChanger | 1 | Pending |  |
| SubPedEditorWalkStyles | 2 | Pending |  |
| SubPedEditorWardrobe | 10 | Pending |  |
| SubPedEditorWardrobeComponent | 3 | Pending |  |
| SubPedEditorWardrobeWearableState | 1 | Pending |  |
| SubPedEditorWeapons | 6 | Pending |  |
| SubPedEditorWeaponsGive | 0 | Pending |  |
| SubPedPositioning | 0 | Pending |  |
| SubPedSelectAttachment | 3 | Pending |  |

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
| SubPlayerPosse | 11 | Partial | Player > Posse: commands are menu rows (Posse Commands hotkeys wait for Rampagio hotkeys); Add Aimed/Nearest Ped, Teleport, Dismiss and Delete rows (ours) |
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
| SubAbout | 12 | Pending |  |
| SubCreatorSettings | 2 | Pending |  |
| SubLanguageManager | 2 | Pending |  |
| SubOverlaySettings | 17 | Pending |  |
| SubSettings | 1 | Pending |  |
| SubSettingsColor | 12 | Pending |  |
| SubSettingsCore | 10 | Pending |  |
| SubSettingsCustomThemes | 2 | Pending |  |
| SubSettingsLoadSave | 7 | Pending |  |
| SubSettingsPremadeThemes | 26 | Pending |  |
| SubSettingsXUI | 6 | Pending |  |
| SubWindowManager | 7 | Pending |  |

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
| SubPedSpawnerDatabase | 2 | Done | Spawner > Ped Spawner > Ped Database: per-ped actions (teleport, posse, bodyguard, enemy, revive, kill, delete) until the Ped Editor is ported |
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
| SubTeleport | 41 | Partial | Teleport: own town list; Rampage's region/camp/shop location lists and Blips not ported |
| SubTeleportCustom | 2 | Done | Teleport > Load Custom / Delete Custom |
| SubTeleportShopsandStuff | 0 | Pending |  |

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
| SubWeaponModifiers | 5 | Partial | Weapon > Weapon Modifiers: weapon model swap and weapon skill stats pending |
| SubWeaponVisuals | 12 | Pending |  |
| SubWeapons | 33 | Partial | Weapon: Always Kill Cam, Thunder Hawk, Rope Gun, Portal Gun, Debug Gun pending |
| SubWeaponsAimbot | 7 | Pending |  |
| SubWeaponsAmmunition | 5 | Partial | Weapon > Ammunition: Drop Ammo pending |
| SubWeaponsBullets | 8 | Pending |  |
| SubWeaponsGive | 0 | Done | Weapon > Manage Weapons > Give Weapon |
| SubWeaponsManage | 10 | Partial | Weapon > Manage Weapons: Give Favourite, Upgrade Weapon, Add Component, Get Duplicate Model pending |

## World

| Rampage submenu | Options | Status | Rampagio |
|---|---|---|---|
| SubIMAPCustom | 3 | Done | World > IPL Loader > Custom: Rampagio_IPLList.xml (same format as Rampage) |
| SubIMAPCustomSet | 3 | Pending | IPL set files (Rampage IPLS folder XML) not ported |
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

