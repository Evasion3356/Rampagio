"""Regenerates docs/PORTING.md from tools/rampage_inventory.csv and the STATUS
table below. Update STATUS as submenus are ported, then run:

    python tools/porting_status.py
"""
import csv,collections,sys
rows=list(csv.DictReader(open('tools/rampage_inventory.csv',encoding='utf-8')))
INTER={"action","toggle","toggle_plain","toggle_value","toggle_cb","choice","slider_float","slider_int","number_int","value_hex","value_pick","color","action_icon","component_pick","posse_item","location"}
cnt=collections.Counter(); area={}
for r in rows:
    area[r['submenu']]=r['area']
    if r['kind'] in INTER: cnt[r['submenu']]+=1
# Areas the user has set aside for now; their submenus show as Tabled unless STATUS says otherwise.
TABLED_AREAS = {'Debug','Object Editor','Script Tools'}
STATUS = {
 'SubSelf':('Done','Player'),
 'SubSelfHorse':('Done','Horse'),
 'SubSelfScenarios':('Done','Player > Scenarios: list from the game scripts plus Custom Input/Search; no Scenarios.txt reload'),
 'SubSelfWardrobe':('Done','Player > Wardrobe: clothing list from a user-supplied Rampagio_ClothingDb.xml (same format as Rampage); Keep Facial Hair is a toggle instead of holding Shift (ours)'),
 'SubSelfWardrobeComponent':('Done','Player > Wardrobe > Components > <category>: picking an item applies it, then opens its wearable states (ours)'),
 'SubSelfWardrobeWearableState':('Done','Player > Wardrobe > Components > Wearable State'),
 'SubSelfOutfitSaver':('Done','Player > Wardrobe > Outfits: saved in Rampagio_Outfits.ini (ours; Rampage writes an XML per outfit)'),
 'SubSelfFacialHair':('Partial','Player > Wardrobe > Hair and Weight: Go to Barber not ported (coordinate from a Rampage table)'),
 'SubSelfPedMetaTags':('Done','Player > Wardrobe > Meta Ped Tags'),
 'SubSelfPedMetaExpressions':('Done','Player > Wardrobe > Meta Ped Expressions: names from the MetaPedExpression list alloc8or links'),
 'SubSelfCustomizations':('Partial','Player > Wardrobe > Overlay Textures: texture hashes typed in; the per-overlay texture tables of Rampage (TX Id) not ported'),
 'SubSelfModelChanger':('Partial','Player > Wardrobe > Model Changer: Force Player Type not ported'),
 'SubModelChangerPeds':('Done','Player > Wardrobe > Model Changer > Humans: models from the game scripts, grouped by prefix'),
 'SubModelChangerPedsList':('Done','Player > Wardrobe > Model Changer > Humans > <group>'),
 'SubModelChangerHorses':('Done','Player > Wardrobe > Model Changer > Horses: one list from the game scripts'),
 'SubModelChangerHorsesList':('Done','Player > Wardrobe > Model Changer > Horses'),
 'SubModelChangerAnimal':('Done','Player > Wardrobe > Model Changer > Animals: one list from the game scripts'),
 'SubSelfWalkStyles':('Done','Player > Wardrobe > Walk Styles: list from the game scripts'),
 'SubDamagePacks':('Done','Player > Wardrobe > Apply Damage Packs: list from the game scripts'),
 'SubTimecycleMod':('Done','Player > Vision > Timecycle Modifiers: list from the game scripts'),
 'SubAnimPostFx':('Done','Player > Vision > Screen Effects: list from the game scripts'),
 'SubMoods':('Done','Player > Moods: mood_ anims from the game scripts'),
 'SubAbilities':('Done','Player > Abilities: recharge toggles restore 1.0 when off (ours)'),
 'SubSelfAnimationsCustom':('Done','Player > Animations > Custom Animations: the Custom Flag submenu is a typed Custom Flags row (decimal or 0x hex)'),
 'SubSelfAnimationsDicts':('Done','Player > Animations > Dictionaries: script list plus Rampagio_PedAnimList.txt, re-read on open instead of Reload List'),
 'SubAnimationDictsList':('Done','Player > Animations > Dictionaries > (dictionary): picking one also fills in Custom Animations (ours)'),
 'SubSelfFacialAnimations':('Done','Player > Animations > Facial Animations'),
 'SubEffects':('Partial','Player > Effects: Scale, Loop, Custom asset/effect, 6 effects from the game scripts; Rampage\'s 25 named presets are its own table and not ported'),
 'SubEmotes':('Done','Player > Emotes: every emote type listed under its own heading instead of an Emote Type choice; adds gun twirls (alloc8or eEmote list)'),
 'SubPlaySpeech':('Done','Player > Play Speech'),
 'SubPlaySpeechRegular':('Done','Player > Play Speech > Regular Speeches: 208 lines from the game scripts (Rampage\'s table has 502); one Display All for every list'),
 'SubPlaySpeechFlowgreet':('Done','Player > Play Speech > Flow Greets: Rampagio_SpeechFlowGreets.txt, re-read on open'),
 'SubPlaySpeechVignettes':('Done','Player > Play Speech > Vignettes: Rampagio_SpeechVignettes.txt, re-read on open'),
 'SubPlaySpeechCustom':('Done','Player > Play Speech > Custom Speeches: Rampagio_SpeechList.txt, re-read on open'),
 'SubVoiceChanger':('Done','Player > Play Speech > Voice Changer: Set Voice lists the script voices plus Custom Input (ours)'),
 'SubPlayerProofs':('Done','Player > Player Proofs: re-applied every frame (ours)'),
 'SubPlayerConfigFlags':('Done','Player > Config Flags: flag by number (no names), shows current state (ours)'),
 'SubPlayerPosse':('Partial','Player > Posse: commands are menu rows (Posse Commands hotkeys wait for Rampagio hotkeys); Add Aimed/Nearest Ped, Teleport, Dismiss and Delete rows (ours)'),
 'SubVehicle':('Done','Vehicle: Invincible Vehicle re-applies to the current vehicle every frame (ours); Fly Speed and Ground Force are separate rows'),
 'SubVehiclePv':('Done','Vehicle > Blip'),
 'SubVehicleAI':('Done','Vehicle > Vehicle AI'),
 'SubVehicleChaffeur':('Done','Vehicle > Chauffeur'),
 'SubVehiclePaintOptions':('Done','Vehicle > Paint Options'),
 'SubVehiclePropsets':('Done','Vehicle > Propsets: every vehicle propset the game scripts name (tools/extract_vehicles.py) instead of a per-model table'),
 'SubVehicleExtras':('Done','Vehicle > Customization'),
 'SubTrainCreator':('Done','Vehicle > Train Creator: the 23 train configs the game scripts use, shown by hash and car count (Rampage has 28 named ones in its own table)'),
 'SubTrainWhistle':('Done','Vehicle > Train Creator > Whistle'),
 'SubPedSpawner':('Done','Spawner > Ped Spawner: models from the game scripts; Hijack Ped adds the first ped of a typed model to the database'),
 'SubPedSpawnerSettings':('Done','Spawner > Ped Spawner > Spawner Settings: Scale and Health are a toggle plus a value row each'),
 'SubPedSpawnerDatabase':('Done','Spawner > Ped Spawner > Ped Database: per-ped actions (teleport, posse, bodyguard, enemy, revive, kill, delete) until the Ped Editor is ported'),
 'SubPedSpawnerPeds':('Done','Spawner > Ped Spawner > Humans: grouped by model prefix'),
 'SubPedSpawnerPedsList':('Done','Spawner > Ped Spawner > Humans > <group>'),
 'SubPedSpawnerHorses':('Done','Spawner > Ped Spawner > Horses: one list from the game scripts'),
 'SubPedSpawnerAnimal':('Partial','Spawner > Ped Spawner > Animals: Legendary Animals lists the legendary models the scripts name; the outfit-preset legendaries of Rampage are its own table'),
 'SubPedSpawnerFish':('Done','Spawner > Ped Spawner > Fishes'),
 'SubPedSpawnerAddon':('Done','Spawner > Ped Spawner > Addon Peds: Rampagio_AddonPeds.txt, re-read on open'),
 'SubPedSpawnerDispatch':('Partial','Spawner > Law Dispatch Spawner: 49 LAW_ responses from the game scripts; the law region per response (Rampage table) is not set'),
 'SubVehicleSpawner':('Partial','Spawner > Vehicle Spawner: lists by type from the game scripts; settings are ours; JSON Loader not ported'),
 'SubObjectSpawner':('Done','Spawner > Object Spawner: objects from the game scripts; creator cam is our own free cam'),
 'SubObjSpawnerDatabase':('Done','Spawner > Object Spawner > Object Database: per-object actions (ours) until the Object Editor is back'),
 'SubObjSpawnerAllObjs':('Done','Spawner > Object Spawner > All Objects: game script objects plus Rampagio_ObjectList.txt'),
 'SubObjSpawnerPropsets':('Done','Spawner > Object Spawner > Propsets: pg_ names from the game scripts'),
 'SubObjSpawnerLoadSave':('Partial','Spawner > Object Spawner > Load / Save: objects only, in Rampagio_Spooner.ini (ours); no spooner XML, peds or vehicles'),
 'SubPlantSpawner':('Done','Spawner > Plant Spawner: the 84 COMPOSITE_LOOTABLE_ composites the scripts name'),
 'SubTeleport':('Partial','Teleport: own town list; Rampage\'s region/camp/shop location lists and Blips not ported'),
 'SubTeleportCustom':('Done','Teleport > Load Custom / Delete Custom'),
 'SubWorld':('Partial','World: main rows done; Water, Cloud Editor, managers, Door Manager, Tornado, IPL, World States, Ambient Light pending'),
 'SubWorldTime':('Done','World > Time'),
 'SubRecoveryMoney':('Done','Recovery > Money: drop is a timed toggle (ours) instead of every tick'),
 'SubRecoveryHonor':('Partial','Recovery > Honor: all actions; current honor reads honor_current; honor HUD meter not shown'),
 'SubRecoveryBounty':('Done','Recovery > Bounty: state rows also show each state bounty (ours)'),
 'SubRecoveryCores':('Done','Recovery > Cores: temporary rank applies on every step (ours)'),
 'SubRecoveryAddItems':('Done','Recovery > Add Items: Unlimited Items also blocks by-GUID removals (ours)'),
 'SubRecoveryGiveItemsList':('Done','Recovery > Add Items > Give Items: own item list from the game scripts (tools/extract_items.py); method is a visible choice instead of Shift (ours)'),
 'SubRecoveryUnlocks':('Done','Recovery > Unlocks: own lists from the game scripts (tools/extract_unlocks.py); Unlock Checks lists every unlock the scripts name; Add Entries writes every journal entry the game allows; Discover Fish skips legendary outfit presets (ours)'),
 'SubUnlockCheats':('Done','Recovery > Unlocks > Cheat Codes: activates each cheat through the game cheat state instead of showing its phrase (ours)'),
 'SubCollectiblesCigaretteCards':('Done','Recovery > Collectibles > Cigarette Cards: sets and card models from the game (CARD_SET_* collectables, s_inv_cigcard_* models); Auto Collect All runs as a stoppable per-frame job (ours)'),
 'SubCollectiblesCigaretteCardsSet':('Done','Recovery > Collectibles > Cigarette Cards > <set>: cards by in-game name with owned count; Give Missing Cards (ours)'),
 'SubCollectiblesDinoBones':('Done','Recovery > Collectibles > Dino Bones: read from the dino_bones collectable category with _COLLECTABLE_GET_PLACEMENT_LOCATION; locations in a Locations list'),
 'SubCollectiblesDreamcatchers':('Done','Recovery > Collectibles > Dreamcatchers: coordinates from discoverable_generic_location (tools/extract_collectibles.py), found state from Global_40.f_8863.f_148'),
 'SubCollectiblesRockCarvings':('Done','Recovery > Collectibles > Rock Carvings: read from the rock_carvings collectable category; locations in a Locations list'),
 'SubMapDiscoverables':('Partial','Recovery > Unlocks > Map Discoverables: 79 discoveries the scripts name; Rampage lists 284 (incl. animal/fish map discoverables)'),
 'SubWorldWeather':('Done','World > Weather'),
 'SubWeapons':('Partial','Weapon: Always Kill Cam, Thunder Hawk, Rope Gun, Portal Gun, Debug Gun pending'),
 'SubWeaponsManage':('Partial','Weapon > Manage Weapons: Give Favourite, Upgrade Weapon, Add Component, Get Duplicate Model pending'),
 'SubWeaponsGive':('Done','Weapon > Manage Weapons > Give Weapon'),
 'SubWeaponsAmmunition':('Partial','Weapon > Ammunition: Drop Ammo pending'),
 'SubWeaponModifiers':('Partial','Weapon > Weapon Modifiers: weapon model swap and weapon skill stats pending'),
}
out=['# Porting status','','Each Rampage submenu (`Submenus::SubXxx`, see `tools/rampage_inventory.md`) and where Rampagio has it.','"Options" counts Rampage\'s static interactive rows. Statuses: Done (every row has an equivalent), Partial, Pending, Tabled (set aside for now). Nothing is live-tested yet.','']
by=collections.defaultdict(list)
for s in sorted(set(area)): by[area[s]].append(s)
tot=collections.Counter()
for a in sorted(by):
    out+=['## '+a,'','| Rampage submenu | Options | Status | Rampagio |','|---|---|---|---|']
    for s in by[a]:
        st,where=STATUS.get(s,('Tabled' if a in TABLED_AREAS else 'Pending',''))
        tot[st]+=1
        out.append('| %s | %d | %s | %s |'%(s,cnt[s],st,where))
    out.append('')
out[5:5]=['Submenus: %d done, %d partial, %d pending, %d tabled.'%(tot['Done'],tot['Partial'],tot['Pending'],tot['Tabled']),'']
open('docs/PORTING.md','w',encoding='utf-8',newline='\r\n').write('\n'.join(out)+'\n')
print(tot)
