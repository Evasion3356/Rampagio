r"""Headless IDA: dumps the pseudocode of every Rampage menu row's handler
(plus small callees) into <workdir>/h/<Submenu>.c, grouped by row.

    idat.exe -A -S"tools\handlers\dump_handlers_ida.py <workdir> tools\rampage_inventory.csv" -L"log.txt" <copy of Rampage.asi.i64>
    python tools/handlers/shrink.py <workdir>/h <workdir>/hs external/ScriptHookSDK/inc/natives.h
    python tools/handlers/compact.py <workdir>/hs <workdir>/hc
    python tools/handlers/view.py <workdir>/hc SubWeapons SubWeaponsManage

<workdir> must already hold inv.json from rampage_inventory_ida.py. shrink
strips boilerplate and renames natives to the SDK header's names by hash;
compact reduces each handler to native calls with literal args; view
filters Rampage's settings save/load noise. Outputs are derived from
Rampage's binary: keep them out of git (use the session scratchpad).
"""
import ida_auto, ida_hexrays, ida_pro, idc, json, os, collections, re
ida_auto.auto_wait()
S=idc.ARGV[1]  # work dir holding inv.json; writes S/h/<Submenu>.c
inv=json.load(open(S+"/inv.json"))
import csv
rows=list(csv.DictReader(open(idc.ARGV[2],encoding="utf-8")))
cg={int(k):v for k,v in inv["callgraph"].items()}
callers=collections.Counter(c for cs in cg.values() for c in cs)
os.makedirs(S+"/h",exist_ok=True)
cache={}
def dec(f):
    if f not in cache:
        try:
            t=str(ida_hexrays.decompile(f))
            # strip the native-hash decryption boilerplate
            t=re.sub(r"\n\s*v\d+ = \*\(_QWORD \*\)\(\*\(_QWORD \*\)sub_180220FE0\([^\n]*\n\s*if \( !v\d+ \)\n\s*\{\n(?:[^\n]*\n){3}\s*\}","",t)
            t=re.sub(r"\n\s*\+\+dword_180426CC8;","",t)
            t=re.sub(r"\n\s*v\d+ = 0x[0-9A-F]+u?LL;","",t)
            cache[f]=t
        except Exception as e: cache[f]="FAIL %x"%f
    return cache[f]
bysub=collections.defaultdict(list)
for r in rows: bysub[r["submenu"]].append(r)
for sub,rs in bysub.items():
    out=[]; done=set()
    for r in rs:
        out.append("##### [%s] %s -- %s"%(r["kind"],r["label"],r["description"]))
        for h in r["handlers"].split():
            f=int(h,16)
            todo=[f]+[c for c in cg.get(f,[]) if callers[c]<=8]
            for g in todo:
                if g in done: out.append("  (see %x)"%g); continue
                done.add(g); out.append("// ---- %x"%g); out.append(dec(g))
    open(S+"/h/%s.c"%sub,"w",encoding="utf-8").write("\n".join(out))
ida_pro.qexit(0)
