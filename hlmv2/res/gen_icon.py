# Purpose: generates every icon hlmv2 uses. All artwork is drawn here with
#          PIL - nothing is copied from utils/hlmv/icon1.ico or any Win32
#          resource, and nothing depends on a system icon theme. The original
#          viewer only shipped an application icon (MX_ICON); the Qt front end
#          needs that icon on every platform plus icons for its menu/toolbar
#          actions, so the extra glyphs are redrawn in the same visual language.
#
#          Re-runnable; output is byte-stable. Run from this directory:
#              python gen_icon.py
#          hlmv2.qrc embeds the PNGs into the executable on win32/linux/darwin.

from PIL import Image, ImageDraw

import math

BG = (43, 52, 64, 255)        # #2B3440 rounded square
CUBE = (62, 143, 169, 255)    # steel blue #3E8FA9
CUBE_HI = (94, 178, 205, 255) # highlight edge
ACCENT = (68, 170, 170, 255)  # cyan underline
INK = (94, 178, 205, 255)     # primary stroke for action glyphs
INK_DIM = (62, 143, 169, 255)
PAPER = (226, 240, 244, 255)

S = 1024       # application icon canvas
A = 64         # action glyph canvas (downscaled to 32/24/16)


def rounded_bg(d):
    d.rounded_rectangle([32, 32, S - 32, S - 32], radius=160, fill=BG)


