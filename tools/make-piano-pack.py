"""Makes Piano Pro's piano pack: its recorded pianos and electric pianos, from
free recordings, as the app plays them.

  Concert Grand   Salamander Grand Piano V3, Yamaha C5 (Alexander Holm, CC BY 3.0)
  Upright         VCSL's Knight upright (Versilian Studios, CC0)
  Rhodes          jRhodes3d, a 1977 Rhodes Mark I Stage 73 (Jeff Learman: see its LICENSE)
  Wurlitzer       Greg Sullivan's Wurlitzer EP200 (CC BY 3.0)
  Electric Grand  Greg Sullivan's Yamaha CP80 (CC BY 3.0)
  Pianet          Greg Sullivan's Hohner Pianet T (CC BY 3.0)

    python tools/make-piano-pack.py <sources> <out>

<sources> holds them unpacked: SalamanderGrandPianoV3_44.1khz16bit/,
vcsl/ (VCSL_Keys.zip's "Upright Piano, Knight" and its .sfz), jlearman.jRhodes3d-master/ and
GregSullivan.E-Pianos-master/.

Each recording is kept as long as it says something (low notes longer than
high), then loops: a stretch near its end, whole periods of its note long,
its end crossfaded into what leads to its start and its own fading taken
out, so a held or pedalled note rings on as long as it is held; the app
fades it as the recording was fading (tail_decay, dB a second). Writes
<out>/<name>.sfz (only the settings the app reads) and <out>/<name>/*.flac
(16-bit), and <out>/CREDITS.txt.
"""
import math
import os
import re
import shutil
import sys

import numpy as np
import soundfile as sf

NOTE_STEPS = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}


def note_number(text):
    """A key as SFZ writes it: a number, or a name with middle C (60) at C4."""
    text = text.strip()
    if re.fullmatch(r"-?\d+", text):
        return int(text)
    m = re.fullmatch(r"([A-Ga-g])([#b]?)(-?\d+)", text)
    if not m:
        raise ValueError(f"not a key: {text!r}")
    step = NOTE_STEPS[m.group(1).lower()] + {"#": 1, "b": -1, "": 0}[m.group(2)]
    return 12 * (int(m.group(3)) + 1) + step


def read_sfz(path):
    """The regions of an SFZ map, each a dict of its settings (headers'
    settings passed down), its sample's full path under "file"."""
    text = open(path, encoding="utf-8", errors="replace").read()
    defines = {}
    for name, value in re.findall(r"^\s*#define\s+(\$\w+)\s+(\S+)", text, re.M):
        defines[name] = value
    text = re.sub(r"^\s*#.*$", "", text, flags=re.M)
    for name in sorted(defines, key=len, reverse=True):
        text = text.replace(name, defines[name])
    text = re.sub(r"//[^\n]*", "", text)
    folder = os.path.dirname(path)
    default_path = ""
    levels = {"global": {}, "master": {}, "group": {}}
    regions = []
    current = None
    header = None
    for token in re.split(r"(<\w+>)", text):
        m = re.fullmatch(r"<(\w+)>", token)
        if m:
            header = m.group(1)
            if header == "global":
                levels = {"global": {}, "master": {}, "group": {}}
                current = levels["global"]
            elif header == "master":
                levels["master"], levels["group"] = {}, {}
                current = levels["master"]
            elif header == "group":
                levels["group"] = {}
                current = levels["group"]
            elif header == "region":
                current = dict(levels["global"])
                current.update(levels["master"])
                current.update(levels["group"])
                regions.append(current)
            else:  # <control>, <curve>: read for default_path only
                current = {}
            continue
        if current is None:
            continue
        # key=value pairs; a sample's name may hold spaces (up to the next key=).
        for key, value in re.findall(r"(\w+)=(.*?)(?=\s+\w+=|\s*$)", token.strip(), re.S):
            value = value.strip()
            if header == "control":
                if key == "default_path":
                    default_path = value.replace("\\", "/")
                continue
            current[key] = value
    out = []
    for r in regions:
        if "sample" not in r:
            continue
        r["file"] = os.path.normpath(os.path.join(folder, default_path, r["sample"].replace("\\", "/")))
        out.append(r)
    return out


