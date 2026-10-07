"""IDA (headless) half of the Rampage feature inventory.

Run against an IDA database of Rampage.asi that already has the
rampage_deob.py annotations applied:

    idat.exe -A -S"rampage_inventory_ida.py <out.json>" -L"log.txt" Rampage.asi.i64

Rampage builds each submenu in a function `Submenus::SubXxx`. Those names
survive only inside the RTTI names of the std::function lambdas each
builder creates, e.g.
    ??_7?$_Func_impl_no_alloc@V_lambda_3_@?1??SubRecoveryBounty@Submenus@@YAXXZ@...
so a builder is found as the function that references its lambdas'
vftables, directly or through a tiny constructor helper.

Each builder is walked instruction by instruction in address order. Every
call to one of the menu API functions (MENU_API below) closes a row; the
strings, handlers, globals and child builders referenced since the
previous API call belong to that row. The last string is the label and
the one before it is usually the description.

Writes JSON consumed by rampage_inventory.py. Nothing here is copied from
Rampage; the output is derived from its binary, so keep it out of git.
"""
import json
import re

import ida_auto
import ida_bytes
import ida_funcs
import ida_hexrays
import ida_lines
import ida_nalt
import ida_pro
import ida_segment
import idautils
import idc

# Menu API functions, named by what the rows they add look like in game.
MENU_API = {
    0x1801ED2B0: "action",
    0x1801ED140: "submenu",
    0x1801EE1E0: "toggle",          # bool global + onChange lambda
    0x1801ECEE0: "title",
    0x1801EE020: "toggle_plain",    # bool global, no callback
    0x1801EDB10: "section",
    0x1801EDEE0: "submenu_idx",     # opens a submenu by index
    0x1801EF200: "choice",          # list of named values
    0x1801F0970: "location",        # teleport target with coords
    0x1801EED90: "slider_float",
    0x1801EEEC0: "toggle_value",    # toggle with a value
    0x1801ED6A0: "action_icon",     # action with a texture icon
    0x1801EDD30: "list_item_icon",
    0x1801ED990: "value_hex",
    0x1801EE530: "list_item",
    0x1801EF3E0: "number_int",
    0x1801F01B0: "color",
    0x1801EE890: "slider_int",
    0x1801F0330: "posse_item",
    0x1801EF6E0: "component_pick",
    0x1801ED430: "value_pick",
    0x1801EE3A0: "toggle_cb",
}

LAMBDA_RX = re.compile(
    r"\?\?_7\?\$_Func_impl_no_alloc@V(_lambda_\w+?_)@\?\w+\?\?(Sub\w+)@Submenus@@YAXXZ@(.*)@std@@6B@")


def is_string(ea):
    seg = ida_segment.getseg(ea)
    if not seg or ida_segment.get_segm_name(seg) != ".rdata":
        return None
    if not ida_bytes.is_strlit(ida_bytes.get_flags(ea)):
        return None
    raw = ida_bytes.get_strlit_contents(ea, -1, ida_nalt.get_str_type(ea))
    return raw.decode("utf-8", "replace") if raw else None


def is_data(ea):
    seg = ida_segment.getseg(ea)
    return bool(seg) and ida_segment.get_segm_name(seg) in (".data", ".bss")


def func_start(ea):
    f = ida_funcs.get_func(ea)
    return f.start_ea if f else None


