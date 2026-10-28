# Map-marker sprites (FFDec PNG export of map.swf) -> tintable 128x128 white+alpha icons as uncompressed BGRA .dds.
# The marker's light fill becomes the icon, its black outline becomes transparent.
import sys, os, glob, struct
from PIL import Image
src, out = sys.argv[1], sys.argv[2]
ICONS = {  # mod file name: map.swf character id (the full-size inner sprite / bitmap)
    'home': 337, 'dovahkiin': 272, 'combat': 213, 'magic': 195, 'schools': 302, 'enchant': 207,
    'progress': 317, 'world': 275, 'quests': 244, 'target': 217, 'console': 293,
    'items': 192, 'actors': 296, 'service': 153,
    'mine': 254, 'farm': 284, 'giant': 278, 'stable': 183, 'lighthouse': 263, 'teleport': 299, 'fort': 281,
    'town': 177, 'camp': 343, 'cave': 340, 'ship': 198, 'stronghold': 232, 'pass': 229, 'landmark': 266,
    'altar': 346, 'docks': 305, 'mushroom': 314, 'ruin': 238, 'mill': 174, 'horse': 171, 'wolf': 189,
}
SIZE = 128
def dds(path, im):
    w, h = im.size
    hdr = struct.pack('<4s7I44x', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x8, h, w, w * 4, 0, 1)
    pf = struct.pack('<2I4s5I', 32, 0x41, b'\0\0\0\0', 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    caps = struct.pack('<5I', 0x1000, 0, 0, 0, 0)
    r, g, b, a = im.split()
    open(path, 'wb').write(hdr + pf + caps + Image.merge('RGBA', (b, g, r, a)).tobytes())
os.makedirs(out, exist_ok=True)
for name, cid in ICONS.items():
    d = glob.glob(os.path.join(src, f'DefineSprite_{cid}*'))[0]
    im = Image.open(os.path.join(d, '1.png')).convert('RGBA')
    im = im.crop(im.getbbox())
    px = im.load(); m = Image.new('L', im.size)
    mp = m.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            lum = max(r, g, b)
            t = min(1.0, max(0.0, (lum - 50) / 110.0))
            mp[x, y] = int(a * t)
    m = m.crop(m.getbbox())
    s = (SIZE - 8) / max(m.size)
    m = m.resize((max(1, round(m.width * s)), max(1, round(m.height * s))), Image.LANCZOS)
    canvas = Image.new('L', (SIZE, SIZE)); canvas.paste(m, ((SIZE - m.width) // 2, (SIZE - m.height) // 2))
    white = Image.new('L', (SIZE, SIZE), 255)
    dds(os.path.join(out, name + '.dds'), Image.merge('RGBA', (white, white, white, canvas)))
    canvas.save(os.path.join(out, '..', 'preview_' + name + '.png'))
print(len(ICONS), 'icons')