def key_range(r):
    center = note_number(r.get("pitch_keycenter", r.get("key", "60")))
    lo = note_number(r.get("lokey", r.get("key", str(center))))
    hi = note_number(r.get("hikey", r.get("key", str(center))))
    return lo, hi, center


def level_db(x):
    return 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-12)


def envelope_db(mono, rate, hop_s=0.02):
    hop = max(1, int(hop_s * rate))
    n = len(mono) // hop
    if n == 0:
        return np.array([]), hop
    frames = mono[: n * hop].reshape(n, hop)
    return 20 * np.log10(np.sqrt(np.mean(frames ** 2, axis=1)) + 1e-9), hop


def loop_recording(x, rate, freq, keep_s, loop_s):
    """The recording cut to keep_s with a loop at its end (see the top);
    returns (frames, loop_start, loop_end, tail_start, tail_decay) or, when
    it ends by itself before then, (frames, None, ...)."""
    keep = int(keep_s * rate)
    if len(x) <= keep + int(0.3 * rate):
        return x, None, None, None, 0.0
    period = rate / freq
    # Whole periods, as near a whole number of frames as can be (the loop's
    # length must be whole frames).
    want = loop_s * rate / period
    best = None
    for n in range(max(1, int(want * 0.8)), int(want * 1.2) + 2):
        err = abs(n * period - round(n * period))
        if best is None or err < best[0] - 1e-9:
            best = (err, n)
    length = int(round(best[1] * period))
    loop_end = keep
    loop_start = loop_end - length
    xf = int(length * 0.45)
    tail_start = loop_start - xf
    # How fast it was fading over its last stretch (dB a second).
    mono = x.mean(axis=1)
    env, hop = envelope_db(mono[max(0, loop_end - int(1.5 * rate)):loop_end], rate)
    decay = 0.0
    if len(env) > 10:
        t = np.arange(len(env)) * hop / rate
        decay = -np.polyfit(t, env, 1)[0]
    # A struck string fades in two stages: quickly at first, then a long,
    # slower ring (the aftersound). The loop is that ring: slower than
    # the stretch it was measured on.
    decay = float(np.clip(decay * 0.6, 0.3, 10.0))
    # Its fading taken out from the tail on (the app puts it back).
    pad = 256  # past the loop's end: its start again (the resampling to the playing rate eats a little of a file's end)
    y = x[: loop_end + pad].copy()
    t = (np.arange(len(y)) - tail_start) / rate
    lift = np.where(t > 0, 10 ** (decay * t / 20), 1.0)
    y *= lift[:, None]
    # The loop's end crossfaded (equal power) into what leads to its start.
    w = np.linspace(0, 1, xf, endpoint=False)
    fade_out = np.cos(w * np.pi / 2)[:, None]
    fade_in = np.sin(w * np.pi / 2)[:, None]
    y[loop_end - xf:loop_end] = y[loop_end - xf:loop_end] * fade_out + y[loop_start - xf:loop_start] * fade_in
    # Past the loop's end: its start again (read by the interpolation).
    y[loop_end:loop_end + pad] = y[loop_start:loop_start + pad]
    return y, loop_start, loop_end, tail_start, decay


def fewer_layers(group, keep):
    """`keep` of a note's loudness layers, spread from the softest to the
    loudest, each covering the ones left out below it."""
    group = sorted(group, key=lambda r: int(r.get("lovel", "0")))
    if len(group) <= keep:
        return group
    picks = [round(j * (len(group) - 1) / (keep - 1)) for j in range(keep)]
    kept = []
    for j, p in enumerate(picks):
        r = dict(group[p])
        r["lovel"] = group[0].get("lovel", "0") if j == 0 else str(int(group[picks[j - 1] + 1].get("lovel", "0")))
        r["hivel"] = group[p].get("hivel", "127")
        kept.append(r)
    return kept


