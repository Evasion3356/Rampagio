"""Static native-hash deobfuscator for Rampage.asi (RDR2 trainer).

Scheme (Rampage.asi, Jan 2026 build):
  - One 64-bit key is written at init (sub_1801E0870):
        g_key (0x180426BA8) = 0x27AC828DBF073D57
  - Each native call site stores an encrypted constant C, looks it up in a
    per-site cache (sub_180220FE0 / sub_18021B6E0) and on a miss decrypts:
        k    = rol64(g_key, r)            ; r = per-site immediate (0..31, may be absent = 0)
        hash = ~rol64(rol64(k ^ C, 32), (k & 0x1F) + 1)
    then hands the hash to ScriptHookRDR2's nativeInit().
  - Some sites instead call the encrypt helper sub_18001B780(plainHash, r)
    at runtime with the PLAINTEXT hash in rcx; those are read directly.

Usage:  python rampage_deob.py <Rampage.asi> <natives-rdr.json> [out_prefix]
Writes  <out_prefix>.csv and <out_prefix>_ida.py (IDA: File > Script file).
"""
import sys, re, json, bisect, csv
import pefile, capstone

M = (1 << 64) - 1
IMM = re.compile(r'-?0x[0-9a-f]+$|\d+$')
RIP = re.compile(r'rip ([+-]) (0x[0-9a-f]+|\d+)')


def rol(x, n):
    n &= 63
    return ((x << n) | (x >> (64 - n))) & M if n else x


def decrypt(key, c, r):
    k = rol(key, r)
    return (~rol(rol(k ^ c, 32), (k & 0x1F) + 1)) & M


def encrypt(key, h, r):  # inverse of decrypt, same as sub_18001B780
    k = rol(key, r)
    return (rol(rol(~h & M, 64 - ((k & 0x1F) + 1)), 32) ^ k) & M


