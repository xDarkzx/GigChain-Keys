"""Makes Pads Pro's backgrounds: one picture for each pad (flowing silk folds,
lit from above, in its family's colours, with the family's own touch: stars
for Synth, soft light for Air, fine lines for Strings, grain for Piano,
rays for Organ). All drawn here from maths; the same every run (each pad
its own seed). Writes src/ui/padart/<nn>-<key>.jpg.

    python tools/make-pad-art.py
"""

import math
import pathlib

import numpy as np
from PIL import Image

WIDTH, HEIGHT = 1280, 540
OUT = pathlib.Path(__file__).resolve().parent.parent / "src" / "ui" / "padart"

# Each family: its colours (deep, middle, light) and its touch.
FAMILIES = {
    "GUITAR": [((40, 8, 18), (196, 74, 52), (255, 196, 128)),
             ((30, 6, 30), (170, 40, 90), (255, 150, 140)),
             ((18, 6, 6), (120, 40, 20), (230, 140, 70)),
             ((44, 12, 8), (210, 96, 40), (255, 214, 150))],
    "SYNTH": [((6, 10, 40), (40, 90, 200), (150, 220, 255)),
              ((14, 4, 36), (90, 50, 190), (200, 160, 255)),
              ((4, 14, 30), (20, 110, 160), (120, 240, 230)),
              ((10, 10, 34), (60, 70, 180), (255, 170, 200))],
    "AIR": [((22, 14, 44), (150, 120, 220), (240, 220, 255)),
                ((10, 20, 40), (110, 160, 220), (230, 245, 255)),
                ((16, 24, 40), (120, 190, 210), (235, 255, 250)),
                ((20, 18, 40), (170, 160, 230), (255, 245, 255))],
    "STRINGS": [((6, 20, 24), (40, 140, 150), (170, 250, 240)),
                  ((14, 14, 22), (110, 120, 150), (220, 230, 255)),
                  ((10, 10, 12), (90, 80, 90), (200, 190, 180)),
                  ((8, 14, 30), (70, 120, 200), (210, 240, 255))],
    "PIANO": [((30, 20, 16), (150, 110, 80), (240, 210, 170)),
              ((24, 18, 20), (140, 90, 100), (230, 190, 180)),
              ((20, 20, 18), (120, 120, 100), (220, 220, 190)),
              ((28, 16, 22), (160, 100, 120), (250, 200, 190))],
    "ATMOS": [((8, 22, 18), (40, 120, 90), (190, 240, 210)),
              ((10, 18, 30), (50, 100, 140), (180, 220, 240)),
              ((14, 12, 10), (90, 70, 50), (210, 190, 160)),
              ((6, 10, 20), (30, 60, 110), (150, 190, 230))],
    "ORGAN": [((30, 20, 8), (200, 150, 60), (255, 236, 180)),
                ((20, 12, 30), (150, 110, 200), (255, 230, 255)),
                ((10, 16, 30), (90, 140, 220), (240, 248, 255)),
                ((30, 24, 10), (190, 170, 90), (255, 250, 220))],
}

# The pads, in Pads Pro's order (PadsPro.cpp, kTypes): key, family.
PADS = [("juno", "SYNTH"), ("supersaw", "SYNTH"), ("moog", "SYNTH"), ("flute", "AIR"),
        ("fmbells", "SYNTH"), ("tapechoir", "AIR"), ("violins", "STRINGS"), ("tonewheel", "ORGAN"),
        ("angelic", "AIR"), ("piano", "PIANO"), ("epiano", "PIANO"), ("ebow", "GUITAR"),
        ("stringmachine", "STRINGS"), ("pipeorgan", "ORGAN"), ("harmonium", "ORGAN"),
        ("twin", "SYNTH"), ("vector", "SYNTH"), ("bowl", "AIR"), ("nebula", "AIR"),
        ("wind", "ATMOS"), ("ocean", "ATMOS"), ("forest", "ATMOS"), ("cave", "ATMOS")]


def silk(rng, x, y):
    """A height field of a few broad folds sweeping across, as cloth laid in
    a long curve: the place bent by slow flows first (so the folds bend
    together), then two or three long waves across it."""
    angle = rng.uniform(-0.45, 0.45)
    u = x * math.cos(angle) + y * math.sin(angle)
    v = -x * math.sin(angle) + y * math.cos(angle)
    for _ in range(3):
        u2 = u + rng.uniform(0.25, 0.6) * np.sin(2 * math.pi * rng.uniform(0.12, 0.35) * v + rng.uniform(0, 6.3))
        v = v + rng.uniform(0.15, 0.4) * np.sin(2 * math.pi * rng.uniform(0.12, 0.35) * u + rng.uniform(0, 6.3))
        u = u2
    h = np.zeros_like(x)
    for i in range(3):
        freq = rng.uniform(0.35, 0.75) * (1 + 0.6 * i)
        h += np.sin(2 * math.pi * freq * u + rng.uniform(0, 6.3)) / (1 + 1.5 * i)
    return h