def main(out_path):
    ida_auto.auto_wait()

    # Lambda vftables. Slot 2 of an MSVC _Func_impl vftable is _Do_call.
    vft = {}
    for ea, name in idautils.Names():
        m = LAMBDA_RX.match(name)
        if m:
            vft[ea] = dict(lam=m.group(1), sub=m.group(2), sig=m.group(3),
                           docall=ida_bytes.get_qword(ea + 16))

    # Who references each vftable: tiny constructor helpers, the lambda's
    # own methods, and (sometimes) the builder itself.
    helper = {}                       # ctor helper -> vftable
    candidates = {}                   # sub name -> set of functions
    for v, info in vft.items():
        for x in idautils.XrefsTo(v):
            f = ida_funcs.get_func(x.frm)
            if not f:
                continue
            if f.end_ea - f.start_ea < 0x40:
                helper[f.start_ea] = v
                for c in idautils.CodeRefsTo(f.start_ea, 0):
                    s = func_start(c)
                    if s is not None:
                        candidates.setdefault(info["sub"], set()).add(s)
            elif f.end_ea - f.start_ea > 0x200:
                candidates.setdefault(info["sub"], set()).add(f.start_ea)
    # A function shared by many submenus (e.g. the option search index)
    # isn't a builder.
    seen = {}
    for fs in candidates.values():
        for f in fs:
            seen[f] = seen.get(f, 0) + 1
    builders = {}
    for sub, fs in candidates.items():
        own = sorted(f for f in fs if seen[f] == 1)
        if own:
            # The biggest one is the builder; any others are its pieces.
            builders[max(own, key=lambda f: ida_funcs.get_func(f).size())] = sub

    rows = []
    for b, sub in builders.items():
        idc.set_name(b, "Submenus__" + sub, idc.SN_NOWARN | idc.SN_NOCHECK)
        cur = dict(strs=[], handlers=[], globals=[])
        order = 0
        for h in sorted(idautils.FuncItems(b)):
            is_call = idc.print_insn_mnem(h) == "call"
            for x in idautils.XrefsFrom(h, 0):
                t = x.to
                if is_call:
                    if t in MENU_API:
                        rows.append(dict(sub=sub, order=order, api=MENU_API[t], site=h, **cur))
                        order += 1
                        cur = dict(strs=[], handlers=[], globals=[])
                    elif t in helper:
                        cur["handlers"].append(["lambda", helper[t]])
                    continue
                if t in vft:
                    cur["handlers"].append(["lambda", t])
                    continue
                if func_start(t) == t and t != b:
                    cur["handlers"].append(["fn", t])
                    continue
                s = is_string(t)
                if s is not None:
                    cur["strs"].append(s)
                elif is_data(t) and t not in cur["globals"]:
                    cur["globals"].append(t)

    # Hex-Rays shows labels the instruction walk can't see: literals copied
    # with xmm constants, and short strings packed into integer
    # immediates. Attach the pseudocode lines leading up to each API call
    # so rampage_inventory.py can read the label from them.
    by_site = {r["site"]: r for r in rows}
    for b in builders:
        try:
            cfunc = ida_hexrays.decompile(b)
        except ida_hexrays.DecompilationFailure:
            continue
        lines = [ida_lines.tag_remove(l.line) for l in cfunc.get_pseudocode()]
        call_lines = []
        for item in cfunc.treeitems:
            e = item.cexpr if item.is_expr() else None
            if e is not None and e.op == ida_hexrays.cot_call and e.x.op == ida_hexrays.cot_obj                     and e.x.obj_ea in MENU_API and e.ea in by_site:
                coords = cfunc.find_item_coords(item)   # (ok, x, y) or (x, y)
                y = coords[-1] if isinstance(coords, tuple) else None
                if y is not None and y >= 0:
                    call_lines.append((y, e.ea))
        prev = 0
        for y, ea in sorted(call_lines):
            by_site[ea]["pc"] = lines[prev:y + 1]
            prev = y + 1

    readers = {}
    for g in {g for r in rows for g in r["globals"]}:
        readers[g] = sorted({s for s in (func_start(x.frm) for x in idautils.XrefsTo(g)) if s is not None})

    # Function chunks, so native call sites (rampage_natives.csv) can be
    # attributed to IDA's functions rather than .pdata entries.
    chunks = []
    for f in idautils.Functions():
        for start, end in idautils.Chunks(f):
            chunks.append([start, end, f])
    chunks.sort()

    callgraph = {}
    for f in idautils.Functions():
        callees = set()
        for h in idautils.FuncItems(f):
            for x in idautils.XrefsFrom(h, 0):
                if x.to != f and func_start(x.to) == x.to:
                    callees.add(x.to)
        callgraph[f] = sorted(callees)

    with open(out_path, "w") as fp:
        json.dump(dict(vft=vft, builders=builders, rows=rows, readers=readers,
                       callgraph=callgraph, chunks=chunks), fp)


if __name__ == "__main__":
    args = idc.ARGV[1:]
    main(args[0] if args else "rampage_inventory.json")
    ida_pro.qexit(0)