SETS = [
    # name, map, which regions, layers kept, keep seconds (lowest key, highest), loop seconds, velocity tracking, release regions
    {"name": "Concert Grand", "sfz": "SalamanderGrandPianoV3_44.1khz16bit/SalamanderGrandPianoV3.sfz",
     "attack": lambda r: r.get("trigger") != "release" and "lokey" in r and int(r["lokey"]) >= 0,
     "release": lambda r: r.get("trigger") == "release" and os.path.basename(r["file"]).startswith("rel"),
     "layers": 10, "keep": (5.0, 2.2), "loop": 0.6, "veltrack": 73},
    {"name": "Upright", "sfz": "vcsl/Upright Piano, Knight.sfz",
     "attack": lambda r: "/Sustains/" in r["file"].replace("\\", "/"),
     "release": lambda r: "/Releases/" in r["file"].replace("\\", "/"),
     "layers": 2, "keep": (4.5, 2.0), "loop": 0.6, "veltrack": 88, "rr": 1},
    {"name": "Rhodes", "sfz": "jlearman.jRhodes3d-master/jRhodes3d-st-no-xfade.sfz",
     "attack": lambda r: True, "release": lambda r: False,
     "layers": 5, "keep": (4.5, 2.0), "loop": 0.5, "veltrack": 70},
    {"name": "Wurlitzer", "sfz": "GregSullivan.E-Pianos-master/Wurlitzer EP200/Wurlitzer EP200.sfz",
     "attack": lambda r: True, "release": lambda r: False,
     "layers": 4, "keep": (4.0, 1.8), "loop": 0.4, "veltrack": 70},
    {"name": "Electric Grand", "sfz": "GregSullivan.E-Pianos-master/CP80/CP80.sfz",
     "attack": lambda r: True, "release": lambda r: False,
     "layers": 4, "keep": (4.5, 2.0), "loop": 0.5, "veltrack": 70},
    {"name": "Pianet", "sfz": "GregSullivan.E-Pianos-master/Pianet T/Pianet T.sfz",
     "attack": lambda r: True, "release": lambda r: False,
     "layers": 4, "keep": (3.0, 1.5), "loop": 0.4, "veltrack": 70},
]