def cube(d):
    cx, cy = S // 2, S // 2 - 40
    w, h, depth = 300, 300, 130
    top = [(cx, cy - h // 2), (cx + w // 2, cy - h // 2 + depth // 2),
           (cx, cy - h // 2 + depth), (cx - w // 2, cy - h // 2 + depth // 2)]
    bot = [(x, y + h) for (x, y) in top]
    width = 34
    d.line([top[0], top[1], top[2], top[3], top[0]], fill=CUBE_HI, width=width)
    d.line([top[3], bot[0]], fill=CUBE, width=width)
    d.line([top[2], bot[1]], fill=CUBE, width=width)
    d.line([top[1], bot[2]], fill=CUBE, width=width)
    d.line([bot[0], bot[1], bot[2], bot[3]], fill=CUBE, width=width)
    for i, (x, y) in enumerate([bot[0], bot[1], bot[3]]):
        d.line([top[i], bot[i]] if i != 1 else [top[2], bot[1]],
               fill=CUBE, width=width)
    d.polygon([top[1], top[2], bot[1], top[1]], outline=CUBE_HI, width=width // 2)


def app_icon():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    rounded_bg(d)
    cube(d)
    d.rounded_rectangle([200, S - 150, S - 200, S - 110], radius=20, fill=ACCENT)
    for size in (16, 24, 32, 48, 64, 128, 256):
        img.resize((size, size), Image.LANCZOS).save(f"hlmv-{size}.png")
    img.resize((256, 256), Image.LANCZOS).save(
        "hlmv.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64),
                           (128, 128), (256, 256)])


# ---------------------------------------------------------------------------
# Action glyphs. Each is drawn transparent at 64x64 so it works on light and
# dark Qt palettes; 4px strokes survive the 16px downscale.
# ---------------------------------------------------------------------------

def glyph():
    img = Image.new("RGBA", (A, A), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def emit(img, name):
    for size in (16, 24, 32):
        img.resize((size, size), Image.LANCZOS).save(f"{name}-{size}.png")


def g_open():
    img, d = glyph()
    d.polygon([(6, 18), (24, 18), (29, 24), (58, 24), (58, 52), (6, 52)],
              outline=INK, width=4)
    d.line([(6, 30), (58, 30)], fill=INK_DIM, width=3)
    d.line([(24, 38), (40, 38)], fill=PAPER, width=4)
    return img


def g_refresh():
    img, d = glyph()
    d.arc([10, 10, 54, 54], start=40, end=320, fill=INK, width=5)
    d.polygon([(48, 4), (58, 14), (44, 18)], fill=INK)
    d.polygon([(16, 60), (6, 50), (20, 46)], fill=INK)
    return img


def g_weapon():
    img, d = glyph()
    # a merged model: two wireframe boxes joined by a link
    d.rectangle([5, 20, 26, 40], outline=INK, width=4)
    d.rectangle([38, 24, 59, 44], outline=INK_DIM, width=4)
    d.line([(26, 30), (38, 32)], fill=PAPER, width=4)
    d.polygon([(38, 32), (32, 27), (32, 37)], fill=PAPER)
    return img


def g_center():
    img, d = glyph()
    for x, y, sx, sy in [(8, 8, 1, 1), (56, 8, -1, 1), (8, 56, 1, -1), (56, 56, -1, -1)]:
        d.line([(x, y), (x + 16 * sx, y)], fill=INK, width=4)
        d.line([(x, y), (x, y + 16 * sy)], fill=INK, width=4)
    d.ellipse([27, 27, 37, 37], outline=CUBE_HI, width=4)
    return img


def g_viewmodel():
    img, d = glyph()
    d.arc([6, 20, 58, 46], start=200, end=340, fill=INK, width=4)
    d.arc([6, 18, 58, 44], start=20, end=160, fill=INK, width=4)
    d.ellipse([26, 25, 38, 37], fill=CUBE_HI)
    return img


def g_screenshot():
    img, d = glyph()
    d.rounded_rectangle([6, 20, 58, 52], radius=6, outline=INK, width=4)
    d.rectangle([20, 14, 32, 20], fill=INK_DIM)
    d.ellipse([24, 29, 40, 45], outline=CUBE_HI, width=4)
    d.ellipse([29, 34, 35, 40], fill=PAPER)
    return img


def g_wireframe():
    img, d = glyph()
    t = [(32, 8), (54, 20), (32, 32), (10, 20)]
    b = [(x, y + 24) for x, y in t]
    d.line(t + [t[0]], fill=INK, width=4)
    d.line(b + [b[0]], fill=INK_DIM, width=4)
    for i in range(4):
        d.line([t[i], b[i]], fill=INK, width=4)
    return img


def g_bones():
    img, d = glyph()
    pts = [(12, 46), (28, 24), (46, 34), (54, 14)]
    d.line(pts, fill=INK_DIM, width=4)
    for x, y in pts:
        d.ellipse([x - 6, y - 6, x + 6, y + 6], fill=CUBE_HI)
    return img


def g_attachments():
    img, d = glyph()
    d.line([(32, 6), (32, 58)], fill=INK, width=4)
    d.line([(6, 32), (58, 32)], fill=INK, width=4)
    d.rectangle([25, 25, 39, 39], outline=CUBE_HI, width=4)
    return img


def g_ground():
    img, d = glyph()
    d.line([(4, 44), (60, 44)], fill=INK, width=4)
    for i in range(-2, 5):
        x = 10 + i * 12
        d.line([(32, 20), (x, 56)], fill=INK_DIM, width=3)
    for y, half in [(30, 16), (38, 26), (48, 40)]:
        d.line([(32 - half, y), (32 + half, y)], fill=INK_DIM, width=3)
    return img


def g_textured():
    img, d = glyph()
    d.rounded_rectangle([6, 6, 58, 58], radius=6, outline=INK, width=4)
    d.rectangle([10, 10, 32, 32], fill=CUBE_HI)
    d.rectangle([34, 34, 54, 54], fill=CUBE_HI)
    return img


def g_normals():
    img, d = glyph()
    d.line([(6, 48), (58, 48)], fill=INK_DIM, width=4)
    for x in (16, 32, 48):
        d.line([(x, 46), (x, 16)], fill=INK, width=4)
        d.polygon([(x, 8), (x - 6, 20), (x + 6, 20)], fill=CUBE_HI)
    return img


def g_hitboxes():
    img, d = glyph()
    d.rectangle([10, 12, 54, 52], outline=INK, width=4)
    for x, y in [(10, 12), (54, 12), (10, 52), (54, 52)]:
        d.rectangle([x - 4, y - 4, x + 4, y + 4], fill=CUBE_HI)
    return img


ACTIONS = {
    "open": g_open,
    "refresh": g_refresh,
    "weapon": g_weapon,
    "center": g_center,
    "viewmodel": g_viewmodel,
    "screenshot": g_screenshot,
    "wireframe": g_wireframe,
    "bones": g_bones,
    "attachments": g_attachments,
    "ground": g_ground,
    "textured": g_textured,
    "normals": g_normals,
    "hitboxes": g_hitboxes,
}


def main():
    app_icon()
    for name, fn in ACTIONS.items():
        emit(fn(), name)
    print("wrote application icon and %d action icons" % len(ACTIONS))


if __name__ == "__main__":
    main()
