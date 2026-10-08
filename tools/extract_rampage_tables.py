r"""Builds src/data/ files from tables Rampage keeps in its .rdata.

    python tools/extract_rampage_tables.py <Rampage.asi> src/data

Each table is found by the pointer to a known first name, then read
until an entry no longer points at a string with the table's prefix.
Needs pefile.

- RampageMapDiscoveries.inc: Recovery > Unlocks > Map Discoverables, an
  array of name pointers (Rampage joaat()s each at runtime).
  { "<name>", "<label>" }, the same rows as MapDiscoveries.inc.
- LawDispatchRegions.inc: Spawner > Law Dispatch Spawner, entries of
  { name pointer, law region hash, state hash } (the two hashes go to
  _SET_LAW_REGION before the response is dispatched; 0 means none).
  { "<name>", 0x<region>, 0x<state> }
- LegendaryAnimals.inc: the legendary rows of Spawner > Ped Spawner >
  Animals and Fishes. Rampage's animal and fish tables are entries of
  { label, model, image, outfit preset, hash }; the legendaries are the
  run starting at Bull Gator (animals) and at the first "Legendary " label
  (fish). Spawning one equips its outfit preset.
  { "<Animal|Fish>", "<label>", "<model>", <preset> }
- OverlayTextures.inc: Player > Wardrobe > Overlay Textures' TX Id and
  Palette Id tables. Rampage's static initializers build them on the
  stack (immediate stores) and copy them into vectors, one per overlay
  type in the order of OVERLAYS, then 25 palettes. The initializers are
  followed just far enough (capstone) to read those stack arrays: the
  overlay one is the function that references "spots" and calls one
  target 12 times (memcpy) and another 6 times (vector from a range);
  the palette one is the next entry in the CRT initializer table.
  { "<overlay>", 0x<albedo>, 0x<normal>, 0x<material> } and
  { "palette", 0x<hash>, 0, 0 }

The names and hashes are carried over on purpose (see CLAUDE.md);
nothing reads Rampage at runtime. Needs pefile and capstone.
"""
import os
import struct
import sys

import capstone
import pefile
from capstone import x86

# Rampage's overlay order (its Overlay choice), which its initializer follows.
OVERLAYS = ("eyebrows", "scars", "eyeliners", "lipsticks", "acne", "shadows", "beardstabble",
            "paintedmasks", "ageing", "blush", "complex", "disc", "foundation", "freckles",
            "grime", "hair", "moles", "spots")


def tidy(name, prefix):
    words = name[len(prefix):].split("_")
    return " ".join(w.capitalize() if not w.isdigit() else w for w in words if w)


