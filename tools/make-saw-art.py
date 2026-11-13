"""Makes Saw Stack Pro's backgrounds: one picture for each stack type, a
club stage at night (laser fans through haze, a glow behind the stage,
light caught in the smoke), in its family's colours: LEAD bright beams,
CHORDS wide fans, PLUCK sharp sparks of light, PAD soft haze, BASS low
heavy light. All drawn here from maths; the same every run (each type its
own seed). Writes src/ui/sawart/<nn>-<key>.jpg.

    python tools/make-saw-art.py
"""

import math
import pathlib

import numpy as np
from PIL import Image

WIDTH, HEIGHT = 1280, 540
OUT = pathlib.Path(__file__).resolve().parent.parent / "src" / "ui" / "sawart"

# Each family: its colours (the night, the beams, the glow).
FAMILIES = {
    "LEAD": [((10, 4, 26), (255, 60, 200), (90, 200, 255)),
             ((6, 8, 30), (80, 220, 255), (255, 120, 220)),
             ((14, 4, 20), (255, 90, 120), (255, 200, 120)),
             ((4, 10, 24), (120, 255, 220), (160, 120, 255))],
    "CHORDS": [((6, 6, 30), (90, 120, 255), (200, 140, 255)),
               ((10, 4, 30), (170, 90, 255), (90, 200, 255)),
               ((4, 10, 30), (60, 170, 255), (140, 255, 230)),
               ((12, 6, 28), (130, 110, 255), (255, 150, 220))],
    "PLUCK": [((4, 14, 18), (60, 255, 200), (200, 255, 120)),
              ((4, 12, 22), (80, 230, 255), (120, 255, 160)),
              ((10, 14, 8), (200, 255, 90), (90, 255, 220)),
              ((4, 16, 16), (100, 255, 170), (255, 240, 140))],
    "PAD": [((14, 8, 26), (200, 140, 255), (255, 190, 230)),
            ((8, 10, 26), (150, 170, 255), (230, 200, 255)),
            ((16, 8, 22), (255, 150, 210), (200, 170, 255)),
            ((8, 12, 24), (140, 200, 255), (255, 210, 240))],
    "BASS": [((20, 4, 6), (255, 70, 40), (255, 160, 60)),
             ((18, 4, 14), (255, 50, 90), (255, 130, 60)),
             ((22, 8, 4), (255, 120, 30), (255, 60, 60)),
             ((16, 4, 10), (220, 40, 60), (255, 170, 90))],
}

# The types, in Saw Stack Pro's order (SawStackTypes.cpp, kTypes): key, family.
TYPES = [("supersaw-lead", "LEAD"), ("trance-lead", "LEAD"), ("anthem-lead", "LEAD"), ("big-room-lead", "LEAD"),
         ("melodic-dubstep-lead", "LEAD"), ("hoover-lead", "LEAD"), ("silk-lead", "LEAD"),
         ("supersaw-chords", "CHORDS"), ("future-bass-chords", "CHORDS"), ("deep-house-chords", "CHORDS"),
         ("gated-trance-chords", "CHORDS"), ("synthwave-chords", "CHORDS"), ("uplifting-chords", "CHORDS"),
         ("supersaw-pluck", "PLUCK"), ("future-pluck", "PLUCK"), ("trance-pluck", "PLUCK"), ("tropical-pluck", "PLUCK"),
         ("rave-stab", "PLUCK"),
         ("supersaw-pad", "PAD"), ("dream-pad", "PAD"), ("analog-pad", "PAD"), ("swell-pad", "PAD"), ("air-pad", "PAD"),
         ("reese-bass", "BASS"), ("saw-bass", "BASS"), ("octave-bass", "BASS"), ("hoover-bass", "BASS"),
         ("pluck-bass", "BASS")]


def smoke(rng, xs, ys):
    """Slow drifting haze: a few broad waves bent by each other, 0-1."""
    u, v = xs / HEIGHT, ys / HEIGHT
    for _ in range(3):
        u2 = u + rng.uniform(0.2, 0.5) * np.sin(2 * math.pi * rng.uniform(0.2, 0.5) * v + rng.uniform(0, 6.3))
        v = v + rng.uniform(0.1, 0.3) * np.sin(2 * math.pi * rng.uniform(0.2, 0.5) * u + rng.uniform(0, 6.3))
        u = u2
    h = np.zeros_like(xs)
    for i in range(4):
        f = rng.uniform(0.6, 1.4) * (1 + i)
        h += np.sin(2 * math.pi * f * u + rng.uniform(0, 6.3)) * np.sin(2 * math.pi * f * 0.7 * v + rng.uniform(0, 6.3)) / (1 + i)
    return np.clip(0.5 + 0.45 * h, 0, 1)


