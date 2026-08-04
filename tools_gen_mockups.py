#!/usr/bin/env python3
"""Render Flipper-style mock screenshots (128x64, orange backlight) for the README.

These mirror the view sources line for line - the same layout constants, the
same seven-segment geometry from helpers/od_ui.c - so a layout collision or an
overlapping label shows up here before it ships on a device. Text is positioned
by BASELINE (PIL anchor "ls"/"rs"/"ms") because canvas_draw_str takes y as the
baseline, not the top.

The sensors are synthetic but the values are real ones: the Ford, Citroen and
Toyota payloads are the same off-air captures the host tests decode.
"""
from PIL import Image, ImageDraw, ImageFont
import os

S = 6  # upscale factor
W, H = 128, 64
BG = (255, 130, 0)  # flipper backlight orange
FG = (10, 8, 4)  # near-black pixels
OUT = os.path.join(os.path.dirname(__file__), "images")
os.makedirs(OUT, exist_ok=True)

MONO = "/System/Library/Fonts/Supplemental/Andale Mono.ttf"
BOLD = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"

f_sec = ImageFont.truetype(MONO, 7 * S - 2)  # FontSecondary
f_pri = ImageFont.truetype(BOLD, 8 * S)  # FontPrimary
f_key = ImageFont.truetype(MONO, 10 * S // 1 - 24)  # FontKeyboard, 6px advance


# ---------------- canvas ----------------


class Screen:
    """A 128x64 Flipper canvas, drawn at S times scale."""

    def __init__(self):
        self.img = Image.new("RGB", (W * S, H * S), BG)
        self.d = ImageDraw.Draw(self.img)
        self.color = FG

    def set_color(self, white):
        self.color = BG if white else FG

    def box(self, x, y, w, h, color=None):
        self.d.rectangle(
            [x * S, y * S, (x + w) * S - 1, (y + h) * S - 1], fill=color or self.color
        )

    def frame(self, x, y, w, h, color=None):
        c = color or self.color
        self.box(x, y, w, 1, c)
        self.box(x, y + h - 1, w, 1, c)
        self.box(x, y, 1, h, c)
        self.box(x + w - 1, y, 1, h, c)

    def rframe(self, x, y, w, h, r, color=None):
        c = color or self.color
        self.box(x + r, y, w - 2 * r, 1, c)
        self.box(x + r, y + h - 1, w - 2 * r, 1, c)
        self.box(x, y + r, 1, h - 2 * r, c)
        self.box(x + w - 1, y + r, 1, h - 2 * r, c)
        for dx, dy in ((r - 1, 1), (1, r - 1)):
            self.box(x + dx, y + dy, 1, 1, c)
            self.box(x + w - 1 - dx, y + dy, 1, 1, c)
            self.box(x + dx, y + h - 1 - dy, 1, 1, c)
            self.box(x + w - 1 - dx, y + h - 1 - dy, 1, 1, c)

    def line(self, x0, y0, x1, y1, color=None):
        self.d.line(
            [x0 * S, y0 * S, x1 * S, y1 * S], fill=color or self.color, width=S
        )

    def dot(self, x, y, color=None):
        self.box(x, y, 1, 1, color)

    def circle(self, cx, cy, r, color=None):
        c = color or self.color
        self.d.ellipse(
            [(cx - r) * S, (cy - r) * S, (cx + r + 1) * S - 1, (cy + r + 1) * S - 1],
            outline=c,
            width=S,
        )

    def disc(self, cx, cy, r, color=None):
        self.d.ellipse(
            [(cx - r) * S, (cy - r) * S, (cx + r + 1) * S - 1, (cy + r + 1) * S - 1],
            fill=color or self.color,
        )

    def text(self, x, baseline, s, font=None, align="l", color=None):
        anchor = {"l": "ls", "r": "rs", "m": "ms"}[align]
        self.d.text(
            (x * S, baseline * S),
            s,
            font=font or f_sec,
            fill=color or self.color,
            anchor=anchor,
        )

    def save(self, name):
        path = os.path.join(OUT, name)
        self.img.save(path)
        print(f"wrote {path}")
        return self.img


def bezel(img, label=None):
    """Put the screen in a device-ish frame so the README reads as hardware."""
    pad = 10 * S
    bar = 0 if not label else 12 * S
    out = Image.new("RGB", (img.width + 2 * pad, img.height + 2 * pad + bar), (24, 22, 20))
    out.paste(img, (pad, pad))
    if label:
        d = ImageDraw.Draw(out)
        d.text(
            (out.width // 2, img.height + pad + bar // 2 + pad // 4),
            label,
            font=ImageFont.truetype(BOLD, 7 * S),
            fill=(240, 236, 230),
            anchor="mm",
        )
    return out


# ---------------- helpers/od_ui.c, mirrored ----------------

SEG_W, SEG_H, SEG_T, SEG_GAP, SEG_INSET = 9, 15, 2, 2, 2

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


def seg_width(n):
    return n * SEG_W + (n - 1) * SEG_GAP if n else 0


def draw_seg(s, x, y, hexstr):
    mid = y + (SEG_H - SEG_T) // 2
    bar_w = SEG_W - 2 * SEG_INSET
    arm_h = mid - (y + SEG_INSET)
    for ch in hexstr:
        m = SEG_HEX.get(ch.upper(), SEG_G)
        if m & SEG_A:
            s.box(x + SEG_INSET, y, bar_w, SEG_T)
        if m & SEG_G:
            s.box(x + SEG_INSET, mid, bar_w, SEG_T)
        if m & SEG_D:
            s.box(x + SEG_INSET, y + SEG_H - SEG_T, bar_w, SEG_T)
        if m & SEG_F:
            s.box(x, y + SEG_INSET, SEG_T, arm_h)
        if m & SEG_B:
            s.box(x + SEG_W - SEG_T, y + SEG_INSET, SEG_T, arm_h)
        if m & SEG_E:
            s.box(x, mid, SEG_T, arm_h)
        if m & SEG_C:
            s.box(x + SEG_W - SEG_T, mid, SEG_T, arm_h)
        x += SEG_W + SEG_GAP


def draw_plate(s, cx, y, hexstr):
    w = seg_width(len(hexstr))
    x = cx - w // 2
    fx, fy, fw, fh = x - 7, y - 5, w + 14, SEG_H + 10
    s.rframe(fx, fy, fw, fh, 3)
    s.dot(fx + 3, fy + 3)
    s.dot(fx + fw - 4, fy + 3)
    s.dot(fx + 3, fy + fh - 4)
    s.dot(fx + fw - 4, fy + fh - 4)
    draw_seg(s, x, y, hexstr)


def draw_signal(s, x, y, rssi):
    level = max(0, min(4, (rssi + 100) // 13))
    for i in range(4):
        h = 3 + i * 2
        bx, by = x + i * 3, y + 9 - h
        if i < level:
            s.box(bx, by, 2, h)
        else:
            s.dot(bx, y + 8)


def draw_meter(s, x, y, w, h, pct):
    s.frame(x, y, w, h)
    fill = ((w - 4) * min(pct, 100)) // 100
    if fill > 0:
        s.box(x + 2, y + 2, fill, h - 4)


def draw_dots(s, cx, y, count, active):
    pitch = 6
    x = cx - (count * pitch) // 2 + pitch // 2
    for i in range(count):
        if i == active:
            s.disc(x, y, 2)
        else:
            s.circle(x, y, 1)
        x += pitch


def draw_badge(s, cx, y, text):
    tw = s.d.textlength(text, font=f_pri) / S
    w, h = int(tw) + 14, 17
    x = cx - w // 2
    # canvas_draw_rbox: a filled rounded box
    s.box(x + 3, y, w - 6, h)
    s.box(x, y + 3, w, h - 6)
    s.box(x + 1, y + 1, w - 2, h - 2)
    s.text(cx, y + h // 2 + 3, text, f_pri, "m", BG)


def draw_wheel(s, cx, cy, frame):
    s.circle(cx, cy, 12)
    s.circle(cx, cy, 11)
    s.circle(cx, cy, 5)
    if (frame // 2) & 1:
        s.line(cx - 8, cy - 8, cx - 4, cy - 4)
        s.line(cx + 8, cy + 8, cx + 4, cy + 4)
        s.line(cx - 8, cy + 8, cx - 4, cy + 4)
        s.line(cx + 8, cy - 8, cx + 4, cy - 4)
    else:
        s.line(cx, cy - 10, cx, cy - 6)
        s.line(cx, cy + 10, cx, cy + 6)
        s.line(cx - 10, cy, cx - 6, cy)
        s.line(cx + 10, cy, cx + 6, cy)
    s.line(cx + 8, cy - 9, cx + 11, cy - 12)
    s.box(cx + 11, cy - 14, 3, 3)
    wave = frame % 4
    for i in range(3):
        if i >= wave:
            continue
        r = 4 + i * 4
        ox, oy = cx + 14, cy - 13
        s.line(ox + r - 2, oy - r, ox + r, oy - r + 2)
        s.line(ox + r, oy - r + 2, ox + r, oy + r - 2)
        s.line(ox + r, oy + r - 2, ox + r - 2, oy + r)


# ---------------- views/sweep_view.c, mirrored ----------------

SW_HDR_BASE, SW_RULE_Y, SW_TOP = 9, 11, 13
SW_ROW_H, SW_ROWS, SW_TEXT_DY = 13, 3, 9
SW_COL_MARK, SW_COL_ID, SW_COL_TAG, SW_COL_PRESS, SW_COL_BARS = 2, 9, 52, 70, 108
SW_STRIP_Y, SW_STRIP_H, SW_STRIP_BASE = 52, 12, 61
SW_WHEEL_CX, SW_WHEEL_CY, SW_INFO_X = 26, 33, 52

# The same sensors the host tests decode, plus a Schrader for the ASK path.
SENSORS = [
    ("45BB320F", "FRD", "183", -58, True),
    ("8ADD48D4", "CIT", "289", -71, False),
    ("FB0A43E7", "TOY", "253", -84, False),
    ("03A38B2", "SCH", "200", -92, False),
]


def sweep_header(s, band="433.92", profile="FSK", rssi=-71, auto=False, hop=0):
    left = f"{band} {profile}"
    s.text(2, SW_HDR_BASE, left)
    if auto:
        x = 2 + int(s.d.textlength(left, font=f_sec) / S) + 4
        s.frame(x, 3, 18, 6)
        fill = (hop * 16) // 4000
        if fill > 0:
            s.box(x + 1, 4, fill, 4)
    s.text(W - 2, SW_HDR_BASE, f"{rssi} dBm", align="r")
    s.line(0, SW_RULE_Y, W - 1, SW_RULE_Y)


def screen_listening():
    s = Screen()
    sweep_header(s, rssi=-92)
    draw_wheel(s, SW_WHEEL_CX, SW_WHEEL_CY, 2)
    s.text(SW_INFO_X, 22, "Listening", f_pri)
    s.text(SW_INFO_X, 33, "for 41s")
    s.text(SW_INFO_X, 43, "18k edges")
    s.line(0, 47, W - 1, 47)
    s.text(W // 2, 55, "Sensors wake when", align="m")
    s.text(W // 2, 63, "a wheel turns.", align="m")
    return s.save("screen_listening.png")


def sweep_row(s, y, row, selected):
    ident, tag, press, rssi, known = row
    if selected:
        s.box(0, y, W - 4, SW_ROW_H - 1)
        s.set_color(True)
    base = y + SW_TEXT_DY
    if known:
        s.disc(SW_COL_MARK + 2, y + 6, 2)
    else:
        s.dot(SW_COL_MARK + 2, y + 6)
    s.text(SW_COL_ID, base, ident)
    s.text(SW_COL_TAG, base, tag)
    s.text(SW_COL_PRESS, base, press)
    draw_signal(s, SW_COL_BARS, y + 2, rssi)
    if selected:
        s.set_color(False)


def screen_sweep():
    s = Screen()
    sweep_header(s, rssi=-58)
    for i, row in enumerate(SENSORS[:SW_ROWS]):
        sweep_row(s, SW_TOP + i * SW_ROW_H, row, i == 0)
    # scrollbar, 3 px at the right edge
    s.box(W - 3, SW_TOP, 3, SW_STRIP_Y - SW_TOP)
    s.box(W - 3, SW_TOP, 3, 14, BG)
    s.box(W - 2, SW_TOP + 1, 1, 12)
    s.box(0, SW_STRIP_Y, W, SW_STRIP_H)
    s.text(2, SW_STRIP_BASE, "4 sensors  124 bit", color=BG)
    s.text(W - 2, SW_STRIP_BASE, "CRITICAL", align="r", color=BG)
    return s.save("screen_sweep.png")


# ---------------- views/sensor_view.c, mirrored ----------------

SV_HDR_BASE, SV_RULE_Y = 9, 11
SV_CONTENT_BOTTOM, SV_DOTS_Y, SV_PLATE_Y = 57, 61, 18


def sensor_header(s, title, right):
    s.text(2, SV_HDR_BASE, title, f_pri)
    s.text(W - 2, SV_HDR_BASE, right, align="r")
    s.line(0, SV_RULE_Y, W - 1, SV_RULE_Y)


def screen_plate():
    s = Screen()
    sensor_header(s, "SERIAL", "MINE")
    draw_plate(s, W // 2, SV_PLATE_Y, "45BB320F")
    s.text(W // 2, 48, "Ford  32-bit serial", align="m")
    s.text(W // 2, SV_CONTENT_BOTTOM, "In your garage", align="m")
    draw_dots(s, W // 2, SV_DOTS_Y, 4, 0)
    return s.save("screen_plate.png")


def field(s, y, label, value):
    s.text(3, y, label)
    s.text(W - 3, y, value, align="r")


def screen_reading():
    s = Screen()
    sensor_header(s, "READING", "FRD")
    field(s, 22, "Pressure", "182.7 kPa")
    draw_meter(s, 3, 25, W - 6, 7, 52)
    field(s, 42, "Temperature", "23 C")
    field(s, 51, "Wheel", "turning")
    s.text(3, SV_CONTENT_BOTTOM, "All of it in the clear")
    draw_dots(s, W // 2, SV_DOTS_Y, 4, 1)
    return s.save("screen_reading.png")


def screen_track():
    s = Screen()
    sensor_header(s, "TRACK", "MINE")
    field(s, 20, "First heard", "6m 20s ago")
    field(s, 29, "Last heard", "3s ago")
    field(s, 38, "Packets", "47")
    field(s, 47, "Signal", "-58 / -54 dBm")
    strip_y, strip_h = 50, 7
    s.line(0, strip_y + strip_h, W - 1, strip_y + strip_h)
    track = [-92, -88, -84, -79, -73, -66, -60, -56, -54, -57, -63, -70, -78, -85, -90]
    for i, dbm in enumerate(track):
        h = max(1, min(strip_h, ((dbm + 110) * strip_h) // 60))
        x = 2 + i * 4
        if x >= W - 2:
            break
        s.box(x, strip_y + strip_h - h, 3, h)
    s.box(62, strip_y, 66, strip_h)
    s.text(125, strip_y + strip_h - 1, "SEEN AGAIN x2", align="r", color=BG)
    draw_dots(s, W // 2, SV_DOTS_Y, 4, 2)
    return s.save("screen_track.png")


def screen_raw():
    s = Screen()
    sensor_header(s, "RAW", "FRD")
    s.text(3, 21, "433.92 MHz")
    s.text(W - 3, 21, "Ford", align="r")
    s.text(3, 33, "45 BB 32 0F 6A", f_key)
    s.text(3, 44, "D4 46 C5", f_key)
    s.text(3, SV_CONTENT_BOTTOM, "Checksum verified")
    draw_dots(s, W // 2, SV_DOTS_Y, 4, 3)
    return s.save("screen_raw.png")


# ---------------- views/expose_view.c, mirrored ----------------


def screen_expose():
    s = Screen()
    s.text(2, 9, "EXPOSURE", f_pri)
    s.text(W - 2, 9, "my car", align="r")
    s.line(0, 11, W - 1, 11)
    draw_badge(s, W // 2, 17, "CRITICAL")
    s.text(W // 2, 43, "Unique and repeating", align="m")
    s.text(W // 2, 52, "4 serials  128 bits", align="m")
    s.box(0, 55, W, 9)
    s.text(W // 2, 63, "IT CAME BACK", align="m", color=BG)
    return s.save("screen_expose.png")


def screen_numbers():
    s = Screen()
    s.text(2, 9, "EXPOSURE", f_pri)
    s.text(W - 2, 9, "all heard", align="r")
    s.line(0, 11, W - 1, 11)
    field(s, 20, "Sensors heard", "4")
    field(s, 29, "Readable serials", "4 decoded")
    field(s, 38, "Identity on air", "124 bits")
    field(s, 47, "Others that match", "none on earth")
    s.line(0, 50, W - 1, 50)
    s.text(3, 57, "of 1.5 billion vehicles")
    draw_dots(s, W // 2, 61, 3, 1)
    return s.save("screen_numbers.png")


# ---------------- views/learn_view.c, mirrored ----------------


def learn_header(s, title, step):
    s.text(2, 9, title, f_pri)
    s.text(W - 2, 9, step, align="r")
    s.line(0, 11, W - 1, 11)


def screen_learn_sensor():
    s = Screen()
    learn_header(s, "THE SENSOR", "1/5")
    draw_wheel(s, 26, 34, 3)
    s.text(52, 22, "Every tyre has")
    s.text(52, 31, "a radio inside.")
    s.text(52, 44, "It wakes when")
    s.text(52, 53, "the wheel turns.")
    s.line(0, 56, W - 1, 56)
    s.text(2, 63, "So does anyone nearby.")
    return s.save("screen_learn_sensor.png")


def screen_learn_packet():
    s = Screen()
    learn_header(s, "THE PACKET", "2/5")
    y, h = 20, 13
    fields = [(2, 46, "SERIAL"), (50, 26, "kPa"), (78, 22, "C"), (102, 24, "CRC")]
    for i, (x, w, label) in enumerate(fields):
        if i == 0:
            s.box(x, y, w, h)
            s.text(x + w // 2, y + h - 3, label, align="m", color=BG)
        else:
            s.frame(x, y, w, h)
            s.text(x + w // 2, y + h - 3, label, align="m")
    s.text(6, 46, "45BB320F", f_key)
    s.text(2, 62, "Plain text. No key. Always.")
    return s.save("screen_learn_packet.png")


def screen_learn_trail():
    s = Screen()
    learn_header(s, "THE TRAIL", "3/5")
    road = 40
    s.line(0, road, W - 1, road)

    def mast(x, active):
        s.line(x, road, x, road - 12)
        s.line(x - 3, road - 12, x + 3, road - 12)
        s.line(x - 2, road, x + 2, road)
        if active:
            for r in (3, 5, 7):
                s.line(x - r, road - 11 - r, x - r + 2, road - 13 - r)
                s.line(x + r, road - 11 - r, x + r - 2, road - 13 - r)

    car_x = 96
    mast(24, False)
    mast(100, True)
    s.box(car_x, road - 6, 16, 4)
    s.box(car_x + 4, road - 9, 8, 3)
    s.disc(car_x + 3, road - 1, 2)
    s.disc(car_x + 12, road - 1, 2)
    s.text(2, 20, "Same serial, twice.")
    s.text(2, 52, "A 09:14", f_key)
    s.text(62, 52, "B 09:17", f_key)
    s.text(2, 62, "Route, time, speed.")
    return s.save("screen_learn_trail.png")


# ---------------- stock modules ----------------


def screen_menu():
    s = Screen()
    s.text(W // 2, 11, "Odograph", f_pri, "m")
    items = [
        "Listen",
        "Exposure report",
        "Garage",
        "How this works",
    ]
    for i, item in enumerate(items):
        y = 16 + i * 12
        if i == 0:
            s.box(0, y, W - 3, 12)
            s.text(4, y + 9, item, color=BG)
        else:
            s.text(4, y + 9, item)
    s.box(W - 3, 14, 3, 50, BG)
    s.box(W - 2, 15, 1, 48)
    s.box(W - 3, 15, 3, 20)
    return s.save("screen_menu.png")


def screen_settings():
    s = Screen()
    rows = [
        ("Band", "433.92"),
        ("Listen for", "FSK"),
        ("Pressure", "kPa"),
        ("Confirm with", "2 packets"),
        ("Sound", "On"),
    ]
    for i, (label, value) in enumerate(rows):
        y = i * 13
        if i == 1:
            s.box(0, y, W - 3, 13)
            s.text(4, y + 9, label, color=BG)
            s.text(W - 8, y + 9, f"<{value}>", align="r", color=BG)
        else:
            s.text(4, y + 9, label)
            s.text(W - 8, y + 9, value, align="r")
    s.box(W - 3, 0, 3, H, BG)
    s.box(W - 2, 1, 1, H - 2)
    s.box(W - 3, 6, 3, 18)
    return s.save("screen_settings.png")


# ---------------- composite ----------------


def contact_sheet(shots, name, cols=3, label_texts=None):
    tiles = [bezel(img, label_texts[i] if label_texts else None) for i, img in enumerate(shots)]
    tw, th = tiles[0].size
    rows = (len(tiles) + cols - 1) // cols
    gap = 8 * S
    sheet = Image.new(
        "RGB",
        (cols * tw + (cols + 1) * gap, rows * th + (rows + 1) * gap),
        (16, 15, 14),
    )
    for i, tile in enumerate(tiles):
        r, c = divmod(i, cols)
        sheet.paste(tile, (gap + c * (tw + gap), gap + r * (th + gap)))
    path = os.path.join(OUT, name)
    sheet.save(path)
    print(f"wrote {path}")


if __name__ == "__main__":
    listening = screen_listening()
    sweep = screen_sweep()
    plate = screen_plate()
    reading = screen_reading()
    track = screen_track()
    raw = screen_raw()
    expose = screen_expose()
    numbers = screen_numbers()
    learn_sensor = screen_learn_sensor()
    learn_packet = screen_learn_packet()
    learn_trail = screen_learn_trail()
    menu = screen_menu()
    settings = screen_settings()

    contact_sheet(
        [listening, sweep, plate, track, expose, learn_trail],
        "screens.png",
        cols=3,
        label_texts=[
            "Listening",
            "Sensors in earshot",
            "The serial",
            "It came back",
            "The verdict",
            "How tracking works",
        ],
    )
    contact_sheet(
        [plate, reading, track, raw],
        "screens_sensor.png",
        cols=4,
        label_texts=["SERIAL", "READING", "TRACK", "RAW"],
    )
    contact_sheet(
        [learn_sensor, learn_packet, learn_trail],
        "screens_learn.png",
        cols=3,
        label_texts=["1 - the sensor", "2 - the packet", "3 - the trail"],
    )
    contact_sheet(
        [menu, settings, numbers],
        "screens_menu.png",
        cols=3,
        label_texts=["Menu", "Settings", "The numbers"],
    )