class Image:
    def __init__(self, path):
        self.pe = pefile.PE(path, fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.data = self.pe.__data__

    def string(self, va, prefix):
        if not self.base <= va < self.base + self.pe.OPTIONAL_HEADER.SizeOfImage:
            return None
        try:
            raw = self.pe.get_data(va - self.base, 128).split(b"\0")[0]
        except Exception:
            return None
        ok = raw and raw.startswith(prefix) and all(32 <= c < 127 for c in raw)
        return raw.decode("ascii") if ok else None

    def entry(self, off, count):
        """The strings at `count` pointers from file offset `off`, or None."""
        ptrs = struct.unpack_from("<%dQ" % count, self.data, off)
        out = [self.string(p, b"") for p in ptrs]
        return out if all(out) else None

    def find_entry(self, label, model):
        """File offset of a { label, model, ... } entry."""
        for off in range(0, len(self.data) - 16, 8):
            e = self.entry(off, 2) if self.data[off + 7] == 0 and self.data[off + 5] == 0 else None
            if e == [label, model]:
                return off
        raise SystemExit("entry %s not found" % label)

    def table(self, first, prefix, stride):
        """Offsets of the entries of the table whose first entry names `first`."""
        off = self.data.find(first + b"\0")
        while off > 0 and self.data[off - 1] != 0:
            off = self.data.find(first + b"\0", off + 1)
        if off < 0:
            raise SystemExit("%s not found" % first)
        va = self.base + self.pe.get_rva_from_offset(off)
        entry = self.data.find(struct.pack("<Q", va))
        if entry < 0:
            raise SystemExit("table of %s not found" % first)
        while self.string(struct.unpack_from("<Q", self.data, entry)[0], prefix):
            yield entry, self.string(struct.unpack_from("<Q", self.data, entry)[0], prefix)
            entry += stride


class StackEmulator:
    """Follows one function's straight-line code, recording immediate
    stores to the stack, and returns the stack bytes each copy reads:
    memcpy(rcx, rdx = &stack, r8 = size), and a vector built from a
    { begin, end } pair of stack pointers at rdx."""

    WIDE = {}
    for _a in "abcd":
        WIDE.update({"e%sx" % _a: "r%sx" % _a, "%sl" % _a: "r%sx" % _a})
    for _a in ("si", "di", "bp", "sp"):
        WIDE["e" + _a] = "r" + _a
    for _k in range(8, 16):
        WIDE.update({"r%dd" % _k: "r%d" % _k, "r%db" % _k: "r%d" % _k})

    def __init__(self, img, memcpy, from_range):
        self.img, self.memcpy, self.from_range = img, memcpy, from_range
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True

    def run(self, start):
        stack, ptrs, regs, xmm, out = {}, {}, {}, {}, []
        reg = lambda i, r: self.WIDE.get(i.reg_name(r), i.reg_name(r))
        movs = ("movaps", "movups", "movdqa", "movdqu")

        def slot(op):
            if op.type == x86.X86_OP_MEM and op.mem.index == 0 and op.mem.base in (x86.X86_REG_RSP, x86.X86_REG_RBP):
                return (op.mem.base, op.mem.disp)

        def put(key, value, size):
            for b in range(size):
                stack[(key[0], key[1] + b)] = (value >> (8 * b)) & 0xFF

        def read(base, lo, hi):
            return bytes(stack.get((base, x), 0) for x in range(lo, hi))

        for i in self.md.disasm(self.img.pe.get_data(start - self.img.base, 0x8000), start):
            ops, m = i.operands, i.mnemonic
            if m == "ret":
                break
            dst = slot(ops[0]) if ops else None
            if m == "mov" and dst is not None:
                if ops[1].type == x86.X86_OP_IMM:
                    put(dst, ops[1].imm, ops[0].size)
                elif ops[1].type == x86.X86_OP_REG:
                    v = regs.get(reg(i, ops[1].reg))
                    if isinstance(v, tuple):
                        ptrs[dst] = v
                    elif v is not None:
                        put(dst, v, ops[0].size)
            elif m in movs and dst is not None and ops[1].type == x86.X86_OP_REG:
                if ops[1].reg in xmm:
                    put(dst, int.from_bytes(xmm[ops[1].reg], "little"), 16)
            elif m in movs and ops[0].type == x86.X86_OP_REG and ops[1].type == x86.X86_OP_MEM \
                    and ops[1].mem.base == x86.X86_REG_RIP:
                xmm[ops[0].reg] = self.img.pe.get_data(i.address + i.size + ops[1].mem.disp - self.img.base, 16)
            elif m in ("xorps", "pxor") and ops[0].reg == ops[1].reg:
                xmm[ops[0].reg] = bytes(16)
            elif m == "xor" and ops[0].type == ops[1].type == x86.X86_OP_REG and ops[0].reg == ops[1].reg:
                regs[reg(i, ops[0].reg)] = 0
            elif m == "mov" and ops[0].type == x86.X86_OP_REG and ops[1].type == x86.X86_OP_IMM:
                regs[reg(i, ops[0].reg)] = ops[1].imm
            elif m == "mov" and ops[0].type == ops[1].type == x86.X86_OP_REG and ops[0].size == 8:
                if reg(i, ops[1].reg) in regs:
                    regs[reg(i, ops[0].reg)] = regs[reg(i, ops[1].reg)]
                else:
                    regs.pop(reg(i, ops[0].reg), None)
            elif m == "lea" and ops[0].type == x86.X86_OP_REG and slot(ops[1]) is not None:
                regs[reg(i, ops[0].reg)] = slot(ops[1])
            elif m == "call":
                target = ops[0].imm if ops[0].type == x86.X86_OP_IMM else None
                src = regs.get("rdx")
                if target == self.memcpy and isinstance(src, tuple) and isinstance(regs.get("r8"), int):
                    out.append(read(src[0], src[1], src[1] + regs["r8"]))
                elif target == self.from_range and isinstance(src, tuple):
                    lo, hi = ptrs.get(src), ptrs.get((src[0], src[1] + 8))
                    if not lo or not hi or lo[0] != hi[0]:
                        raise SystemExit("unresolved range at %x" % i.address)
                    out.append(read(lo[0], lo[1], hi[1]))
                for r in ("rax", "rcx", "rdx", "r8", "r9", "r10", "r11"):
                    regs.pop(r, None)
            elif ops and ops[0].type == x86.X86_OP_REG and m not in ("cmp", "test", "push"):
                regs.pop(reg(i, ops[0].reg), None)
        return out


def overlay_tables(img):
    """[(overlay, [(albedo, normal, material)...])...], [palette hashes]."""
    text = next(s for s in img.pe.sections if s.Name.rstrip(b"\0") == b".text")
    spots_va = img.base + img.pe.get_rva_from_offset(img.data.find(b"\0spots\0") + 1)
    code = img.data[text.PointerToRawData:text.PointerToRawData + text.SizeOfRawData]
    text_va = img.base + text.VirtualAddress
    refs = [text_va + p - 3 for p in range(3, len(code) - 4)
            if code[p - 3:p - 1] in (b"\x48\x8d", b"\x4c\x8d")
            and struct.unpack_from("<i", code, p)[0] == spots_va - (text_va + p + 4)]
    img.pe.parse_data_directories([pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXCEPTION"]])
    starts = sorted({img.base + e.struct.BeginAddress for r in refs for e in img.pe.DIRECTORY_ENTRY_EXCEPTION
                     if e.struct.BeginAddress <= r - img.base < e.struct.EndAddress})
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    for start in starts:
        calls = {}
        for i in md.disasm(img.pe.get_data(start - img.base, 0x8000), start):
            if i.mnemonic == "ret":
                break
            if i.mnemonic == "call" and i.op_str.startswith("0x"):
                calls[int(i.op_str, 16)] = calls.get(int(i.op_str, 16), 0) + 1
        # The vector allocator is also called 12 times: keep the pair whose
        # copies give one table per overlay.
        for memcpy in (t for t, n in calls.items() if n == 12):
            for from_range in (t for t, n in calls.items() if n == 6):
                emu = StackEmulator(img, memcpy, from_range)
                blocks = emu.run(start)
                if len(blocks) == len(OVERLAYS) and all(b and len(b) % 16 == 0 for b in blocks):
                    break
            else:
                continue
            break
        else:
            continue
        break
    else:
        raise SystemExit("overlay initializer not found")
    overlays = []
    for name, block in zip(OVERLAYS, blocks):
        words = struct.unpack("<%dI" % (len(block) // 4), block)
        overlays.append((name, [words[k:k + 3] for k in range(0, len(words), 4)]))
    slot = img.data.find(struct.pack("<Q", start))
    palette = emu.run(struct.unpack_from("<Q", img.data, slot + 8)[0])
    if len(palette) != 1 or len(palette[0]) != 100:
        raise SystemExit("palette initializer not recognised")
    return overlays, struct.unpack("<25I", palette[0])


def write(path, note, lines):
    with open(path, "w", newline="\r\n", encoding="utf-8") as f:
        f.write("// Generated by tools/extract_rampage_tables.py from Rampage's\n")
        f.write("// %s. Regenerate rather than edit.\n" % note)
        f.write("".join(l + "\n" for l in lines))
    print("%s: %d" % (os.path.basename(path), len(lines)))


def main(asi, dst):
    img = Image(asi)
    write(os.path.join(dst, "RampageMapDiscoveries.inc"), "Map Discoverables table. { name, label }",
          ['{ "%s", "%s" },' % (n, tidy(n, "MAP_"))
           for _, n in img.table(b"MAP_ANIMAL_FISH_BASS_LARGE_MOUTH", b"MAP_", 8)])
    rows = []
    for off, name in img.table(b"LAW_CUSTOM_MUD3B", b"LAW_", 16):
        region, state = struct.unpack_from("<II", img.data, off + 8)
        rows.append('{ "%s", 0x%08X, 0x%08X },' % (name, region, state))
    legendary = []
    for kind, label, model, is_legendary in (
            ("Animal", "Bull Gator", "a_c_alligator_02", lambda e: "legendary" in e[2]),
            ("Fish", "Legendary Bluegill", "A_C_FishBluegil_01_ms", lambda e: e[0].startswith("Legendary "))):
        off = img.find_entry(label, model)
        while True:
            e = img.entry(off, 3)
            if not e or not is_legendary(e):
                break
            preset = struct.unpack_from("<i", img.data, off + 24)[0]
            legendary.append('{ "%s", "%s", "%s", %d },' % (kind, e[0], e[1], preset))
            off += 32
    write(os.path.join(dst, "LegendaryAnimals.inc"),
          "legendary animal and fish tables. { kind, label, model, outfit preset }", legendary)
    overlays, palettes = overlay_tables(img)
    lines = ['{ "%s", 0x%08X, 0x%08X, 0x%08X },' % ((name,) + tuple(e)) for name, es in overlays for e in es]
    lines += ['{ "palette", 0x%08X, 0, 0 },' % p for p in palettes]
    write(os.path.join(dst, "OverlayTextures.inc"),
          "Overlay Textures TX Id and Palette Id tables. { overlay, albedo, normal, material }", lines)
    write(os.path.join(dst, "LawDispatchRegions.inc"),
          "Law Dispatch Spawner table. { response, law region, state }", rows)


if __name__ == "__main__":
    main(*sys.argv[1:3])
