# Extracts files from a TES5/SSE BSA whose path contains any of the given substrings.
import struct, sys, os
src, out, pats = sys.argv[1], sys.argv[2], [p.lower() for p in sys.argv[3:]]
f = open(src, 'rb')
magic, ver, off, flags, nfold, nfile, lfold, lfile, ftypes = struct.unpack('<4sIIIIIIIH', f.read(34)); f.read(2)
fsize = 24 if ver == 105 else 16
counts = [struct.unpack_from('<I', f.read(fsize), 8)[0] for _ in range(nfold)]
recs = []
for cnt in counts:
    n = f.read(1)[0]; folder = f.read(n).rstrip(b'\0').decode('latin1')
    for _ in range(cnt):
        h, size, o = struct.unpack('<QII', f.read(16)); recs.append([folder, size, o])
names = f.read(lfile).split(b'\0')
for i, r in enumerate(recs):
    path = (r[0] + '/' + names[i].decode('latin1')).replace(chr(92), '/')
    if not any(p in path.lower() for p in pats): continue
    size, o = r[1], r[2]
    comp = bool(flags & 4) ^ bool(size & 0x40000000); size &= 0x3FFFFFFF
    f.seek(o); data = f.read(size)
    if flags & 0x100:
        n = data[0]; data = data[1 + n:]
    if comp:
        raw = struct.unpack_from('<I', data)[0]; data = data[4:]
        if ver == 105:
            import lz4.frame; data = lz4.frame.decompress(data)
        else:
            import zlib; data = zlib.decompress(data)
    dst = os.path.join(out, path); os.makedirs(os.path.dirname(dst), exist_ok=True)
    open(dst, 'wb').write(data); print(path, len(data))