def make(spec, sources, out):
    regions = read_sfz(os.path.join(sources, spec["sfz"]))
    attacks = [r for r in regions if spec["attack"](r) and not spec["release"](r)]
    releases = [r for r in regions if spec["release"](r)]
    # Round robins: only the first (memory goes to more layers and longer notes).
    if spec.get("rr"):
        attacks = [r for r in attacks if r.get("seq_position", "1") == "1"]
        releases = [r for r in releases if r.get("seq_position", "1") == "1"]
    notes = {}
    for r in attacks:
        notes.setdefault(key_range(r), []).append(r)
    kept = []
    for key, group in sorted(notes.items()):
        kept += fewer_layers(group, spec["layers"])
    folder = os.path.join(out, spec["name"])
    os.makedirs(folder, exist_ok=True)
    lines = [f"// {spec['name']}: made by tools/make-piano-pack.py from {spec['sfz']}",
             f"<global> amp_veltrack={spec['veltrack']}", "<group> trigger=attack"]
    done = {}
    lows, highs = spec["keep"]
    for r in kept:
        lo, hi, center = key_range(r)
        tune = float(r.get("tune", "0"))
        freq = 440.0 * 2 ** ((center - 69 + tune / 100.0) / 12.0)
        place = min(max((center - 21) / 87.0, 0.0), 1.0)
        keep_s = lows + (highs - lows) * place
        x, rate = sf.read(r["file"], dtype="float64", always_2d=True)
        if x.shape[1] == 1:
            x = np.repeat(x, 2, axis=1)
        x = x[:, :2]
        offset = int(r.get("offset", "0"))
        x = x[offset:]
        y, ls, le, ts, decay = loop_recording(x, rate, freq, keep_s, spec["loop"])
        name = f"{len(done):03d}.flac"
        done[name] = r["file"]
        sf.write(os.path.join(folder, name), np.clip(y, -1, 1), rate, subtype="PCM_16")
        settings = [f"lokey={lo}", f"hikey={hi}", f"pitch_keycenter={center}",
                    f"lovel={max(1, int(r.get('lovel', '1')))}", f"hivel={int(r.get('hivel', '127'))}"]
        if tune:
            settings.append(f"tune={tune:g}")
        vol = float(r.get("volume", "0")) + float(r.get("global_volume", "0")) + float(r.get("master_volume", "0")) + float(r.get("group_volume", "0"))
        if vol:
            settings.append(f"volume={vol:.2f}")
        if ls is not None:
            settings += [f"loop_start={ls}", f"loop_end={le}", f"tail_start={ts}", f"tail_decay={decay:.2f}"]
        # (The sample last: a name runs to the end of its line.)
        lines.append("<region> " + " ".join(settings) + f" sample={spec['name']}/{name}")
    if releases:
        lines.append("<group> trigger=release")
        for r in releases:
            lo, hi, center = key_range(r)
            if r.get("pitch_keytrack") == "0":
                center = lo  # one recording a key, not tuned to it
            x, rate = sf.read(r["file"], dtype="float64", always_2d=True)
            if x.shape[1] == 1:
                x = np.repeat(x, 2, axis=1)
            x = x[: int(2.0 * rate), :2]
            name = f"r{len(done):03d}.flac"
            done[name] = r["file"]
            sf.write(os.path.join(folder, name), np.clip(x, -1, 1), rate, subtype="PCM_16")
            vol = float(r.get("volume", "0")) + float(r.get("global_volume", "0"))
            settings = [f"lokey={lo}", f"hikey={hi}", f"pitch_keycenter={center}",
                        f"lovel={max(1, int(r.get('lovel', '1')))}", f"hivel={int(r.get('hivel', '127'))}", f"volume={vol:.2f}"]
            if "rt_decay" in r:
                settings.append(f"rt_decay={float(r['rt_decay']):g}")
            if "ampeg_release" in r:
                settings.append(f"ampeg_release={float(r['ampeg_release']):g}")
            lines.append("<region> " + " ".join(settings) + f" sample={spec['name']}/{name}")
    with open(os.path.join(out, spec["name"] + ".sfz"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    size = sum(os.path.getsize(os.path.join(folder, n)) for n in os.listdir(folder))
    print(f"{spec['name']}: {len(kept)} recordings, {len(releases)} release sounds, {size / 1e6:.1f} MB")


CREDITS = """Piano Pro's piano pack (made by tools/make-piano-pack.py in GigChain Keys)

Concert Grand: Salamander Grand Piano V3 (Yamaha C5) by Alexander Holm,
  Creative Commons Attribution 3.0 (https://creativecommons.org/licenses/by/3.0/).
  https://freepats.zenvoid.org/Piano/acoustic-grand-piano.html
Upright: Versilian Community Sample Library (VCSL Keys) by
  Versilian Studios LLC, CC0 1.0 (public domain). https://github.com/sgossner/VCSL
Rhodes: jRhodes3d (1977 Rhodes Mark I Stage 73) by Jeff Learman: samples
  CC BY-NC 4.0 (https://creativecommons.org/licenses/by-nc/4.0/), music made
  with it CC0. https://github.com/sfzinstruments/jlearman.jRhodes3d
Wurlitzer, Electric Grand (Yamaha CP80) and Pianet (Hohner Pianet T): Greg
  Sullivan's E-Pianos (http://www.sullivang.net/), mapped by kinwie, Creative
  Commons Attribution 3.0. https://github.com/sfzinstruments/GregSullivan.E-Pianos

The recordings here are trimmed, looped and re-encoded from those (the
changes: see tools/make-piano-pack.py).
"""


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    sources, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    only = os.environ.get("ONLY")
    for spec in SETS:
        if only and spec["name"] != only:
            continue
        shutil.rmtree(os.path.join(out, spec["name"]), ignore_errors=True)
        make(spec, sources, out)
    with open(os.path.join(out, "CREDITS.txt"), "w", encoding="utf-8") as f:
        f.write(CREDITS)


if __name__ == "__main__":
    main()
