#!/usr/bin/env python3
"""The app in other languages (docs/i18n/app.md): Qt's translation files,
src/ui/translations/gigchain_<code>.ts, which lupdate fills with every
marked text and which lrelease builds into the app.

    app_i18n.py check              every language: complete, current and intact
    app_i18n.py check LUPDATE PROJECT
                                   ... and holding every text the source marks
                                   (lupdate and qt_add_translations' project file)
    app_i18n.py todo <code>        the texts still to translate, as JSON:
                                   [{"key", "context", "source", "comment", "plural"}]
    app_i18n.py merge <code> FILE  writes translations ({key: text, or a list
                                   of plural forms, or {"t": ..., "same": true}
                                   for a text that is the same in that language})

A text's key is a fingerprint of its context, English and comment, so a
changed English is a new text. The files are written exactly as lupdate
writes them, so lupdate and this tool never fight over a file.
"""
from __future__ import annotations

import hashlib
import json
import re
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FOLDER = ROOT / "src" / "ui" / "translations"
LANGUAGES = ["hi", "pt_BR", "es", "id", "de", "fr", "ru", "ja", "ko", "zh_CN"]

# Plural forms Qt expects for each language (QTranslator's numerus rules).
PLURALS = {"hi": 2, "pt_BR": 2, "es": 2, "id": 1, "de": 2, "fr": 2, "ru": 3, "ja": 1, "ko": 1, "zh_CN": 1}

# Never translated: kept, letter for letter, wherever the English has them.
KEEP = ["GigChain Keys", "GigChain", "MainStage", "Windows", "macOS", "Linux", "VST3", "VST", "MIDI", "GitHub",
        "Discord", "Buy Me a Coffee", "Piano Pro", "Pads Pro", "Saw Stack Pro", "Sustain Pads", "SoundFont",
        "ASIO", "WASAPI", "PipeWire", "JACK", "PulseAudio", "ALSA", "Core Audio", "OTT", "ChordPro", "Wine",
        "yabridge", "Bluetooth", "USB"]

PLACEHOLDER = re.compile(r"%L?\d{1,2}|%n")
TAG = re.compile(r"</?[A-Za-z][^>]*>")
LETTER = re.compile(r"[A-Za-z]{3,}")


def key_of(context: str, source: str, comment: str) -> str:
    return hashlib.sha1(f"{context}\0{source}\0{comment}".encode("utf-8")).hexdigest()[:12]


@dataclass
class Message:
    context: str
    source: str
    comment: str = ""
    extracomment: str = ""
    plural: bool = False
    forms: list[str] = field(default_factory=list)  # one, or one per plural form
    unfinished: bool = True
    same: bool = False  # (translatorcomment "same": the same words in that language, on purpose)
    translatorcomment: str = ""

    @property
    def key(self) -> str:
        return key_of(self.context, self.source, self.comment)


@dataclass
class TsFile:
    language: str
    contexts: list[tuple[str, list[Message]]]

    def messages(self) -> list[Message]:
        return [m for _, ms in self.contexts for m in ms]


def path_of(code: str) -> Path:
    return FOLDER / f"gigchain_{code}.ts"


def read(code: str) -> TsFile:
    root = ET.parse(path_of(code)).getroot()
    contexts: list[tuple[str, list[Message]]] = []
    for ctx in root.findall("context"):
        name = ctx.findtext("name") or ""
        messages = []
        for m in ctx.findall("message"):
            tr = m.find("translation")
            plural = m.get("numerus") == "yes"
            if plural:
                forms = [f.text or "" for f in tr.findall("numerusform")]
            else:
                forms = [tr.text or ""]
            note = m.findtext("translatorcomment") or ""
            messages.append(Message(context=name, source=m.findtext("source") or "", comment=m.findtext("comment") or "",
                                    extracomment=m.findtext("extracomment") or "", plural=plural, forms=forms,
                                    unfinished=tr.get("type") == "unfinished", same=note == "same",
                                    translatorcomment=note))
        contexts.append((name, messages))
    return TsFile(language=root.get("language") or code, contexts=contexts)