def mix(a, b, t):
    return a + (b - a) * t[..., None]


def make(index, key, family):
    rng = np.random.default_rng(1000 + index)
    deep, middle, light = (np.array(c, dtype=np.float32) / 255 for c in FAMILIES[family][index % 4])
    ys, xs = np.mgrid[0:HEIGHT, 0:WIDTH].astype(np.float32)
    x = xs / HEIGHT
    y = ys / HEIGHT

    h = silk(rng, x, y)
    # Lit from the upper left: the folds' slopes catch the light, softly.
    gy, gx = np.gradient(h)
    slope = -(gx * 0.7 + gy * 0.7) * HEIGHT * 0.35
    shine = (0.5 + 0.5 * np.tanh(slope * 1.4)) ** 3
    fold = 0.5 + 0.5 * np.tanh(h * 1.2)
    # Fine threads along the folds, as brushed silk has.
    threads = 0.5 + 0.5 * np.sin(h * rng.uniform(140, 200))

    # A slow sweep of colour across, the deep colour in the troughs.
    sweep = np.clip(0.25 + 0.6 * (x / (WIDTH / HEIGHT)) * rng.uniform(0.6, 1.0), 0, 1)
    image = mix(np.broadcast_to(deep, (HEIGHT, WIDTH, 3)), middle, fold * sweep)
    image = image + (light - image) * (shine * 0.7)[..., None]
    image = image * (0.9 + 0.1 * threads)[..., None]

    if family == "SYNTH":
        stars = np.zeros((HEIGHT, WIDTH), np.float32)
        for _ in range(260):
            sx, sy = rng.uniform(0, WIDTH), rng.uniform(0, HEIGHT * 0.8)
            size = rng.uniform(0.6, 2.2)
            d2 = (xs - sx) ** 2 + (ys - sy) ** 2
            stars += np.exp(-d2 / (2 * size * size)) * rng.uniform(0.3, 1.0)
        image = image + np.clip(stars, 0, 1)[..., None] * 0.9
    elif family == "AIR":
        glow = np.zeros((HEIGHT, WIDTH), np.float32)
        for _ in range(26):
            bx, by = rng.uniform(0, WIDTH), rng.uniform(0, HEIGHT)
            r = rng.uniform(14, 60)
            d = np.sqrt((xs - bx) ** 2 + (ys - by) ** 2)
            glow += np.clip(1 - d / r, 0, 1) ** 0.6 * rng.uniform(0.08, 0.22)
        image = image + glow[..., None] * light
    elif family == "STRINGS":
        lines = (np.sin(xs * 0.9 + h * 30) > 0.985).astype(np.float32)
        image = image + lines[..., None] * light * 0.12
    elif family == "PIANO":
        image = image * (0.9 + 0.1 * rng.random((HEIGHT, WIDTH), np.float32))[..., None]
        image = image * np.array([1.0, 0.96, 0.88], np.float32)
    elif family == "ORGAN":
        cx = WIDTH * rng.uniform(0.35, 0.65)
        ang = np.arctan2(ys + 80, xs - cx)
        rays = (0.5 + 0.5 * np.sin(ang * 34 + rng.uniform(0, 6))) ** 6
        fall = np.exp(-(ys / HEIGHT) * 2.2)
        image = image + (rays * fall * 0.35)[..., None] * light

    # Darker towards the bottom (the name and the knobs sit there) and at the sides.
    shade = 1 - 0.55 * np.clip((y * HEIGHT / HEIGHT - 0.45) / 0.55, 0, 1)
    side = 1 - 0.35 * ((x / (WIDTH / HEIGHT) - 0.5) * 2) ** 4
    image = image * (shade * side)[..., None]

    out = (np.clip(image, 0, 1) * 255).astype(np.uint8)
    path = OUT / f"{index + 1:02d}-{key}.jpg"
    Image.fromarray(out).save(path, quality=86, optimize=True, progressive=True)
    return path


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for index, (key, family) in enumerate(PADS):
        print(make(index, key, family))


if __name__ == "__main__":
    main()
