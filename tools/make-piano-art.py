"""Makes Piano Pro's backgrounds: one picture for each type, light in a dark
room, in its family's colours: ACOUSTIC a warm concert hall (a pool of
stage light, soft gold glints out of focus), ELECTRIC a 70s studio (amber
and teal lamps through smoke), SYNTH an 80s night (a neon sun, a grid
running away), HOUSE a club (blue and violet light through haze). All
drawn here from maths; the same every run (each type its own seed).
Writes src/ui/pianoart/<nn>-<key>.jpg.

    python tools/make-piano-art.py
"""

import math
import pathlib

import numpy as np
from PIL import Image

WIDTH, HEIGHT = 1280, 540
OUT = pathlib.Path(__file__).resolve().parent.parent / "src" / "ui" / "pianoart"

# Each type, in Piano Pro's order (PianoTypes.cpp, kTypes): key, family, its
# colours (the room, the main light, the second light).
TYPES = [
    ("grand-piano", "ACOUSTIC", (14, 9, 6), (255, 196, 120), (255, 236, 200)),
    ("bright-grand", "ACOUSTIC", (12, 10, 10), (255, 226, 170), (210, 230, 255)),
    ("ballad-grand", "ACOUSTIC", (10, 8, 14), (240, 170, 120), (190, 160, 255)),
    ("upright-piano", "ACOUSTIC", (14, 8, 5), (230, 160, 90), (255, 210, 150)),
    ("felt-piano", "ACOUSTIC", (10, 9, 9), (200, 170, 140), (160, 140, 130)),
    ("electric-piano", "ELECTRIC", (10, 7, 6), (255, 150, 60), (90, 200, 200)),
    ("suitcase-rhodes", "ELECTRIC", (12, 6, 8), (255, 120, 80), (255, 200, 120)),
    ("wurlitzer", "ELECTRIC", (8, 10, 8), (255, 190, 80), (120, 220, 170)),
    ("electric-grand", "ELECTRIC", (8, 8, 14), (120, 170, 255), (255, 160, 200)),
    ("pianet", "ELECTRIC", (10, 10, 6), (230, 220, 110), (255, 140, 70)),
    ("dx-piano", "ELECTRIC", (6, 8, 16), (110, 200, 255), (220, 240, 255)),
    ("lofi-keys", "ELECTRIC", (10, 8, 10), (220, 150, 160), (150, 170, 210)),
    ("poly-keys", "SYNTH", (12, 4, 22), (255, 80, 180), (80, 220, 255)),
    ("poly-strings", "SYNTH", (8, 4, 24), (190, 110, 255), (255, 150, 220)),
    ("poly-brass", "SYNTH", (18, 6, 14), (255, 140, 60), (255, 70, 140)),
    ("house-piano", "HOUSE", (4, 6, 20), (70, 140, 255), (190, 110, 255)),
    ("big-house-piano", "HOUSE", (8, 4, 22), (150, 90, 255), (60, 200, 255)),
    ("prog-house-piano", "HOUSE", (4, 10, 18), (60, 210, 230), (120, 120, 255)),
    ("emotional-piano", "HOUSE", (10, 6, 20), (255, 130, 200), (120, 150, 255)),
]

YY, XX = np.mgrid[0:HEIGHT, 0:WIDTH].astype(np.float32)


def glow(img, cx, cy, rx, ry, colour, strength):
    d = ((XX - cx) / rx) ** 2 + ((YY - cy) / ry) ** 2
    img += np.exp(-d)[..., None] * np.array(colour, np.float32) * strength


def bokeh(img, rng, colours, count, size, y_range):
    for _ in range(count):
        cx = rng.uniform(0, WIDTH)
        cy = rng.uniform(*y_range)
        r = rng.uniform(*size)
        c = colours[rng.integers(len(colours))]
        d = np.sqrt((XX - cx) ** 2 + (YY - cy) ** 2) / r
        disc = np.clip(1.15 - d, 0, 1) ** 0.6 * (0.7 + 0.3 * np.clip(d, 0, 1))  # a lens's disc: brighter at its rim
        img += disc[..., None] * np.array(c, np.float32) * rng.uniform(0.05, 0.16)