def _esc(text: str) -> str:
    return (text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            .replace('"', "&quot;").replace("'", "&apos;"))


def serialise(ts: TsFile) -> str:
    """As lupdate writes it (no locations, no obsolete texts)."""
    out = ['<?xml version="1.0" encoding="utf-8"?>', "<!DOCTYPE TS>",
           f'<TS version="2.1" language="{ts.language}" sourcelanguage="en">']
    for name, messages in ts.contexts:
        out.append("<context>")
        out.append(f"    <name>{_esc(name)}</name>")
        for m in messages:
            out.append('    <message numerus="yes">' if m.plural else "    <message>")
            out.append(f"        <source>{_esc(m.source)}</source>")
            if m.comment:
                out.append(f"        <comment>{_esc(m.comment)}</comment>")
            if m.extracomment:
                out.append(f"        <extracomment>{_esc(m.extracomment)}</extracomment>")
            if m.translatorcomment:
                out.append(f"        <translatorcomment>{_esc(m.translatorcomment)}</translatorcomment>")
            kind = ' type="unfinished"' if m.unfinished else ""
            if m.plural:
                out.append(f"        <translation{kind}>")
                for form in m.forms:
                    out.append(f"            <numerusform>{_esc(form)}</numerusform>")
                out.append("        </translation>")
            else:
                out.append(f"        <translation{kind}>{_esc(m.forms[0] if m.forms else '')}</translation>")
            out.append("    </message>")
        out.append("</context>")
    out.append("</TS>")
    return "\n".join(out) + "\n"


def write(code: str, ts: TsFile) -> None:
    with open(path_of(code), "w", encoding="utf-8", newline="\n") as f:
        f.write(serialise(ts))


def _without_names(text: str) -> str:
    for name in sorted(KEEP, key=len, reverse=True):
        text = text.replace(name, " ")
    return PLACEHOLDER.sub(" ", TAG.sub(" ", text))


def needs_words(source: str) -> bool:
    """False for a text of names, numbers and placeholders only ("VST3", "%1 Hz"): it may stay as it is."""
    return bool(LETTER.search(_without_names(source)))


def problems_of(code: str, m: Message) -> list[str]:
    where = f"{code} [{m.context}] \"{m.source[:70]}\""
    if m.unfinished or any(not f.strip() for f in m.forms):
        return [f"{where}: not translated"]
    problems = []
    if m.plural and len(m.forms) != PLURALS[code]:
        problems.append(f"{where}: {len(m.forms)} plural forms, {code} has {PLURALS[code]}")
    want = sorted(PLACEHOLDER.findall(m.source))
    for form in m.forms:
        got = sorted(PLACEHOLDER.findall(form))
        # (A language with one plural form may say the number in words only where English has %n: never.)
        if got != want:
            problems.append(f"{where}: placeholders {got}, the English has {want}")
        if sorted(TAG.findall(form)) != sorted(TAG.findall(m.source)):
            problems.append(f"{where}: its tags differ from the English")
        for name in KEEP:
            if name in m.source and name not in form and not any(name in k and k in m.source for k in KEEP if k != name):
                problems.append(f"{where}: \"{name}\" is missing (never translated)")
        if form.strip() == m.source.strip() and needs_words(m.source) and not m.same:
            problems.append(f"{where}: left in English")
        if m.source.endswith("\n") != form.endswith("\n"):
            problems.append(f"{where}: its line ends differ from the English")
    return problems


def check() -> list[str]:
    problems: list[str] = []
    english = None
    for code in LANGUAGES:
        if not path_of(code).exists():
            problems.append(f"{code}: no translation file ({path_of(code).name})")
            continue
        ts = read(code)
        texts = [(m.context, m.source, m.comment) for m in ts.messages()]
        if english is None:
            english = texts
        elif sorted(texts) != sorted(english):
            problems.append(f"{code}: its texts differ from {LANGUAGES[0]}'s (run update_translations)")
        for m in ts.messages():
            problems += problems_of(code, m)
        if serialise(ts) != path_of(code).read_text(encoding="utf-8"):
            problems.append(f"{code}: not as lupdate writes it (run update_translations)")
    return problems


