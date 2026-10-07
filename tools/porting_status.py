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
STATUS = {
 'SubSelf':('Done','Player'),
 'SubSelfHorse':('Done','Horse'),
 'SubTeleport':('Partial','Teleport: own town list; Rampage\'s region/camp/shop location lists and Blips not ported'),
 'SubTeleportCustom':('Done','Teleport > Load Custom / Delete Custom'),
 'SubWorld':('Partial','World: main rows done; Water, Cloud Editor, managers, Door Manager, Tornado, IPL, World States, Ambient Light pending'),
 'SubWorldTime':('Done','World > Time'),
 'SubRecoveryMoney':('Done','Recovery > Money: drop is a timed toggle (ours) instead of every tick'),
 'SubRecoveryHonor':('Partial','Recovery > Honor: all actions; current honor reads honor_current; honor HUD meter not shown'),
 'SubWorldWeather':('Done','World > Weather'),
 'SubWeapons':('Partial','Weapon: Always Kill Cam, Thunder Hawk, Rope Gun, Portal Gun, Debug Gun pending'),
 'SubWeaponsManage':('Partial','Weapon > Manage Weapons: Give Favourite, Upgrade Weapon, Add Component, Get Duplicate Model pending'),
 'SubWeaponsGive':('Done','Weapon > Manage Weapons > Give Weapon'),
 'SubWeaponsAmmunition':('Partial','Weapon > Ammunition: Drop Ammo pending'),
 'SubWeaponModifiers':('Partial','Weapon > Weapon Modifiers: weapon model swap and weapon skill stats pending'),
}
out=['# Porting status','','Each Rampage submenu (`Submenus::SubXxx`, see `tools/rampage_inventory.md`) and where Rampagio has it.','"Options" counts Rampage\'s static interactive rows. Statuses: Done (every row has an equivalent), Partial, Pending. Nothing is live-tested yet.','']
by=collections.defaultdict(list)
for s in sorted(set(area)): by[area[s]].append(s)
tot=collections.Counter()
for a in sorted(by):
    out+=['## '+a,'','| Rampage submenu | Options | Status | Rampagio |','|---|---|---|---|']
    for s in by[a]:
        st,where=STATUS.get(s,('Pending',''))
        tot[st]+=1
        out.append('| %s | %d | %s | %s |'%(s,cnt[s],st,where))
    out.append('')
out[5:5]=['Submenus: %d done, %d partial, %d pending.'%(tot['Done'],tot['Partial'],tot['Pending']),'']
open('docs/PORTING.md','w',encoding='utf-8',newline='\r\n').write('\n'.join(out)+'\n')
print(tot)
