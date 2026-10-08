r"""Checks src/ScriptBytecode.cpp's function walk against the decompiler, offline:

    python tools/check_script_functions.py [..\SciptsCompile\script_rel] [..\Scripts\1491.50\script_rel]

Walks every <name>.ysc.full the same way ScriptBytecode::ListFunctions does
(same operand lengths, same ENTER/LEAVE/SWITCH handling) and compares each
func_N's "// Position - 0x..." in the decompiled <name>.ysc.c with the
function the walk found at index N: the position must be its ENTER offset or
the end of the previous function. Keep the operand table here in step with
kOperandLengths there.

Result for 1491.50 (2026-10-08): 1,638 scripts, 0 mismatches.
"""
import glob, os, re, struct, sys

ENTER, LEAVE, SWITCH = 34, 80, 60
OPERANDS = {}
for op in (109, 92, 20, 128, 39, 108, 99, 23, 100, 75, 102, 103, 137, 84, 78, 31, 121, 94, 41):
    OPERANDS[op] = 1
for op in (111, 37, 59, 127, 24, 120, 140, 64, 2, 10, 88, 1, 68, 70, 58, 95, 135, 112, 74, 104, 139, 21, 114, 46, 117, 138, 35):
    OPERANDS[op] = 2
for op in (123, 3, 57, 62, 107, 40, 93, 133, 38, 33):
    OPERANDS[op] = 3
for op in (55, 134):
    OPERANDS[op] = 4
OPERANDS[LEAVE] = 2

def load_code(path):
    """The code pages of a decompressed PC .ysc (RSC7 header optional), in one piece."""
    data = open(path, "rb").read()
    base = 0x10 if data[:4] in (b"RSC7", b"RSC8") else 0
    u32 = lambda offset: struct.unpack_from("<I", data, base + offset)[0]
    blocks, length = u32(0x10) & 0xFFFFFF, u32(0x1C)
    code = bytearray()
    for page in range((length + 0x3FFF) >> 14):
        offset = (struct.unpack_from("<I", data, base + blocks + page * 8)[0] & 0xFFFFFF) + base
        size = 0x4000 if (page + 1) * 0x4000 < length else length % 0x4000
        code += data[offset:offset + size]
    return code

def functions(code):
    """(enter, start) per function, in func_N order."""
    found, pc, after_leave = [], 0, 0
    while pc < len(code):
        op = code[pc]
        if op == ENTER:
            found.append((pc, after_leave))
            pc += 5 + code[pc + 4]
            continue
        if op == LEAVE:
            after_leave = pc + 3
        if op == SWITCH:
            pc += 3 + 6 * (code[pc + 1] | code[pc + 2] << 8)
            continue
        pc += 1 + OPERANDS.get(op, 0)
    return found

def main(ysc_dir=os.path.join("..", "SciptsCompile", "script_rel"),
         decompiled_dir=os.path.join("..", "Scripts", "1491.50", "script_rel")):
    header = re.compile(r"\b(?:func_(\d+)|__EntryFunction__)\(.*?\) // Position - 0x([0-9A-F]+)")
    scripts = mismatches = 0
    for path in sorted(glob.glob(os.path.join(ysc_dir, "*.ysc.full"))):
        name = os.path.basename(path)[:-len(".ysc.full")]
        source = os.path.join(decompiled_dir, name + ".ysc.c")
        if not os.path.exists(source):
            continue
        scripts += 1
        found = functions(load_code(path))
        with open(source, encoding="utf-8", errors="replace") as f:
            text = f.read()
        for match in header.finditer(text):
            index = int(match.group(1) or 0)
            position = int(match.group(2), 16)
            if index >= len(found) or position not in found[index]:
                mismatches += 1
                if mismatches <= 10:
                    print(name, "func_%d" % index, hex(position), found[index] if index < len(found) else "missing")
    print(scripts, "scripts,", mismatches, "mismatches")
    return 1 if mismatches else 0

if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:3]))
