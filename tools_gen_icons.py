#!/usr/bin/env python3
"""Generate 1-bit 10x10 Flipper icons for Odograph from ASCII bitmaps.

'#' = foreground (black / on), anything else = background (white / off).
fbt thresholds PNGs to 1-bit, where dark pixels become 'on'.
"""
from PIL import Image
import os

OUT = os.path.join(os.path.dirname(__file__), "icons")
os.makedirs(OUT, exist_ok=True)

GLYPHS = {
    # App mark: a wheel with the valve sensor at its shoulder, radiating.
    # The whole product in ten pixels square - a tyre that talks.
    "odograph_10px": [
        "..####...#",
        ".#....#.#.",
        "#..##..#.#",
        "#.####.#.#",
        "#.####.#.#",
        "#..##..#.#",
        ".#....#.#.",
        "..####...#",
        "..........",
        "..........",
    ],
    # A number plate: the serial, framed and bolted on.
    "plate_10px": [
        "..........",
        "##########",
        "#........#",
        "#.#.##.#.#",
        "#.#.#..#.#",
        "#.#.##.#.#",
        "#........#",
        "##########",
        "..........",
        "..........",
    ],
    # Two masts and the trail between them: the same serial, twice.
    "trail_10px": [
        "#.......#.",
        "###...###.",
        ".#.....#..",
        ".#.....#..",
        ".#.....#..",
        "..........",
        ".##..##...",
        "####.####.",
        ".#.#..#.#.",
        "..........",
    ],
}


def render(name, rows):
    img = Image.new("1", (10, 10), 1)  # 1 = white
    px = img.load()
    for y, row in enumerate(rows):
        for x, ch in enumerate(row[:10]):
            if ch == "#":
                px[x, y] = 0  # 0 = black
    path = os.path.join(OUT, f"{name}.png")
    img.save(path)
    print(f"wrote {path}")


if __name__ == "__main__":
    for name, rows in GLYPHS.items():
        assert len(rows) == 10, f"{name}: expected 10 rows, got {len(rows)}"
        for row in rows:
            assert len(row) <= 10, f"{name}: row longer than 10 columns"
        render(name, rows)
