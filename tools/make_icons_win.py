# Windows icon generator — port of tools/make_icons.swift to Pillow.
# Draws the outlined padlock/bubble icon on a midnight -> deep blue gradient.
# Renders at 4x then downsamples (LANCZOS) for antialiasing, writes
# app_icon_<N>.png per size plus a multi-size app_icon.ico.
#
# Usage: python tools/make_icons_win.py [outdir]
#   default outdir: windows/runner/resources

import struct
import sys
from PIL import Image, ImageDraw

SIZES = [16, 24, 32, 48, 64, 128, 256]
SS = 4  # supersample factor


def draw_icon(size: int) -> Image.Image:
    s = size * SS
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    dr = ImageDraw.Draw(img)

    # Gradient: midnight (bottom) -> deep blue (top).
    # Swift draws angle=90 in a y-up context: colors[0] at bottom.
    top = (int(0.08 * 255), int(0.24 * 255), int(0.50 * 255))
    bot = (int(0.03 * 255), int(0.08 * 255), int(0.18 * 255))
    for y in range(s):
        t = y / (s - 1)  # y-down: top row = deep blue (matches Swift, whose top is colors[1])
        c = tuple(round(top[i] + (bot[i] - top[i]) * t) for i in range(3))
        dr.line([(0, y), (s, y)], fill=c + (255,))

    stroke_w = s * 0.032
    w = s * 0.60
    h = w * 0.78
    # Swift rect: x=(s-w)/2 - s*0.02, y=s*0.22 (y-up, bottom edge). Convert to
    # y-down: pillow_top = s - (y_up + h)
    x0 = (s - w) / 2 - s * 0.02
    y_up_bottom = s * 0.22
    rect = [x0, s - (y_up_bottom + h), x0 + w, s - y_up_bottom]  # l,t,r,b

    white = (255, 255, 255, 255)
    sw = max(1, round(stroke_w))

    # Shackle: arc center (midX, rectTop_up + sr*0.1) in y-up coords.
    sr = w * 0.24
    cx = (rect[0] + rect[2]) / 2
    cy_p = rect[1] - sr * 0.1  # y-up rectTop -> pillow top; +up -> smaller pillow y
    box = [cx - sr, cy_p - sr, cx + sr, cy_p + sr]
    # AppKit arc CCW -25..205 (gap at 205..335, bottom-right). Pillow angles are
    # clockwise from 3 o'clock (y down): same arc = 155 -> 385 (i.e. 155..360..25).
    dr.arc(box, start=155, end=385, fill=white, width=sw)

    # Body outline: rounded rect inset by stroke/2, radius h*0.18.
    inset = stroke_w / 2
    dr.rounded_rectangle(
        [rect[0] + inset, rect[1] + inset, rect[2] - inset, rect[3] - inset],
        radius=h * 0.18, outline=white, width=sw)

    # Bubble tail at bottom-left: polyline (y-up -> pillow flips y).
    tx = x0 + w * 0.25
    tail_len = w * 0.09
    b = rect[3]  # pillow bottom edge == appkit minY
    pts = [
        (tx, b - stroke_w * 0.4),
        (tx - tail_len, b + tail_len * 0.9),
        (tx + tail_len * 1.7, b - stroke_w * 0.4),
    ]
    dr.line(pts, fill=white, width=sw, joint="curve")

    # Keyhole: stroked circle + short stem. Swift: oval y=cy-kr*0.2 (y-up
    # bottom); cy_appkit = minY + h*0.45 -> pillow cy = b - h*0.45.
    kr = w * 0.09
    cy = b - h * 0.45
    # oval y-up bottom = cy_a - kr*0.2 -> pillow bottom = (s - (cy_a - kr*0.2))
    # i.e. pillow box = [cx-kr, py_bot-2kr, cx+kr, py_bot] where py_bot = cy + kr*0.2
    py_bot = cy + kr * 0.2
    dr.ellipse([cx - kr, py_bot - 2 * kr, cx + kr, py_bot], outline=white, width=sw)
    # stem: appkit (cx, cy_a-kr*0.5) -> (cx, cy_a-kr*1.5); pillow y grows down.
    dr.line([(cx, cy + kr * 0.5), (cx, cy + kr * 1.5)], fill=white, width=sw)

    return img.resize((size, size), Image.LANCZOS)


def main() -> None:
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "windows/runner/resources"
    imgs = []
    for n in SIZES:
        im = draw_icon(n)
        p = f"{out_dir}/app_icon_{n}.png"
        im.save(p)
        print("wrote", p)
        imgs.append(im)

    # Pillow's ICO saver ignores append_images; pack the ICO manually so every
    # size keeps its native render (PNG-in-ICO, supported since Vista).
    import io

    blobs = []
    for n, im in zip(SIZES, imgs):
        buf = io.BytesIO()
        im.save(buf, format="PNG")
        blobs.append(buf.getvalue())

    ico_path = f"{out_dir}/app_icon.ico"
    header = struct.pack("<HHH", 0, 1, len(blobs))
    entries = b""
    offset = 6 + 16 * len(blobs)
    for n, blob in zip(SIZES, blobs):
        w8 = 0 if n >= 256 else n
        entries += struct.pack(
            "<BBBBHHII", w8, w8, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    with open(ico_path, "wb") as f:
        f.write(header + entries + b"".join(blobs))
    print("wrote", ico_path)


if __name__ == "__main__":
    main()
