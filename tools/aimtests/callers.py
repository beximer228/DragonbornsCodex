# Find every E8/E9 rel32 in SkyrimSE.exe .text that targets the given RVAs; print caller RVA and the containing function name.
import struct, sys, bisect
EXE = r"G:\Games\Neverland\Neverland\Stock Game\SkyrimSE.exe"
NAMES = sys.argv[1]
targets = {int(x, 16) for x in sys.argv[2:]}
data = open(EXE, "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
opt = struct.unpack_from("<H", data, pe + 20)[0]
sec = pe + 24 + opt
names = []
for line in open(NAMES, encoding="utf-8", errors="replace"):
    p = line.split(None, 1)
    if len(p) == 2:
        try: names.append((int(p[0], 16), p[1].strip()))
        except ValueError: pass
names.sort(); keys = [n[0] for n in names]
for i in range(nsec):
    name, vsize, va, rsize, raw = struct.unpack_from("<8sIIII", data, sec + 40 * i)
    if not name.startswith(b".text"): continue
    blob = data[raw:raw + rsize]
    for off in range(len(blob) - 5):
        op = blob[off]
        if op not in (0xE8, 0xE9): continue
        rva = va + off
        tgt = (rva + 5 + struct.unpack_from("<i", blob, off + 1)[0]) & 0xFFFFFFFF
        if tgt in targets:
            j = bisect.bisect_right(keys, rva) - 1
            fn = f"{names[j][1]}+{rva - names[j][0]:X}" if j >= 0 else "?"
            print(f"{'call' if op == 0xE8 else 'jmp '} {rva:X} -> {tgt:X}  in {fn}")
