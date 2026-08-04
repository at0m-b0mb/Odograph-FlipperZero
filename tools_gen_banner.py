#!/usr/bin/env python3
"""Render the Odograph GitHub banner + social-preview card.

The motif is the product in one picture: a serial number rendered as the
number plate it effectively is, standing over a road where the same serial
repeats away into the distance - because that is exactly what a tyre sensor
does. Every repeat is dimmer and smaller, which is the trail two roadside
receivers would write.

The seven-segment geometry is the same one helpers/od_ui.c draws on the
device, so the artwork is the product rather than a picture of it.

Type is auto-fitted to its column rather than hand-tuned, so a wording change
can never quietly overflow into the artwork. Supersampled, then
LANCZOS-downsampled.
"""
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops
import math
import os

OUT = os.path.join(os.path.dirname(__file__), "images")
os.makedirs(OUT, exist_ok=True)

BOLD = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
BLACK_F = "/System/Library/Fonts/Supplemental/Arial Black.ttf"
MONO = "/System/Library/Fonts/Supplemental/Andale Mono.ttf"
REG = "/System/Library/Fonts/Supplemental/Arial.ttf"

# palette - asphalt at night, lit by the signal itself
BG_TOP = (7, 8, 12)
BG_BOT = (18, 15, 12)
AMBER = (255, 138, 24)  # the flipper backlight, and the live serial
EMBER = (255, 92, 26)  # the hotter core of a fresh transmission
WHITE = (245, 242, 238)
GRAY = (150, 146, 140)
DIM = (54, 50, 46)
ROAD = (30, 28, 26)

SS = 2  # supersample

SERIAL = "45BB320F"

# ---------------- helpers/od_ui.c seven-segment table, mirrored ----------------

SEG_A, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F, SEG_G = (1 << i for i in range(7))
SEG_HEX = {
    "0": SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,
    "1": SEG_B | SEG_C,
    "2": SEG_A | SEG_B | SEG_D | SEG_E | SEG_G,
    "3": SEG_A | SEG_B | SEG_C | SEG_D | SEG_G,
    "4": SEG_B | SEG_C | SEG_F | SEG_G,
    "5": SEG_A | SEG_C | SEG_D | SEG_F | SEG_G,
    "6": SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,
    "7": SEG_A | SEG_B | SEG_C,
    "8": SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,
    "9": SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G,
    "A": SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,
    "B": SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,
    "C": SEG_A | SEG_D | SEG_E | SEG_F,
    "D": SEG_B | SEG_C | SEG_D | SEG_E | SEG_G,
    "E": SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,
    "F": SEG_A | SEG_E | SEG_F | SEG_G,
}


def add(a, b):
    """Additive compositing - light stacks, it does not replace."""
    return ImageChops.add(a, b)


def font(path, px):
    try:
        return ImageFont.truetype(path, int(px))
    except OSError:
        return ImageFont.truetype(BOLD, int(px))


def fit(path, px, text, max_w):
    """Largest size at or under `px` whose `text` fits `max_w`.

    The banner is regenerated whenever the pitch is reworded, and a line that
    silently runs into the artwork is the classic way a generated banner rots.
    """
    size = int(px)
    while size > 8:
        f = font(path, size)
        if f.getbbox(text)[2] <= max_w:
            return f
        size -= 2
    return font(path, 8)


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def vgradient(w, h):
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):
        d.line([(0, y), (w, y)], fill=lerp(BG_TOP, BG_BOT, y / max(1, h - 1)))
    return img