def beams(img, rng, colour, count, origin_y, spread):
    ox = WIDTH / 2 + rng.uniform(-200, 200)
    for _ in range(count):
        angle = math.radians(rng.uniform(-spread, spread)) + math.pi / 2
        dx, dy = math.cos(angle), math.sin(angle)
        # Distance from the beam's line, widening away from its source.
        px, py = XX - ox, YY - origin_y
        along = px * dx + py * dy
        across = np.abs(px * dy - py * dx)
        width = 8 + 0.12 * np.clip(along, 0, None)
        beam = np.exp(-(across / width) ** 2) * (along > 0) * np.exp(-along / 900)
        img += beam[..., None] * np.array(colour, np.float32) * rng.uniform(0.08, 0.2)


def haze(img, rng, colour, strength):
    noise = rng.normal(size=(HEIGHT // 30 + 2, WIDTH // 30 + 2)).astype(np.float32)
    big = np.array(Image.fromarray(noise).resize((WIDTH, HEIGHT), Image.BICUBIC))
    big = (big - big.min()) / (big.max() - big.min() + 1e-6)
    img += (big ** 2)[..., None] * np.array(colour, np.float32) * strength


def grid(img, colour):
    horizon = HEIGHT * 0.58
    below = YY > horizon
    depth = np.where(below, (YY - horizon) / (HEIGHT - horizon), 0)
    d = np.maximum(depth, 0.02)
    # A flat floor seen in perspective: rows closer together toward the
    # horizon, columns meeting at its middle.
    z = 1.0 / d
    rows = np.abs(np.sin(z * 2.2)) < 0.05 * z ** 0.15 * d * 6
    x = (XX - WIDTH / 2) / d
    cols = np.abs(np.sin(x / 260.0 * math.pi)) < 0.035 / np.sqrt(d)
    line = (rows | cols) & below & (depth > 0.03)
    img += (line * depth ** 0.7 * 0.9)[..., None] * np.array(colour, np.float32)


def draw(index, key, family, room, light, second):
    rng = np.random.default_rng(1000 + index)
    img = np.zeros((HEIGHT, WIDTH, 3), np.float32) + np.array(room, np.float32)
    if family == "ACOUSTIC":
        glow(img, WIDTH * rng.uniform(0.42, 0.58), HEIGHT * 0.15, 520, 260, light, 0.55)
        glow(img, WIDTH / 2, HEIGHT * 1.05, 900, 160, light, 0.25)
        haze(img, rng, light, 0.08)
        bokeh(img, rng, [light, second], 26, (18, 60), (40, HEIGHT * 0.75))
        beams(img, rng, second, 3, -60, 12)
    elif family == "ELECTRIC":
        glow(img, WIDTH * 0.22, HEIGHT * 0.3, 420, 300, light, 0.5)
        glow(img, WIDTH * 0.8, HEIGHT * 0.45, 420, 300, second, 0.4)
        haze(img, rng, light, 0.14)
        bokeh(img, rng, [light, second], 18, (25, 90), (0, HEIGHT))
    elif family == "SYNTH":
        cx, cy = WIDTH / 2, HEIGHT * 0.52
        d = np.sqrt((XX - cx) ** 2 + (YY - cy) ** 2)
        sun = (d < 170) & ((YY < cy - 10) | (np.sin(YY / 7.0) > -0.2)) & (YY < HEIGHT * 0.58)
        mix = np.clip((YY - (cy - 170)) / 220, 0, 1)[..., None]
        sun_colour = np.array(second, np.float32) * (1 - mix) + np.array(light, np.float32) * mix
        img += sun[..., None] * sun_colour * 0.75
        glow(img, cx, cy, 420, 220, light, 0.3)
        grid(img, light)
        haze(img, rng, second, 0.07)
    else:  # HOUSE
        glow(img, WIDTH / 2, HEIGHT * 0.95, 700, 200, light, 0.45)
        beams(img, rng, light, 7, HEIGHT * 1.1, 40)
        beams(img, rng, second, 5, -80, 30)
        haze(img, rng, second, 0.16)
        bokeh(img, rng, [light, second], 12, (10, 30), (0, HEIGHT * 0.6))
    # Darker at the edges and under the controls (they sit over the lower part).
    edge = 1 - 0.55 * (((XX - WIDTH / 2) / (WIDTH * 0.62)) ** 2 + ((YY - HEIGHT * 0.4) / (HEIGHT * 0.85)) ** 2)
    img *= np.clip(edge, 0.25, 1)[..., None]
    img = 255 * (1 - np.exp(-img / 255 * 1.6))  # soft, film-like top
    Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).save(OUT / f"{index + 1:02d}-{key}.jpg", quality=86)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for i, t in enumerate(TYPES):
        draw(i, *t)
        print(f"{i + 1:02d}-{t[0]}.jpg")


if __name__ == "__main__":
    main()
