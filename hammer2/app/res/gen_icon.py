#!/usr/bin/env python
# encoding: utf-8
#
# Generates the Hammer Qt6 editor application icon assets.
#
# Design: modern flat "design hammer" on a dark blue-grey rounded square
# (#2B3440). A large hammer (amber wooden handle + steel-teal head) sits at
# 45 degrees in the center, the head carries a lighter highlight band, and a
# thin cyan underline decorates the bottom. All shapes are big solid color
# blocks (no thin outlines) so the icon stays recognizable at 16 px.
#
# The artwork is drawn once at 1024x1024, then downscaled with LANCZOS to
# 16/24/32/48/64/128/256 and written as hammer-<size>.png next to this
# script. A multi-resolution hammer.ico (deployment only, never compiled)
# is generated from the same 256 px master. The script is idempotent:
# re-running it reproduces byte-identical assets (all drawing is
# deterministic, existing files are simply overwritten).
#
# Only Pillow is required (no third-party additions to the build itself).

import os

from PIL import Image, ImageDraw

# Output directory: the directory containing this script (hammer2/app/res).
RES_DIR = os.path.dirname(os.path.abspath(__file__))

# Master rendering resolution before LANCZOS downscaling.
CANVAS = 1024

# Palette (RGBA).
COLOR_BG = (43, 52, 64, 255)         # #2B3440 dark blue-grey rounded square
COLOR_HANDLE = (224, 161, 62, 255)   # #E0A13E amber wooden handle
COLOR_GRIP = (182, 124, 46, 255)     # #B67C2E darker amber grip end
COLOR_HEAD = (62, 143, 169, 255)     # #3E8FA9 steel-teal hammer head
COLOR_HIGHLIGHT = (126, 193, 214, 255)  # #7EC1D6 head highlight band
COLOR_UNDERLINE = (63, 200, 207, 255)   # #3FC8CF cyan underline accent

# PNG sizes to emit (16 px must stay readable).
PNG_SIZES = [16, 24, 32, 48, 64, 128, 256]

# Sizes embedded into the multi-resolution .ico file.
ICO_SIZES = [(256, 256), (128, 128), (64, 64), (48, 48),
             (32, 32), (24, 24), (16, 16)]


def rounded_square(draw, size, color, radius):
    """Draw the app-tile rounded square background."""
    draw.rounded_rectangle(
        (0, 0, size - 1, size - 1), radius=radius, fill=color)


def draw_hammer_layer(size):
    """Draw the upright hammer (head on top, handle below) on a transparent
    layer of the given size. The hammer is centered horizontally; the total
    vertical span head-top..handle-bottom is centered on the canvas."""
    layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)

    cx = size // 2

    # Vertical layout at 1024 scale (values scale linearly with size):
    # head 132..322, handle 292..892 (overlaps the head for a clean joint),
    # grip cap 792..892. Center of the span (132+892)/2 = 512 = canvas center.
    def v(y):
        return int(round(y * size / float(CANVAS)))

    s = size / float(CANVAS)

    # Handle shaft: rounded vertical bar (amber). Drawn first so the head
    # covers the joint.
    half_w = int(round(75 * s))
    draw.rounded_rectangle(
        (cx - half_w, v(292), cx + half_w, v(892)),
        radius=int(round(40 * s)), fill=COLOR_HANDLE)

    # Darker grip cap at the bottom end of the handle (separate large block).
    draw.rounded_rectangle(
        (cx - half_w, v(792), cx + half_w, v(892)),
        radius=int(round(40 * s)), fill=COLOR_GRIP)

    # Head: wide rounded horizontal bar (steel teal).
    head_half = int(round(200 * s))
    draw.rounded_rectangle(
        (cx - head_half, v(132), cx + head_half, v(322)),
        radius=int(round(44 * s)), fill=COLOR_HEAD)

    # Highlight band: lighter steel block inset along the top of the head.
    # Kept thick enough (56/1024) to still tint the head at 16 px.
    inset = int(round(20 * s))
    draw.rounded_rectangle(
        (cx - head_half + inset, v(152), cx + head_half - inset, v(208)),
        radius=int(round(28 * s)), fill=COLOR_HIGHLIGHT)

    return layer


def build_master():
    """Compose the 1024x1024 master icon and return it."""
    img = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Background tile with iOS-like corner radius (~20%).
    rounded_square(draw, CANVAS, COLOR_BG, radius=210)

    # Cyan underline decoration near the bottom, centered, clear of the
    # handle tip which ends in the lower-left quadrant.
    draw.rounded_rectangle(
        (292, 884, 732, 932), radius=24, fill=COLOR_UNDERLINE)

    # Hammer: drawn upright, then rotated 45 degrees clockwise so the head
    # points to the top-right and the handle to the bottom-left. Bicubic
    # resampling keeps the block edges smooth at master resolution.
    hammer = draw_hammer_layer(CANVAS).rotate(
        -45, expand=True, resample=Image.BICUBIC)

    # Paste the rotated hammer centered on the tile (alpha acts as mask).
    off = ((CANVAS - hammer.width) // 2, (CANVAS - hammer.height) // 2)
    img.alpha_composite(hammer, off)

    return img


def main():
    master = build_master()

    written = []

    # Per-size PNGs (compiled into the binary via hammer.qrc).
    for size in PNG_SIZES:
        out = os.path.join(RES_DIR, "hammer-%d.png" % size)
        master.resize((size, size), Image.LANCZOS).save(out)
        written.append(os.path.basename(out))

    # Multi-resolution .ico for deployment (not part of the build).
    ico_out = os.path.join(RES_DIR, "hammer.ico")
    master.resize((256, 256), Image.LANCZOS).save(ico_out, sizes=ICO_SIZES)
    written.append(os.path.basename(ico_out))

    print("wrote: " + ", ".join(written))


if __name__ == "__main__":
    main()