def source_drift(lupdate: str, project: str) -> list[str]:
    """The source's marked texts against the files': lupdate (as update_translations
    runs it) on a copy of one file must leave it as it is."""
    import shutil
    import subprocess
    import tempfile
    code = LANGUAGES[0]
    with tempfile.TemporaryDirectory() as folder:
        copy = Path(folder) / path_of(code).name
        shutil.copyfile(path_of(code), copy)
        run = subprocess.run([lupdate, "-project", project, "-no-obsolete", "-locations", "none", "-ts", str(copy)],
                             capture_output=True, text=True, encoding="utf-8", errors="replace")
        if run.returncode != 0:
            return [f"lupdate failed ({run.returncode}): {run.stderr.strip() or run.stdout.strip()}"]
        if copy.read_text(encoding="utf-8") == path_of(code).read_text(encoding="utf-8"):
            return []
        theirs = {(m.context, m.source, m.comment) for m in read(code).messages()}
        ET_root = ET.parse(copy).getroot()
        now = {(c.findtext("name") or "", m.findtext("source") or "", m.findtext("comment") or "")
               for c in ET_root.findall("context") for m in c.findall("message")}
        new = sorted(now - theirs)
        gone = sorted(theirs - now)
        problems = [f"new in the source, in no translation file: [{c}] \"{s[:70]}\"" for c, s, _ in new[:20]]
        problems += [f"no longer in the source: [{c}] \"{s[:70]}\"" for c, s, _ in gone[:20]]
        problems.append("the translation files are not current with the source: build the update_translations "
                        "target, translate what is new, and commit")
        return problems


def todo(code: str) -> list[dict]:
    return [{"key": m.key, "context": m.context, "source": m.source, "comment": m.comment or m.extracomment,
             "plural": PLURALS[code] if m.plural else 0}
            for m in read(code).messages() if problems_of(code, m)]


def merge(code: str, given: dict) -> list[str]:
    """Writes the given translations; returns what it could not take."""
    ts = read(code)
    by_key = {m.key: m for m in ts.messages()}
    refused = [f"{k}: no such text" for k in given if k not in by_key]
    for key, value in given.items():
        m = by_key.get(key)
        if m is None:
            continue
        same = isinstance(value, dict) and value.get("same", False)
        text = value["t"] if isinstance(value, dict) else value
        forms = text if isinstance(text, list) else [text]
        if m.plural and len(forms) == 1 and PLURALS[code] > 1:
            refused.append(f"{key}: needs {PLURALS[code]} plural forms")
            continue
        m.forms = list(forms)
        m.unfinished = False
        m.same = same
        m.translatorcomment = "same" if same else ""
    write(code, ts)
    return refused


def main(argv: list[str]) -> int:
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8")
    if len(argv) in (2, 4) and argv[1] == "check":
        problems = check()
        if len(argv) == 4:  # check <lupdate> <lupdate project .json>: also against the source
            problems += source_drift(argv[2], argv[3])
        for p in problems[:200]:
            print(p)
        if problems:
            print(f"{len(problems)} problem(s)")
            return 1
        print("every language is complete, current and intact")
        return 0
    if len(argv) == 3 and argv[1] == "todo":
        print(json.dumps(todo(argv[2]), ensure_ascii=False, indent=1))
        return 0
    if len(argv) == 4 and argv[1] == "merge":
        refused = merge(argv[2], json.loads(Path(argv[3]).read_text(encoding="utf-8")))
        for r in refused:
            print(r)
        left = len(todo(argv[2]))
        print(f"{argv[2]}: {left} still to translate")
        return 1 if refused else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