def main():
    asi, dbpath = sys.argv[1], sys.argv[2]
    out = sys.argv[3] if len(sys.argv) > 3 else 'rampage_natives'
    pe = pefile.PE(asi)
    base = pe.OPTIONAL_HEADER.ImageBase
    img = pe.get_memory_mapped_image()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

    funcs = sorted(set((e.struct.BeginAddress, e.struct.EndAddress) for e in pe.DIRECTORY_ENTRY_EXCEPTION))
    ins = []
    for s, e in funcs:
        for i in md.disasm(img[s:e], base + s):
            ins.append((i.address, i.size, i.mnemonic, i.op_str))
    addrs = [x[0] for x in ins]
    fstarts = [base + s for s, _ in funcs]

    def tgt(a, sz, op):
        m = RIP.search(op)
        if not m:
            return None
        d = int(m.group(2), 0)
        return a + sz + (d if m.group(1) == '+' else -d)

    def func_of(a):
        return fstarts[bisect.bisect_right(fstarts, a) - 1]

    # Key global = the rip-relative qword most often loaded right before a 'rol reg, imm'.
    votes = {}
    for k in range(len(ins) - 1):
        a, sz, m, o = ins[k]
        if m == 'mov' and 'rip' in o and not o.startswith('qword') and ins[k + 1][2] == 'rol':
            t = tgt(a, sz, o)
            votes[t] = votes.get(t, 0) + 1
    keyva = max(votes, key=votes.get)
    key = None
    for k, (a, sz, m, o) in enumerate(ins):
        if m == 'mov' and o.startswith('qword ptr [rip') and tgt(a, sz, o) == keyva:
            src = o.split(', ')[1]
            p = ins[k - 1] if ins[k - 1][2] == 'movabs' else ins[k - 2]
            if p[2] == 'movabs' and p[3].startswith(src + ', '):
                key = int(p[3].split(', ')[1], 0)
    print(f'key global {keyva:#x} = {key:#x}')

    # Encrypt helper(s): small leaf functions (no .pdata entry) embedding the key as an
    # immediate within their first few instructions; identified by direct-call target.
    helpers = set()
    for t in {int(o, 0) for _, _, m, o in ins if m == 'call' and IMM.match(o)}:
        if t - base >= len(img):
            continue
        for i in md.disasm(img[t - base:t - base + 64], t):
            if i.mnemonic == 'movabs' and i.op_str.endswith(f', {key:#x}'):
                helpers.add(t)
            if i.mnemonic == 'ret':
                break

    nativeInit = None
    for e in pe.DIRECTORY_ENTRY_IMPORT:
        for imp in e.imports:
            if imp.name and b'nativeInit' in imp.name:
                nativeInit = imp.address

    db = json.load(open(dbpath, encoding='utf8'))
    names = {int(h, 16): f"{ns}::{v['name']}" for ns, d in db.items() for h, v in d.items()}

    def flow_consts(k):
        # Stack slot holding C, read right after the key load.
        slot = None
        for j in range(k + 1, k + 12):
            mm = re.search(r'qword ptr (\[r[bs]p [+-] 0x[0-9a-f]+\]|\[r[bs]p\])', ins[j][3])
            if ins[j][2] in ('xor', 'mov') and mm and not ins[j][3].startswith('qword'):
                slot = mm.group(1)
                break
        if slot is None:
            return []
        lo = bisect.bisect_left(addrs, func_of(ins[k][0]))
        src, found = None, []
        for j in range(k - 1, max(0, k - 8000), -1):
            a, sz, m, o = ins[j]
            if src is None:
                if m == 'mov' and o.startswith('qword ptr ' + slot + ', '):
                    src = o.split(', ')[1]
                    if IMM.match(src):
                        return [int(src, 0) & M]
            elif m in ('movabs', 'mov') and o.startswith(src + ', '):
                v = o.split(', ')[1]
                if IMM.match(v):
                    found.append((int(v, 0) & M, j >= lo))
                elif j >= lo:
                    src = v
        # Constant hoisted into a callee-saved register: try in-function defs first.
        return [c for c, _ in sorted(found, key=lambda x: not x[1])][:64]

    rows = []
    for k, (a, sz, m, o) in enumerate(ins):
        if m != 'mov' or o.startswith('qword') or tgt(a, sz, o) != keyva:
            continue
        reg = o.split(',')[0]
        r = 0
        for j in range(k + 1, k + 8):
            if ins[j][2] == 'rol' and ins[j][3].startswith(reg + ', ') and not ins[j][3].endswith('cl'):
                r = int(ins[j][3].split(', ')[1], 0)
                break
            if ins[j][2] == 'xor' and ins[j][3].endswith(', ' + reg):
                break
        cands = [('flow', decrypt(key, c, r)) for c in flow_consts(k)]
        for j in range(k - 1, k - 40, -1):
            if ins[j][2] == 'call' and IMM.match(ins[j][3]) and int(ins[j][3], 0) in helpers:
                for t in range(j - 1, j - 6, -1):
                    if ins[t][2] == 'movabs' and ins[t][3].startswith('rcx, '):
                        cands.append(('helper', int(ins[t][3].split(', ')[1], 0)))
                        break
                break
            if ins[j][2] in ('ret', 'int3'):
                break
        for j in range(k - 1, k - 40, -1):
            if ins[j][2] == 'movabs':
                cands.append(('near', decrypt(key, int(ins[j][3].split(', ')[1], 0), r)))
                break
            if ins[j][2] in ('ret', 'int3'):
                break
        how, h = next((c for c in cands if c[1] in names), cands[0] if cands else (None, None))
        call = None
        for j in range(k + 1, min(len(ins), k + 40)):
            if ins[j][2] == 'call' and tgt(ins[j][0], ins[j][1], ins[j][3]) == nativeInit:
                call = ins[j][0]
                break
        rows.append(dict(site=a, call=call, func=func_of(a), rot=r, how=how, hash=h, name=names.get(h, '')))

    named = sum(1 for r in rows if r['name'])
    unres = sum(1 for r in rows if r['hash'] is None)
    print(f'{len(rows)} sites, {named} named, {len(rows) - named - unres} decoded-but-unknown, {unres} unresolved')

    with open(out + '.csv', 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['site', 'nativeInit_call', 'function', 'rot', 'how', 'hash', 'native'])
        for r in rows:
            w.writerow([f"{r['site']:#x}", f"{r['call']:#x}" if r['call'] else '', f"{r['func']:#x}", r['rot'],
                        r['how'] or '', f"{r['hash']:#018x}" if r['hash'] is not None else '', r['name']])

    with open(out + '_ida.py', 'w') as f:
        f.write('import idc\n')
        f.write(f'idc.set_name({keyva:#x}, "g_NativeHashKey", idc.SN_NOWARN)\n')
        for n, h in enumerate(sorted(helpers)):
            f.write(f'idc.set_name({h:#x}, "EncryptNativeHash{"_%d" % n if n else ""}", idc.SN_NOWARN)\n')
        f.write('S = [\n')
        for r in rows:
            if r['hash'] is None:
                f.write(f"  ({r['site']:#x}, 0, 0, 'UNRESOLVED native hash'),\n")
                continue
            label = r['name'] or f"UNKNOWN_{r['hash']:016X}"
            f.write(f"  ({r['site']:#x}, {r['call'] or 0:#x}, {r['hash']:#x}, {label!r}),\n")
        f.write(']\n')
        f.write(IDA_RUNTIME)


