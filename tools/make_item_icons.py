# SkyUI's item icons (interface/skyui/icons_item_psychosteve.swf, psychosteve) -> one tintable atlas, icons/item_atlas.dds.
# Input: an FFDec frame export of that swf (java -jar ffdec-cli.jar -zoom 4 -format frame:png -export frame <dir> <swf>)
# and its -swf2xml dump, which gives the frame labels. Cells of CELL px, COLS per row, in label (= frame) order;
# white + alpha (the white icon on black becomes the alpha). Prints the label list for ItemIcons.cpp.
import sys, os, re, struct
from PIL import Image


def dds(path, im):  # uncompressed BGRA, as make_icons.py
    w, h = im.size
    hdr = struct.pack('<4s7I44x', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x8, h, w, w * 4, 0, 1)
    pf = struct.pack('<2I4s5I', 32, 0x41, bytes(4), 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    caps = struct.pack('<5I', 0x1000, 0, 0, 0, 0)
    r, g, b, a = im.split()
    open(path, 'wb').write(hdr + pf + caps + Image.merge('RGBA', (b, g, r, a)).tobytes())


frames, xml, out = sys.argv[1], sys.argv[2], sys.argv[3]
CELL, COLS, PAD = 48, 12, 3
labels, frame = [], 1
for line in open(xml, encoding='utf-8'):
    if line.startswith('    <item type="ShowFrameTag"'):
        frame += 1
    elif line.startswith('    <item type="FrameLabelTag"'):
        labels.append((re.search(r'name="([^"]*)"', line).group(1), frame))
rows = (len(labels) + COLS - 1) // COLS
alpha = Image.new('L', (COLS * CELL, rows * CELL))
for i, (name, f) in enumerate(labels):
    m = Image.open(os.path.join(frames, f'{f}.png')).convert('L')
    m = m.crop(m.getbbox())
    s = (CELL - 2 * PAD) / max(m.size)
    m = m.resize((max(1, round(m.width * s)), max(1, round(m.height * s))), Image.LANCZOS)
    x, y = (i % COLS) * CELL, (i // COLS) * CELL
    alpha.paste(m, (x + (CELL - m.width) // 2, y + (CELL - m.height) // 2))
white = Image.new('L', alpha.size, 255)
dds(out, Image.merge('RGBA', (white, white, white, alpha)))
print(f'{len(labels)} icons, {COLS}x{rows} cells of {CELL}px ->', out)
print(', '.join(f'"{n}"' for n, _ in labels))
