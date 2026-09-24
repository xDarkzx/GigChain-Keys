"""Writes examples/demo.gigchain.json: a two-song demo setlist that uses
Arturia and FabFilter plugins installed in the standard VST3 folder.

Run from the repo root:  python examples/make_demo_setlist.py
"""
import json
import pathlib
import uuid

VST3 = "C:/Program Files/Common Files/VST3/"


def slot(path, name):
    return {"pluginId": VST3 + path, "displayName": name, "bypass": False}


def channel(name, instrument, effects=(), volume=0.0, low=0, high=127, transpose=0):
    return {
        "id": str(uuid.uuid4()),
        "name": name,
        "instrument": instrument,
        "effects": list(effects),
        "volumeDb": volume,
        "mute": False,
        "solo": False,
        "keyLow": low,
        "keyHigh": high,
        "transpose": transpose,
        "midiChannel": 0,
    }


def patch(name, *channels):
    return {"id": str(uuid.uuid4()), "name": name, "channels": list(channels)}


def song(name, *patches):
    return {"id": str(uuid.uuid4()), "name": name, "patches": list(patches)}


piano = slot("Arturia/Piano V2.vst3", "Piano V2")
strings = slot("Arturia/Solina V2.vst3", "Solina V2")
pad = slot("Arturia/Jup-8 V4.vst3", "Jup-8 V4")
rhodes = slot("Arturia/Stage-73 V2.vst3", "Stage-73 V2")
organ = slot("Arturia/B-3 V2.vst3", "B-3 V2")
lead = slot("Arturia/Mini V3.vst3", "Mini V3")
reverb = slot("FabFilter/FabFilter Pro-R.vst3", "Pro-R")
eq = slot("FabFilter/FabFilter Pro-Q 3.vst3", "Pro-Q 3")

setlist = {
    "formatVersion": 1,
    "songs": [
        song(
            "Demo Ballad",
            patch("Intro", channel("Piano", piano, [reverb])),
            patch("Verse", channel("Piano", piano, [reverb]), channel("Strings", strings, volume=-8.0)),
            patch(
                "Chorus",
                channel("Piano", piano, [reverb]),
                channel("Strings", strings, volume=-6.0),
                channel("Pad", pad, volume=-10.0),
            ),
        ),
        song(
            "Funk Tune",
            patch("Verse", channel("Rhodes", rhodes, [eq])),
            patch(
                "Chorus",
                channel("Organ (left hand)", organ, high=59),
                channel("Rhodes (right hand)", rhodes, [eq], low=60),
            ),
            patch("Solo", channel("Lead", lead, [reverb], transpose=12, volume=-4.0)),
        ),
    ],
}

out = pathlib.Path(__file__).with_name("demo.gigchain.json")
out.write_text(json.dumps(setlist, indent=4) + "\n", encoding="utf-8")
print("wrote", out)