# Appended to the generated IDA script after the S table.
IDA_RUNTIME = r'''
import ida_funcs, ida_hexrays

for site, call, h, name in S:
    c = "%s  0x%016X" % (name, h)
    idc.set_cmt(site, c, 0)
    if call:
        idc.set_cmt(call, "nativeInit(" + c + ")", 0)
print("Rampage deob: annotated %d disassembly sites" % len(S))

# Pseudocode: Hex-Rays keeps its own per-function comment store, keyed by
# (statement ea, position). Attach each comment to the statement the
# nativeInit call (or, failing that, the decrypt site) belongs to.
ITPS = (ida_hexrays.ITP_SEMI, ida_hexrays.ITP_CURLY1, ida_hexrays.ITP_COLON,
        ida_hexrays.ITP_BRACE2, ida_hexrays.ITP_ELSE, ida_hexrays.ITP_DO,
        ida_hexrays.ITP_CASE, ida_hexrays.ITP_BLOCK1)

def stmt_ea(eamap, ea):
    if ea in eamap and len(eamap[ea]):
        return eamap[ea][0].ea
    return None

def orphaned(cfunc):
    cfunc.__str__()                       # re-print so orphan state is current
    return cfunc.has_orphan_cmts()

def annotate_func(fea, items):
    try:
        cfunc = ida_hexrays.decompile(fea)
    except ida_hexrays.DecompilationFailure:
        return 0, len(items)
    if not cfunc:
        return 0, len(items)
    eamap = cfunc.get_eamap()
    placed, todo = {}, []
    for site, call, c in items:
        ea = (call and stmt_ea(eamap, call)) or stmt_ea(eamap, site)
        if ea is None:
            continue
        # several natives can collapse into one statement (nested calls)
        placed.setdefault(ea, []).append(c)
    for ea, cs in placed.items():
        todo.append((ea, " | ".join(dict.fromkeys(cs))))
    # fast path: everything as end-of-statement comments, one re-print
    for ea, c in todo:
        tl = ida_hexrays.treeloc_t(); tl.ea = ea; tl.itp = ida_hexrays.ITP_SEMI
        cfunc.set_user_cmt(tl, c)
    if orphaned(cfunc):
        cfunc.del_orphan_cmts()
        # retry the ones that got dropped at other positions
        for ea, c in todo:
            tl = ida_hexrays.treeloc_t(); tl.ea = ea; tl.itp = ida_hexrays.ITP_SEMI
            if cfunc.get_user_cmt(tl, ida_hexrays.RETRIEVE_ALWAYS):
                continue
            for itp in ITPS[1:]:
                tl.itp = itp
                cfunc.set_user_cmt(tl, c)
                if not orphaned(cfunc):
                    break
                cfunc.del_orphan_cmts()
    cfunc.save_user_cmts()
    ida_hexrays.mark_cfunc_dirty(fea)
    n = sum(1 for ea, c in todo if any(
        cfunc.get_user_cmt(_tl(ea, itp), ida_hexrays.RETRIEVE_ALWAYS) for itp in ITPS))
    return n, len(items) - n

def _tl(ea, itp):
    tl = ida_hexrays.treeloc_t(); tl.ea = ea; tl.itp = itp
    return tl

if ida_hexrays.init_hexrays_plugin():
    byfunc = {}
    for site, call, h, name in S:
        f = ida_funcs.get_func(site)
        if f:
            byfunc.setdefault(f.start_ea, []).append((site, call, "%s  0x%016X" % (name, h)))
    ok = miss = 0
    for i, (fea, items) in enumerate(sorted(byfunc.items())):
        a, b = annotate_func(fea, items)
        ok += a; miss += b
        if i % 50 == 0:
            print("Rampage deob: pseudocode %d/%d functions" % (i, len(byfunc)))
    print("Rampage deob: %d pseudocode comments placed, %d sites not mappable, %d functions"
          % (ok, miss, len(byfunc)))
else:
    print("Rampage deob: Hex-Rays not available, skipped pseudocode comments")
'''


if __name__ == '__main__':
    main()