def draw_seg_string(d, x, y, text, cell_w, cell_h, thick, color, gap=None):
    """Seven-segment digits, same proportions as the device renderer."""
    gap = cell_w * 2 // 9 if gap is None else gap
    inset = max(1, cell_w * 2 // 9)
    mid = y + (cell_h - thick) // 2
    bar_w = cell_w - 2 * inset
    arm_h = mid - (y + inset)

    for ch in text:
        m = SEG_HEX.get(ch.upper(), SEG_G)
        if m & SEG_A:
            d.rectangle([x + inset, y, x + inset + bar_w, y + thick], fill=color)
        if m & SEG_G:
            d.rectangle([x + inset, mid, x + inset + bar_w, mid + thick], fill=color)
        if m & SEG_D:
            d.rectangle(
                [x + inset, y + cell_h - thick, x + inset + bar_w, y + cell_h], fill=color
            )
        if m & SEG_F:
            d.rectangle([x, y + inset, x + thick, y + inset + arm_h], fill=color)
        if m & SEG_B:
            d.rectangle(
                [x + cell_w - thick, y + inset, x + cell_w, y + inset + arm_h], fill=color
            )
        if m & SEG_E:
            d.rectangle([x, mid, x + thick, mid + arm_h], fill=color)
        if m & SEG_C:
            d.rectangle([x + cell_w - thick, mid, x + cell_w, mid + arm_h], fill=color)
        x += cell_w + gap
    return x


def seg_run_width(n, cell_w, gap):
    return n * cell_w + (n - 1) * gap


def build_plate(w, h, cx, cy, cell_w, cell_h, thick, color):
    """The hero: the serial in a bolted plate frame, on its own layer."""
    layer = Image.new("RGB", (w, h), (0, 0, 0))
    d = ImageDraw.Draw(layer)

    gap = cell_w * 2 // 9
    run = seg_run_width(len(SERIAL), cell_w, gap)
    x = cx - run // 2
    y = cy - cell_h // 2

    pad_x, pad_y = cell_w, cell_h * 2 // 5
    fx0, fy0 = x - pad_x, y - pad_y
    fx1, fy1 = x + run + pad_x, y + cell_h + pad_y
    r = cell_h // 4
    d.rounded_rectangle([fx0, fy0, fx1, fy1], radius=r, outline=color, width=max(2, thick // 2))

    bolt = max(3, thick // 2)
    for bx, by in (
        (fx0 + pad_x // 2, fy0 + pad_y // 2),
        (fx1 - pad_x // 2, fy0 + pad_y // 2),
        (fx0 + pad_x // 2, fy1 - pad_y // 2),
        (fx1 - pad_x // 2, fy1 - pad_y // 2),
    ):
        d.ellipse([bx - bolt, by - bolt, bx + bolt, by + bolt], fill=color)

    draw_seg_string(d, x, y, SERIAL, cell_w, cell_h, thick, color, gap)
    return layer, (fx0, fy0, fx1, fy1)


def build_trail(w, h, start_y, cx, cell_w, cell_h, thick):
    """The same serial repeating away down the road, dimmer each time.

    This is the whole thesis as artwork: the identity does not change, so a
    second reading of it anywhere else is another point on a line.
    """
    layer = Image.new("RGB", (w, h), (0, 0, 0))
    d = ImageDraw.Draw(layer)

    y = start_y
    cw, ch, th = cell_w, cell_h, thick
    for i in range(5):
        cw = max(4, int(cw * 0.62))
        ch = max(7, int(ch * 0.62))
        th = max(1, int(th * 0.62))
        gap = max(1, cw * 2 // 9)
        fade = 0.52 ** (i + 1)
        color = tuple(int(c * fade) for c in AMBER)

        run = seg_run_width(len(SERIAL), cw, gap)
        y += int(ch * 1.15)
        if y + ch > h - int(h * 0.07):
            break
        draw_seg_string(d, cx - run // 2, y, SERIAL, cw, ch, th, color, gap)
    return layer


def build_arcs(w, h, cx, cy, rmax, color):
    """Concentric arcs behind the plate - the transmission it rode in on."""
    layer = Image.new("RGB", (w, h), (0, 0, 0))
    d = ImageDraw.Draw(layer)
    n = 7
    for i in range(n):
        t = (i + 1) / n
        r = int(rmax * t)
        fade = (1.0 - t) ** 1.5
        c = tuple(int(v * fade * 0.55) for v in color)
        if max(c) <= 2:
            continue
        d.arc(
            [cx - r, cy - r, cx + r, cy + r],
            start=-58,
            end=58,
            fill=c,
            width=max(2, int(rmax * 0.012)),
        )
        d.arc(
            [cx - r, cy - r, cx + r, cy + r],
            start=122,
            end=238,
            fill=c,
            width=max(2, int(rmax * 0.012)),
        )
    return layer


def build_road(w, h, horizon, color, vx=None):
    """A road receding to a vanishing point, drawn as lane dashes."""
    layer = Image.new("RGB", (w, h), (0, 0, 0))
    d = ImageDraw.Draw(layer)
    vx, vy = (w // 2 if vx is None else vx), horizon

    # verges
    for side in (-1, 1):
        d.polygon(
            [(vx, vy), (vx + side * w, h), (vx + side * int(w * 0.62), h)],
            fill=tuple(int(c * 0.62) for c in ROAD),
        )

    # centre dashes, shorter and dimmer as they recede
    y = h
    seg = int(h * 0.16)
    i = 0
    while y > vy + 6 and i < 22:
        t = (y - vy) / max(1, (h - vy))
        wdt = max(1, int(w * 0.012 * t))
        length = max(2, int(seg * t * 0.55))
        fade = 0.16 + 0.30 * t
        d.rectangle(
            [vx - wdt, y - length, vx + wdt, y],
            fill=tuple(int(c * fade) for c in color),
        )
        y -= length + int(length * 1.2) + 4
        i += 1
    return layer


def render(path, W, H, layout="wide"):
    w, h = W * SS, H * SS
    img = vgradient(w, h)

    art_cx = int(w * (0.71 if layout == "wide" else 0.5))
    art_cy = int(h * (0.46 if layout == "wide" else 0.26))
    art_w = int(w * (0.52 if layout == "wide" else 0.66))

    # road first, so everything else sits on it
    road = build_road(w, h, int(h * 0.30), AMBER, vx=art_cx)
    road = road.filter(ImageFilter.GaussianBlur(radius=w * 0.0015))
    img = add(img, road)

    cell_w = int(art_w * 0.075)
    cell_h = int(cell_w * 15 / 9)
    thick = max(3, int(cell_w * 2 / 9))

    arcs = build_arcs(w, h, art_cx, art_cy, int(art_w * 0.72), AMBER)
    img = add(img, arcs.filter(ImageFilter.GaussianBlur(radius=w * 0.002)))
    img = add(img, arcs)

    # The card puts its type where the wide banner puts the receding trail,
    # so the trail is a wide-layout element only.
    if layout == "wide":
        trail = build_trail(w, h, art_cy + cell_h * 3 // 4, art_cx, cell_w, cell_h, thick)
        img = add(img, trail.filter(ImageFilter.GaussianBlur(radius=w * 0.0022)))
        img = add(img, trail)

    plate, box = build_plate(w, h, art_cx, art_cy, cell_w, cell_h, thick, AMBER)
    glow = plate.filter(ImageFilter.GaussianBlur(radius=w * 0.006))
    img = add(img, ImageChops.multiply(glow, Image.new("RGB", (w, h), (150, 150, 150))))
    img = add(img, plate)

    # a hot core on the plate, so the serial reads as live rather than printed
    core = Image.new("RGB", (w, h), (0, 0, 0))
    dc = ImageDraw.Draw(core)
    gap = cell_w * 2 // 9
    run = seg_run_width(len(SERIAL), cell_w, gap)
    draw_seg_string(
        dc,
        art_cx - run // 2,
        art_cy - cell_h // 2,
        SERIAL,
        cell_w,
        cell_h,
        max(1, thick // 3),
        EMBER,
        gap,
    )
    img = add(img, core.filter(ImageFilter.GaussianBlur(radius=w * 0.004)))

    d = ImageDraw.Draw(img)

    # ---------------- type ----------------
    # The two layouts get their own scale, not one set of ratios stretched
    # across both: the card is half as wide per line and would otherwise run
    # its body text off the bottom edge.
    if layout == "wide":
        col_x, col_w = int(w * 0.06), int(w * 0.40)
        title_y = int(h * 0.26)
        s_title, s_tag, s_sub = 0.075, 0.030, 0.0205
        sub = [
            "Every tyre broadcasts a fixed serial in the clear.",
            "Odograph decodes it and shows you what it gives away.",
        ]
        show_chips = True
        foot_y = 0.90
    else:
        col_x, col_w = int(w * 0.08), int(w * 0.84)
        title_y = int(h * 0.58)
        s_title, s_tag, s_sub = 0.062, 0.026, 0.018
        sub = ["Every tyre broadcasts a fixed serial in the clear."]
        show_chips = False
        foot_y = 0.92

    f_title = fit(BLACK_F, w * s_title, "ODOGRAPH", col_w)
    d.text((col_x, title_y), "ODOGRAPH", font=f_title, fill=WHITE)
    ty = title_y + f_title.getbbox("ODOGRAPH")[3] + int(h * 0.030)

    tagline = "A licence plate you cannot cover."
    f_tag = fit(BOLD, w * s_tag, tagline, col_w)
    d.text((col_x, ty), tagline, font=f_tag, fill=AMBER)
    ty += f_tag.getbbox(tagline)[3] + int(h * 0.026)

    for line in sub:
        f_sub = fit(REG, w * s_sub, line, col_w)
        d.text((col_x, ty), line, font=f_sub, fill=GRAY)
        ty += f_sub.getbbox(line)[3] + int(h * 0.016)

    if show_chips:
        ty += int(h * 0.022)
        chips = ["315 / 433.92 MHz", "6 protocols", "listen-only"]
        f_chip = font(BOLD, w * 0.0165)
        cx = col_x
        for chip in chips:
            bb = f_chip.getbbox(chip)
            pad_x, pad_y = int(w * 0.011), int(h * 0.014)
            cw = bb[2] + pad_x * 2
            chh = bb[3] + pad_y * 2
            if cx + cw > col_x + col_w:
                cx = col_x
                ty += chh + int(h * 0.016)
            d.rounded_rectangle(
                [cx, ty, cx + cw, ty + chh], radius=chh // 2, outline=DIM, width=max(2, SS)
            )
            d.text((cx + pad_x, ty + pad_y - bb[1] // 2), chip, font=f_chip, fill=GRAY)
            cx += cw + int(w * 0.012)

    # footer, never allowed to land on top of the block above it
    f_foot = font(MONO, w * 0.0155)
    foot = "github.com/at0m-b0mb/Odograph-FlipperZero"
    fy = max(int(h * foot_y), ty + int(h * 0.02))
    d.text((col_x, fy), foot, font=f_foot, fill=tuple(int(c * 0.72) for c in GRAY))

    img.resize((W, H), Image.LANCZOS).save(path)
    print(f"wrote {path}")


if __name__ == "__main__":
    render(os.path.join(OUT, "banner.png"), 1280, 440, "wide")
    render(os.path.join(OUT, "social-preview.png"), 1280, 640, "card")
