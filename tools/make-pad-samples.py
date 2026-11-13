"""Makes Pads Pro's sampled sets from free recordings (CC0 / public domain):

  strings  VSCO 2 Community Edition (Versilian Studios, CC0): the violin and
           cello sections, sustained with vibrato (the softer take). Each
           note gets a seamless loop in its held part (a crossfade baked
           in), so it sustains as long as the key is held.
  piano    Splendid Grand Piano (AKAI, public domain; mapped by kinwie):
           its Mp layer, as recorded (it rings and fades as a piano does).

    python tools/make-pad-samples.py <source folder> <out folder>

<source> holds what the recordings were downloaded to ("Violin Section",
"Cello Section", "Piano/Samples" with "Piano/MP.txt"). Writes <out>/<set>/
*.wav (16-bit) and <out>/<set>.txt: one line a note, "file root loopStart
loopEnd" (loop 0 0: none). They go in the app's sound pack as pads/
(cmake/PianoSamples.cmake): the app reads them from samples/pads.
"""
import glob
import os
import re
import sys

import numpy as np
import soundfile as sf

NOTES = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}


def midi(name):
    """VSCO's and Splendid's names put middle C (60) at C3."""
    m = re.match(r"([A-G]#?)(-?\d+)", name)
    return 12 * (int(m.group(2)) + 2) + NOTES[m.group(1)]


def level(x):
    return np.sqrt(np.mean(x ** 2) + 1e-20)


def detect_hz(mono, rate):
    """The pitch of the steadiest part (autocorrelation), to check the names."""
    seg = mono[int(1.0 * rate):int(1.0 * rate) + 8192]
    seg = seg - seg.mean()
    ac = np.correlate(seg, seg, "full")[len(seg) - 1:]
    lo, hi = int(rate / 1200), int(rate / 50)
    lag = lo + int(np.argmax(ac[lo:hi]))
    return rate / lag


def loop_points(x, rate, root):
    """A loop in the held part: after the bow has settled (1 s), before the
    note dies away; up to 4 s long, its end crossfaded into what leads to its
    start, so it runs on seamlessly."""
    mono = x.mean(axis=1)
    hop = int(0.05 * rate)
    env = np.array([level(mono[i:i + hop]) for i in range(0, len(mono) - hop, hop)])
    held = np.median(env[int(1.0 / 0.05):]) if len(env) > 40 else env.max()
    end = len(env) - 1
    while end > 0 and env[end] < held * 0.5:  # (-6 dB: the release)
        end -= 1
    a = int(1.0 * rate)
    b = min(end * hop, a + int(4.0 * rate))
    # The loop a whole number of the note's periods long (no jump in pitch).
    period = rate / (440.0 * 2 ** ((root - 69) / 12))
    b = a + int(round(int((b - a) / period) * period))
    fade = int(0.35 * rate)
    y = x.copy()
    for ch in range(y.shape[1]):
        t = np.linspace(0.0, 1.0, fade)
        w_in = np.sin(0.5 * np.pi * t)
        w_out = np.cos(0.5 * np.pi * t)
        y[b - fade:b, ch] = x[b - fade:b, ch] * w_out + x[a - fade:a, ch] * w_in
    return y[:b], a, b