def fan(rng, xs, ys, cx, cy, count, spread, sharp, tilt):
    """Beams fanning up from (cx, cy): bright cores, fading with distance."""
    ang = np.arctan2(cy - ys, xs - cx)  # 0 right, pi/2 straight up
    dist = np.sqrt((xs - cx) ** 2 + (ys - cy) ** 2)
    light = np.zeros_like(xs)
    for i in range(count):
        a = math.pi / 2 + tilt + spread * ((i / max(count - 1, 1)) - 0.5)
        d = np.abs(np.angle(np.exp(1j * (ang - a))))
        light += np.exp(-(d * sharp) ** 2) * rng.uniform(0.6, 1.0)
    return light * np.exp(-dist / (HEIGHT * 1.3)) * (ys < cy)


def glow(xs, ys, cx, cy, r):
    return np.exp(-((xs - cx) ** 2 + (ys - cy) ** 2) / (2 * r * r))


def make(index, key, family):
    rng = np.random.default_rng(2000 + index)
    night, beam, halo = (np.array(c, dtype=np.float32) / 255 for c in FAMILIES[family][index % 4])
    ys, xs = np.mgrid[0:HEIGHT, 0:WIDTH].astype(np.float32)
    haze = smoke(rng, xs, ys)

    # The night, lighter towards the stage glow at the bottom.
    image = np.broadcast_to(night, (HEIGHT, WIDTH, 3)).copy()
    stage = glow(xs, ys, WIDTH * rng.uniform(0.4, 0.6), HEIGHT * 1.05, HEIGHT * 0.55)
    image += stage[..., None] * halo * 0.55

    light = np.zeros_like(xs)
    if family == "LEAD":  # two bright fans crossing
        for side in (-1, 1):
            light += fan(rng, xs, ys, WIDTH * (0.5 + side * rng.uniform(0.18, 0.3)), HEIGHT * 1.02, rng.integers(5, 9),
                         rng.uniform(0.7, 1.1), rng.uniform(70, 110), -side * rng.uniform(0.15, 0.35))
    elif family == "CHORDS":  # one wide fan from the middle
        light += fan(rng, xs, ys, WIDTH * rng.uniform(0.45, 0.55), HEIGHT * 1.05, rng.integers(11, 17),
                     rng.uniform(1.8, 2.4), rng.uniform(60, 90), rng.uniform(-0.1, 0.1))
    elif family == "PLUCK":  # many thin sharp beams, and sparks
        for _ in range(3):
            light += 0.7 * fan(rng, xs, ys, WIDTH * rng.uniform(0.2, 0.8), HEIGHT * 1.02, rng.integers(3, 6),
                               rng.uniform(0.5, 0.9), rng.uniform(140, 200), rng.uniform(-0.5, 0.5))
        for _ in range(140):
            sx, sy = rng.uniform(0, WIDTH), rng.uniform(0, HEIGHT * 0.75)
            light += glow(xs, ys, sx, sy, rng.uniform(0.8, 2.0)) * rng.uniform(0.4, 1.2)
    elif family == "PAD":  # soft wide light in the haze
        light += 0.6 * fan(rng, xs, ys, WIDTH * 0.5, HEIGHT * 1.1, 7, 2.0, 18, 0.0)
        for _ in range(5):
            light += glow(xs, ys, rng.uniform(0, WIDTH), rng.uniform(0, HEIGHT * 0.7), rng.uniform(90, 200)) * 0.35
    else:  # BASS: low heavy light along the floor
        light += 0.8 * fan(rng, xs, ys, WIDTH * rng.uniform(0.3, 0.7), HEIGHT * 1.3, 9, 2.6, 40, 0.0)
        light += glow(xs, ys, WIDTH * 0.5, HEIGHT * 0.95, HEIGHT * 0.35) * 0.8

    # The beams show where the smoke is.
    seen = light * (0.35 + 0.65 * haze)
    image += np.clip(seen, 0, 2)[..., None] * beam * 0.85
    image += np.clip(seen - 0.6, 0, 2)[..., None] * 0.45  # white-hot cores
    image += (haze * 0.06)[..., None] * halo

    # Darker where the name and the knobs sit, and at the sides.
    y = ys / HEIGHT
    x = xs / WIDTH
    shade = 1 - 0.5 * np.clip((y - 0.55) / 0.45, 0, 1)
    side = 1 - 0.35 * ((x - 0.5) * 2) ** 4
    image *= (shade * side)[..., None]

    out = (np.clip(image, 0, 1) * 255).astype(np.uint8)
    path = OUT / f"{index + 1:02d}-{key}.jpg"
    Image.fromarray(out).save(path, quality=86, optimize=True, progressive=True)
    return path


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for index, (key, family) in enumerate(TYPES):
        print(make(index, key, family))


if __name__ == "__main__":
    main()