def ring_on(x, rate, root):
    """A piano note that rings on while held (a pad holds its chord under a
    whole song): its strike and first ring as recorded (low notes longer),
    then a loop near there, whole periods long, its fading taken out of it
    and its end crossfaded into what leads to its start, so the ring goes
    on, steady, as long as the key is held."""
    place = min(max((root - 21) / 87.0, 0.0), 1.0)
    keep = min(int((5.0 - 2.8 * place) * rate), len(x) - int(0.05 * rate))
    period = rate / (440.0 * 2 ** ((root - 69) / 12))
    length = int(round(max(1, round(0.5 * rate / period)) * period))
    length = min(length, keep // 3)
    b = keep
    a = b - length
    fade = int(length * 0.45)
    start = a - fade
    # How fast it was fading there (dB a second): taken out from `start` on.
    mono = x.mean(axis=1)
    hop = int(0.02 * rate)
    seg = mono[max(0, b - int(1.5 * rate)):b]
    env = 20 * np.log10(np.sqrt(np.mean(seg[: len(seg) // hop * hop].reshape(-1, hop) ** 2, axis=1)) + 1e-9)
    slope = -np.polyfit(np.arange(len(env)) * hop / rate, env, 1)[0] if len(env) > 10 else 0.0
    slope = float(np.clip(slope, 0.0, 30.0))
    y = x[:b].copy()
    t = (np.arange(len(y)) - start) / rate
    y *= np.where(t > 0, 10 ** (slope * t / 20), 1.0)[:, None]
    w = np.linspace(0, 1, fade, endpoint=False)
    y[b - fade:b] = y[b - fade:b] * np.cos(w * np.pi / 2)[:, None] + y[a - fade:a] * np.sin(w * np.pi / 2)[:, None]
    return y, a, b


def write(out, name, x, rate):
    sf.write(os.path.join(out, name), np.clip(x, -1.0, 1.0), rate, subtype="PCM_16")


def strings(src, out):
    folder = os.path.join(out, "strings")
    os.makedirs(folder, exist_ok=True)
    lines = []
    zones = []
    # The cellos below the violins' lowest note (G3, 55), the violins from there up.
    for f in glob.glob(os.path.join(src, "Cello Section", "*.wav")):
        root = midi(os.path.basename(f).split("_")[1])
        if root < 55:
            zones.append((f, root, "cello"))
    for f in glob.glob(os.path.join(src, "Violin Section", "*.wav")):
        zones.append((f, midi(os.path.basename(f).split("_")[2]), "violin"))
    for f, root, kind in sorted(zones, key=lambda z: z[1]):
        x, rate = sf.read(f, always_2d=True)
        hz = detect_hz(x.mean(axis=1), rate)
        expected = 440.0 * 2 ** ((root - 69) / 12)
        off = 1200 * np.log2(hz / expected)
        off = (off + 600) % 1200 - 600  # (octave errors of the detector aside)
        y, a, b = loop_points(x, rate, root)
        y = y / level(y[a:b].mean(axis=1)) * 0.12  # (every note as loud: -18 dBFS held)
        name = f"{kind}-{root}.wav"
        write(folder, name, y, rate)
        lines.append(f"{name} {root} {a} {b}")
        print(f"{name}: {os.path.basename(f)}, {off:+.0f} cents from its name, loop {a / rate:.2f}-{b / rate:.2f} s")
    with open(os.path.join(out, "strings.txt"), "w") as fh:
        fh.write("\n".join(lines) + "\n")


def piano(src, out):
    folder = os.path.join(out, "piano")
    os.makedirs(folder, exist_ok=True)
    text = open(os.path.join(src, "Piano", "MP.txt"), encoding="utf-8-sig").read().replace("\n", "")
    lines = []
    loudest = 0.0
    takes = []
    for m in re.finditer(r"pitch_keycenter=(\d+).*?sample=(Mp [^$]+)\$EXT", text):
        root = int(m.group(1))
        x, rate = sf.read(os.path.join(src, "Piano", "Samples", m.group(2) + "flac"), always_2d=True)
        y, a, b = ring_on(x, rate, root)
        takes.append((root, y, rate, a, b))
        loudest = max(loudest, np.abs(y).max())
    for root, x, rate, a, b in sorted(takes, key=lambda t: t[0]):
        name = f"piano-{root}.wav"
        write(folder, name, x * (0.7 / loudest), rate)  # (one gain for all: the piano's own balance kept)
        lines.append(f"{name} {root} {a} {b}")
    with open(os.path.join(out, "piano.txt"), "w") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"piano: {len(lines)} notes")


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    strings(src, out)
    piano(src, out)


if __name__ == "__main__":
    main()
