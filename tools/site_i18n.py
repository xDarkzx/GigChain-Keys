#!/usr/bin/env python3
"""The website in other languages (docs/i18n/website.md).

English pages in site/ are the source. Each language's translations live in
site/i18n/<folder>.json: every phrase's English and its translation, keyed
by a fingerprint of the English. This tool finds the phrases a reader sees,
writes every page in every language, and checks the translations.

  python3 tools/site_i18n.py build <out>    every page, every language, the sitemap (checks first)
  python3 tools/site_i18n.py check          the translations, against the pages as they are now
  python3 tools/site_i18n.py todo <folder>  the phrases that language still needs, as JSON
  python3 tools/site_i18n.py phrases        every phrase of the translated pages, as JSON
  python3 tools/site_i18n.py merge <folder> <file>  files {key: translation} into site/i18n/<folder>.json

Only the standard library: it runs as it is on GitHub's machines.
"""
from __future__ import annotations

import hashlib
import html
import json
import re
import sys
import urllib.parse
from dataclasses import dataclass, field
from html.parser import HTMLParser
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SITE = ROOT / "site"
I18N = SITE / "i18n"
BASE = "https://xdarkzx.github.io/GigChain-Keys/"

# folder, hreflang, the language's own name, and the one-line offer shown to
# a reader whose browser prefers it ({name}: its own name).
LANGUAGES = [
    ("hi", "hi", "हिन्दी", "यह पेज हिन्दी में भी है।", "हिन्दी में पढ़ें", "बंद करें"),
    ("pt-br", "pt-BR", "Português", "Esta página também está em português.", "Ver em português", "Fechar"),
    ("es", "es", "Español", "Esta página también está en español.", "Ver en español", "Cerrar"),
    ("id", "id", "Bahasa Indonesia", "Halaman ini juga tersedia dalam Bahasa Indonesia.", "Baca dalam Bahasa Indonesia", "Tutup"),
    ("de", "de", "Deutsch", "Diese Seite gibt es auch auf Deutsch.", "Auf Deutsch lesen", "Schließen"),
    ("fr", "fr", "Français", "Cette page existe aussi en français.", "Lire en français", "Fermer"),
    ("ru", "ru", "Русский", "Эта страница есть и на русском языке.", "Читать на русском", "Закрыть"),
    ("ja", "ja", "日本語", "このページは日本語でもご覧いただけます。", "日本語で見る", "閉じる"),
    ("ko", "ko", "한국어", "이 페이지는 한국어로도 볼 수 있습니다.", "한국어로 보기", "닫기"),
    ("zh-cn", "zh-Hans", "简体中文", "本页面也有简体中文版。", "查看中文版", "关闭"),
]
FOLDERS = [lang[0] for lang in LANGUAGES]
# The language menu's last item on a translated page: a wording that reads
# wrong is reported as an issue, titled with its language.
REPORT = {
    "hi": "अनुवाद में गलती बताएँ", "pt-br": "Informar um erro de tradução", "es": "Informar un error de traducción",
    "id": "Laporkan kesalahan terjemahan", "de": "Übersetzungsfehler melden", "fr": "Signaler une erreur de traduction",
    "ru": "Сообщить об ошибке перевода", "ja": "翻訳の誤りを報告", "ko": "번역 오류 신고", "zh-cn": "报告翻译问题",
}
ISSUES = "https://github.com/xDarkzx/GigChain-Keys/issues/new"
# The pages that are translated; the others (comparisons with other hosts,
# for English searches) stay English, and translated pages link to them.
TRANSLATED = ["index.html", "support.html", "mainstage-for-windows.html", "free-mainstage-alternatives.html",
              "play-vst-plugins-live.html"]
# Names that a translation must keep when its English has them.
KEEP = ["GigChain Keys", "MainStage", "Windows", "macOS", "Linux", "VST3", "MIDI", "GitHub", "Discord",
        "Buy Me a Coffee", "Piano Pro", "Pads Pro", "Saw Stack Pro"]

INLINE = {"a", "b", "strong", "em", "i", "span", "code", "small", "br", "sup", "sub", "abbr", "kbd", "mark", "s",
          "u", "img", "wbr", "time"}
VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr"}
SKIP = {"script", "style", "svg", "noscript", "template", "textarea", "pre"}
TEXT_ATTRS = ("alt", "title", "aria-label", "placeholder")
META_NAMES = {"description", "twitter:title", "twitter:description", "twitter:image:alt"}
META_PROPERTIES = {"og:title", "og:description", "og:image:alt"}
LETTER = re.compile(r"[^\W\d_]", re.UNICODE)
PLACEHOLDER = re.compile(r"\{\w+\}")


# ------------------------------------------------------------------ parsing
@dataclass
class Node:
    tag: str | None  # None: text
    start: int = 0  # the node's first character (an element: its '<')
    inner_start: int = 0  # an element: just after its start tag
    inner_end: int = 0  # an element: its end tag's '<'
    end: int = 0  # just past the node
    attrs: dict = field(default_factory=dict)
    raw_tag: str = ""  # the start tag as written
    children: list = field(default_factory=list)
    parent: "Node | None" = None
    has_text: bool = False  # text with a letter in it, anywhere inside


class Tree(HTMLParser):
    """A page as nodes with their places in the source: what is replaced is
    exactly the source between them, so everything else stays as written."""

    def __init__(self, source: str):
        super().__init__(convert_charrefs=False)
        self.source = source
        self.lines = [0]
        for i, ch in enumerate(source):
            if ch == "\n":
                self.lines.append(i + 1)
        self.root = Node(tag="#root", start=0, inner_start=0, end=len(source), inner_end=len(source))
        self.stack = [self.root]
        self.feed(source)
        self.close()
        while len(self.stack) > 1:  # unclosed elements end where the page does
            node = self.stack.pop()
            node.inner_end = node.end = len(source)

    def at(self) -> int:  # (not "offset": HTMLParser has its own)
        line, col = self.getpos()
        return self.lines[line - 1] + col

    def handle_starttag(self, tag, attrs):
        start = self.at()
        raw = self.get_starttag_text() or ""
        node = Node(tag=tag, start=start, inner_start=start + len(raw), attrs=dict(attrs), raw_tag=raw,
                    parent=self.stack[-1])
        self.stack[-1].children.append(node)
        if tag in VOID:
            node.inner_end = node.end = node.inner_start
        else:
            self.stack.append(node)

    def handle_startendtag(self, tag, attrs):
        start = self.at()
        raw = self.get_starttag_text() or ""
        node = Node(tag=tag, start=start, inner_start=start + len(raw), inner_end=start + len(raw),
                    end=start + len(raw), attrs=dict(attrs), raw_tag=raw, parent=self.stack[-1])
        self.stack[-1].children.append(node)

    def handle_endtag(self, tag):
        at = self.at()
        close = self.source.index(">", at) + 1
        for i in range(len(self.stack) - 1, 0, -1):
            if self.stack[i].tag == tag:
                while len(self.stack) > i:
                    node = self.stack.pop()
                    node.inner_end = at if node.tag == tag else at
                    node.end = close if node.tag == tag else at
                return

    def _text(self, length_of):
        start = self.at()
        node = Node(tag=None, start=start, end=start + length_of, parent=self.stack[-1])
        node.inner_start, node.inner_end = node.start, node.end
        self.stack[-1].children.append(node)
        return node

    def handle_data(self, data):
        node = self._text(len(data))
        if LETTER.search(data) and not self._in_skip():
            node.has_text = True

    # (&amp; &#8212; &#x2014;: as written, with or without the semicolon)
    REF = re.compile(r"&#?[xX]?[0-9a-zA-Z.-]+;?")

    def handle_entityref(self, name):
        self._text(len(self.REF.match(self.source, self.at()).group(0)))

    def handle_charref(self, name):
        self._text(len(self.REF.match(self.source, self.at()).group(0)))

    def _in_skip(self):
        return any(n.tag in SKIP for n in self.stack)


def mark_text(node: Node) -> bool:
    for child in node.children:
        if mark_text(child):
            node.has_text = True
    return node.has_text


def all_inline(node: Node) -> bool:
    return all(child.tag is None or (child.tag in INLINE and all_inline(child)) for child in node.children)


# ------------------------------------------------------------------ phrases
@dataclass
class Phrase:
    english: str  # as written in the source (tags and entities kept), whitespace tidied
    start: int  # what is replaced in the source
    end: int
    kind: str  # "html", "attr", "json"


def tidy(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def key_of(english: str) -> str:
    return hashlib.sha1(tidy(english).encode("utf-8")).hexdigest()[:12]


def _trimmed(source: str, start: int, end: int) -> tuple[int, int]:
    while start < end and source[start].isspace():
        start += 1
    while end > start and source[end - 1].isspace():
        end -= 1
    return start, end


def _no_translate(node: Node) -> bool:
    return node.attrs.get("translate") == "no" or "notranslate" in (node.attrs.get("class") or "").split()


def phrases_of(source: str) -> list[Phrase]:
    tree = Tree(source)
    mark_text(tree.root)
    found: list[Phrase] = []

    def add_run(run: list[Node]):
        if not any(n.has_text for n in run):
            return
        # Links side by side with only spaces between them (a menu): each one
        # on its own. A sentence with a link in it stays one phrase.
        loose_text = any(n.tag is None and source[n.start:n.end].strip() for n in run)
        pieces = [n for n in run if n.tag is not None and n.has_text]
        if not loose_text and len(pieces) > 1:
            for n in pieces:
                found.append(Phrase(tidy(source[n.start:n.end]), n.start, n.end, "html"))
            return
        start, end = _trimmed(source, run[0].start, run[-1].end)
        if start < end:
            found.append(Phrase(tidy(source[start:end]), start, end, "html"))

    def walk(node: Node):
        if node.tag in SKIP or _no_translate(node):
            return
        if node.tag not in (None, "#root") and node.tag not in INLINE and node.has_text and all_inline(node):
            if node.children:
                add_run(node.children)  # (a menu of links: each link on its own)
            return
        run: list[Node] = []
        for child in node.children:
            if child.tag is None or (child.tag in INLINE and all_inline(child) and not _no_translate(child)):
                run.append(child)
                continue
            if run:
                add_run(run)
                run = []
            walk(child)
        if run:
            add_run(run)

    walk(tree.root)

    # Attributes a reader sees (or hears), and the page's description.
    def attributes(node: Node):
        for child in node.children:
            if child.tag is None:
                continue
            wanted = [a for a in TEXT_ATTRS if child.attrs.get(a)]
            if child.tag == "meta" and (child.attrs.get("name") in META_NAMES or child.attrs.get("property") in META_PROPERTIES):
                wanted.append("content")
            for name in wanted:
                value = child.attrs.get(name) or ""
                if not LETTER.search(value):
                    continue
                match = re.search(r"\b" + re.escape(name) + r"\s*=\s*(\"([^\"]*)\"|'([^']*)')", child.raw_tag)
                if match:
                    group = 2 if match.group(2) is not None else 3
                    start = child.start + match.start(group)
                    found.append(Phrase(tidy(match.group(group)), start, start + len(match.group(group)), "attr"))
            if child.tag not in SKIP:
                attributes(child)

    attributes(tree.root)

    # Structured data (the FAQ search engines show) and the script's own words.
    def scripts(node: Node):
        for child in node.children:
            if child.tag == "script" and child.attrs.get("type") in ("application/ld+json", "application/json"):
                start, end = _trimmed(source, child.inner_start, child.inner_end)
                if start < end:
                    found.append(Phrase(source[start:end], start, end, "json"))
            elif child.tag is not None:
                scripts(child)

    scripts(tree.root)
    found.sort(key=lambda p: p.start)
    return found


# JSON blocks: which strings in them are read by people.
def json_texts(text: str, script_words: bool) -> list[str]:
    data = json.loads(text)
    out: list[str] = []

    def visit(value, parent_type=None, key=None):
        if isinstance(value, dict):
            kind = value.get("@type")
            for k, v in value.items():
                visit(v, kind or parent_type, k)
        elif isinstance(value, list):
            for v in value:
                visit(v, parent_type, key)
        elif isinstance(value, str) and LETTER.search(value) and _json_wanted(key, parent_type, script_words):
            out.append(value)

    visit(data)
    return out


def _json_wanted(key, parent_type, script_words) -> bool:
    if script_words:
        return True
    if key in ("description", "text", "applicationSubCategory", "featureList", "alternativeHeadline"):
        return True
    return key == "name" and parent_type in ("Question", "ListItem", "HowToStep", "HowTo")


def translate_json(text: str, script_words: bool, translate, lang_url) -> str:
    data = json.loads(text)

    def visit(value, parent_type=None, key=None):
        if isinstance(value, dict):
            kind = value.get("@type")
            return {k: visit(v, kind or parent_type, k) for k, v in value.items()}
        if isinstance(value, list):
            return [visit(v, parent_type, key) for v in value]
        if isinstance(value, str):
            if LETTER.search(value) and _json_wanted(key, parent_type, script_words):
                return translate(value)
            if key in ("item", "url", "@id") and value.startswith(BASE):
                return lang_url(value)
        return value

    indent = "  " if "\n" in text else None
    return json.dumps(visit(data), ensure_ascii=False, indent=indent)


def page_phrases(source: str) -> list[str]:
    """Every English phrase of a page, in order, as the translation files hold them."""
    texts: list[str] = []
    for p in phrases_of(source):
        if p.kind == "json":
            script_words = '"@context"' not in p.english
            texts += [tidy(t) for t in json_texts(p.english, script_words)]
        else:
            texts.append(p.english)
    return texts


# ------------------------------------------------------------------ translations
def load(folder: str) -> dict:
    path = I18N / f"{folder}.json"
    if not path.exists():
        return {}
    return json.loads(path.read_text(encoding="utf-8")).get("phrases", {})


def names_only(english: str) -> bool:
    """A phrase of names that are never translated ("Windows", "Piano Pro"): kept as it is."""
    return not LETTER.search(_only_words(english))


def wanted_phrases() -> dict[str, str]:
    """key -> English, for every phrase of the translated pages that needs translating."""
    out: dict[str, str] = {}
    for page in TRANSLATED:
        for text in page_phrases((SITE / page).read_text(encoding="utf-8")):
            if not names_only(text):
                out[key_of(text)] = tidy(text)
    return out


TAG = re.compile(r"<(/?)([a-zA-Z][\w-]*)([^>]*)>")
ATTR = re.compile(r"([\w:-]+)\s*=\s*(\"[^\"]*\"|'[^']*')")


def _tags(text: str) -> list[str]:
    """The tags of a phrase with their attributes (links, classes, ids the page's
    script and styles use): what a translation must keep. Attributes that are
    text themselves (alt, title, aria-label) may be translated."""
    out = []
    for close, name, rest in TAG.findall(text):
        attrs = sorted(f"{k}={v}" for k, v in ATTR.findall(rest) if k not in TEXT_ATTRS)
        out.append(f"{close}{name.lower()} {' '.join(attrs)}".strip())
    return sorted(out)


def _plain(text: str) -> str:
    return html.unescape(TAG.sub(" ", text))


def problems_of(folder: str, wanted: dict[str, str], phrases: dict) -> list[str]:
    problems = []
    for key, english in wanted.items():
        entry = phrases.get(key)
        if entry is None or not entry.get("t"):
            problems.append(f"{folder}: not translated: {english[:90]}")
            continue
        if tidy(entry.get("en", "")) != english:
            problems.append(f"{folder}: its English changed since: {english[:90]}")
            continue
        translated = entry["t"]
        if _tags(translated) != _tags(english):
            problems.append(f"{folder}: tags or links differ from the English: {english[:70]} -> {translated[:70]}")
        if sorted(PLACEHOLDER.findall(translated)) != sorted(PLACEHOLDER.findall(english)):
            problems.append(f"{folder}: placeholders differ: {english[:70]} -> {translated[:70]}")
        for name in KEEP:
            if name in _plain(english) and name not in _plain(translated):
                problems.append(f"{folder}: '{name}' is missing: {english[:60]} -> {translated[:60]}")
        # (Marked "same": the same word in that language on purpose, Spanish "No".)
        if tidy(translated) == english and LETTER.search(_only_words(english)) and not entry.get("same"):
            problems.append(f"{folder}: left in English: {english[:90]}")
    for key, entry in phrases.items():
        if key not in wanted:
            problems.append(f"{folder}: no page has this phrase any more (remove it): {entry.get('en', '')[:80]}")
    return problems


def _only_words(text: str) -> str:
    """What is left of a phrase without the names that are never translated."""
    plain = _plain(text)
    names: list[str] = []
    for name in KEEP + ["VST", "VST2", "Rhodes", "Wurlitzer", "OTT", "Squash", "Enhance", "Sustain Pads", "AppImage",
                        "Surge XT", "Vital", "Dexed", "Arturia", "Analog Lab", "Kontakt", "Keyscape", "SF2",
                        "SoundFont", "PipeWire", "JACK", "ASIO", "WASAPI", "REAPER", "Ableton Live", "Logic",
                        "Gig Performer", "Cantabile", "Carla", "Camelot", "GitHub Sponsors", "Apple Silicon",
                        "Ubuntu", "Debian", "Mint", "Fedora", "Arch", "MB", "GB", "Mac", "Pop!_OS", "Audio Units",
                        "Cantabile Lite", "Element", "SoundFonts", "Loop station", "Setlists", ".deb"]:
        names.append(name)
    for name in sorted(names, key=len, reverse=True):  # (longest first: "Cantabile Lite" before "Cantabile")
        plain = plain.replace(name, " ")
    return plain


def check() -> list[str]:
    wanted = wanted_phrases()
    problems = []
    for folder in FOLDERS:
        problems += problems_of(folder, wanted, load(folder))
    return problems


# ------------------------------------------------------------------ building
def page_url(page: str, folder: str | None) -> str:
    name = "" if page == "index.html" else page
    return BASE + (f"{folder}/" if folder else "") + name


def rewrite_links(text: str, folder: str) -> str:
    """Links on a page one folder down: its own language's pages, the shared files one up."""

    def fix(match):
        attr, quote, url = match.group(1), match.group(2), match.group(3)
        if re.match(r"^(?:[a-z]+:|//|#|\.\./|/|data:)", url):
            return match.group(0)
        if url.startswith("./"):
            return match.group(0)  # this language's home page
        path = url.split("#")[0].split("?")[0]
        if path in TRANSLATED:
            return match.group(0)
        return f"{attr}={quote}../{url}{quote}"

    return re.sub(r"\b(href|src)=([\"'])([^\"']*)\2", fix, text)


def head_extras(page: str, folder: str | None) -> str:
    links = [f'<link rel="alternate" hreflang="en" href="{page_url(page, None)}">']
    if page in TRANSLATED:
        for f, code, *_ in LANGUAGES:
            links.append(f'<link rel="alternate" hreflang="{code}" href="{page_url(page, f)}">')
        links.append(f'<link rel="alternate" hreflang="x-default" href="{page_url(page, None)}">')
    return "\n".join(links) + "\n" + STYLE


def language_menu(page: str, folder: str | None) -> str:
    current = "English" if folder is None else next(l[2] for l in LANGUAGES if l[0] == folder)
    up = "" if folder is None else "../"
    english = up + ("" if page == "index.html" else page) or "./"
    mine = ' aria-current="true"' if folder is None else ""
    items = [f'<li><a href="{english}" hreflang="en" lang="en"{mine}>English</a></li>']
    if page in TRANSLATED:
        for f, code, name, *_ in LANGUAGES:
            target = f"{up}{f}/{'' if page == 'index.html' else page}"
            current_attr = ' aria-current="true"' if f == folder else ""
            items.append(f'<li><a href="{target}" hreflang="{code}" lang="{code}"{current_attr}>{name}</a></li>')
    if folder is not None:
        title = urllib.parse.quote(f"Translation ({folder}, {page}): ")
        items.append(f'<li class="lang-report"><a href="{ISSUES}?labels=translation&amp;title={title}">✎ {REPORT[folder]}</a></li>')
    return ('<details class="lang-menu"><summary aria-label="Language">🌐 <span>' + current + "</span></summary>"
            '<ul>' + "".join(items) + "</ul></details>")


def offer_script(page: str, folder: str | None) -> str:
    """The one-line offer of the reader's own language, when the page has it."""
    if page not in TRANSLATED:
        return ""
    up = "" if folder is None else "../"
    offers = {}
    for f, code, name, text, go, close in LANGUAGES:
        offers[code] = {"url": f"{up}{f}/{'' if page == 'index.html' else page}", "text": text, "go": go, "close": close}
    offers["en"] = {"url": up + ("" if page == "index.html" else page) or "./", "text": "This page is also in English.",
                    "go": "Read in English", "close": "Close"}
    here = "en" if folder is None else next(l[1] for l in LANGUAGES if l[0] == folder)
    # In <head>, so a reader taken to their language never sees the English first.
    # - A language chosen in the menu (or the offer) is remembered and always kept.
    # - No choice yet: an English page goes straight to the reader's own
    #   language (the browser's first one the site has); a page in another
    #   language (a link someone shared) stays, and offers theirs instead.
    # Search engines read the English pages as they are (their browser is English).
    return (
        "<script>\n(function () {\n"
        f"  var offers = {json.dumps(offers, ensure_ascii=False)};\n"
        f"  var here = {json.dumps(here)};\n"
        "  function main(tag) { return String(tag).toLowerCase().split(\"-\")[0]; }\n"
        "  var chosen = null;\n"
        "  try { chosen = localStorage.getItem(\"gck-lang\"); } catch (e) {}\n"
        "  // (Every language link a reader clicks is their choice from then on.)\n"
        "  document.addEventListener(\"click\", function (e) {\n"
        "    var link = e.target.closest && e.target.closest(\".lang-menu a[hreflang], .lang-offer a[hreflang]\");\n"
        "    if (link) try { localStorage.setItem(\"gck-lang\", link.getAttribute(\"hreflang\")); } catch (err) {}\n"
        "  });\n"
        "  if (chosen) {\n"
        "    if (chosen !== here && offers[chosen]) location.replace(offers[chosen].url);\n"
        "    return;\n"
        "  }\n"
        "  var asked = (navigator.languages && navigator.languages.length ? navigator.languages : [navigator.language || \"\"]);\n"
        "  var want = null;\n"
        "  for (var i = 0; i < asked.length && !want; i++) {\n"
        "    var tag = String(asked[i]).toLowerCase(), m = main(tag);\n"
        "    if (m === main(here)) return; // already in a language they read\n"
        "    if (m === \"zh\") want = (tag.indexOf(\"tw\") < 0 && tag.indexOf(\"hk\") < 0 && tag.indexOf(\"hant\") < 0) ? \"zh-Hans\" : null;\n"
        "    else if (m === \"pt\") want = \"pt-BR\";\n"
        "    else for (var code in offers) if (main(code) === m) want = code;\n"
        "  }\n"
        "  if (!want || want === here) return;\n"
        "  if (here === \"en\") { location.replace(offers[want].url); return; }\n"
        "  try { if (localStorage.getItem(\"gck-lang-offer\") === \"no\") return; } catch (e) {}\n"
        "  document.addEventListener(\"DOMContentLoaded\", function () {\n"
        "    var o = offers[want], bar = document.createElement(\"div\");\n"
        "    bar.className = \"lang-offer\"; bar.setAttribute(\"lang\", want); bar.setAttribute(\"role\", \"region\");\n"
        "    bar.setAttribute(\"aria-label\", o.text);\n"
        "    var text = document.createElement(\"span\"); text.textContent = o.text;\n"
        "    var go = document.createElement(\"a\"); go.href = o.url; go.textContent = o.go; go.hreflang = want;\n"
        "    var close = document.createElement(\"button\"); close.type = \"button\"; close.textContent = \"×\";\n"
        "    close.setAttribute(\"aria-label\", o.close);\n"
        "    close.onclick = function () { bar.remove(); try { localStorage.setItem(\"gck-lang-offer\", \"no\"); } catch (e) {} };\n"
        "    bar.appendChild(text); bar.appendChild(go); bar.appendChild(close);\n"
        "    document.body.insertBefore(bar, document.body.firstChild);\n"
        "  });\n"
        "})();\n</script>\n"
    )


STYLE = """<style id="lang-style">
.lang-menu { position: relative; display: inline-block; }
.lang-menu summary { cursor: pointer; list-style: none; white-space: nowrap; }
.lang-menu summary::-webkit-details-marker { display: none; }
.lang-menu ul { position: absolute; right: 0; top: 100%; z-index: 50; margin: 6px 0 0; padding: 6px 0; list-style: none;
  min-width: 190px; background: var(--panel, #1c1c20); border: 1px solid var(--border, #33333a); border-radius: 10px;
  box-shadow: 0 10px 30px rgba(0,0,0,.45); }
.lang-menu li a { display: block; padding: 7px 14px; text-decoration: none; color: inherit; }
.lang-menu li a:hover, .lang-menu li a:focus-visible { background: rgba(255,255,255,.07); }
.lang-menu li a[aria-current] { font-weight: 700; }
.lang-menu li.lang-report { border-top: 1px solid var(--border, #33333a); margin-top: 4px; padding-top: 4px; font-size: 14px; opacity: .85; }
.lang-offer { display: flex; flex-wrap: wrap; gap: 8px 14px; align-items: center; justify-content: center; padding: 9px 16px;
  background: var(--panel, #1c1c20); border-bottom: 1px solid var(--border, #33333a); font-size: 15px; }
.lang-offer a { font-weight: 700; }
.lang-offer button { background: none; border: 0; color: inherit; font-size: 20px; line-height: 1; cursor: pointer; padding: 2px 6px; }
@media (max-width: 560px) { .lang-menu summary span { display: none; } }
</style>"""


def build_page(source: str, page: str, folder: str | None, phrases: dict) -> str:
    out = source
    if folder is not None:
        def translate(english: str) -> str:
            if names_only(english):
                return english
            entry = phrases.get(key_of(english))
            if entry is None or not entry.get("t") or tidy(entry.get("en", "")) != tidy(english):
                raise KeyError(f"{folder}/{page}: no translation for: {tidy(english)[:80]}")
            return entry["t"]

        for p in sorted(phrases_of(source), key=lambda p: p.start, reverse=True):
            if p.kind == "json":
                script_words = '"@context"' not in p.english
                text = translate_json(p.english, script_words, lambda s: translate(tidy(s)),
                                      lambda url: _lang_url(url, folder))
            elif p.kind == "attr":
                text = html.escape(html.unescape(translate(p.english)), quote=True)
            else:
                text = translate(p.english)
            out = out[:p.start] + text + out[p.end:]
        code = next(l[1] for l in LANGUAGES if l[0] == folder)
        out = re.sub(r"<html\b[^>]*>", f'<html lang="{code}">', out, count=1)
        out = re.sub(r'(<link rel="canonical" href=")[^"]*(")', lambda m: m.group(1) + page_url(page, folder) + m.group(2),
                     out, count=1)
        out = re.sub(r'(<meta property="og:url" content=")[^"]*(")', lambda m: m.group(1) + page_url(page, folder) + m.group(2),
                     out, count=1)
        out = re.sub(r'(<meta property="og:type"[^>]*>)', lambda m: m.group(1) + f'\n<meta property="og:locale" content="{code.replace("-", "_")}">',
                     out, count=1)
        out = rewrite_links(out, folder)
    out = out.replace("</head>", head_extras(page, folder) + "\n</head>", 1)
    out = re.sub(r"(<header\b[^>]*>.*?)(</nav>)", lambda m: m.group(1) + language_menu(page, folder) + m.group(2), out,
                 count=1, flags=re.S)
    out = out.replace("</head>", offer_script(page, folder) + "</head>", 1)  # (before anything shows)
    return out


def _lang_url(url: str, folder: str) -> str:
    rest = url[len(BASE):]
    path = rest.split("#")[0] or "index.html"
    if path in TRANSLATED:
        return BASE + f"{folder}/" + rest
    return url


def sitemap() -> str:
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9" xmlns:xhtml="http://www.w3.org/1999/xhtml">']
    pages = sorted(p.name for p in SITE.glob("*.html"))
    for page in pages:
        versions = [(None, "en")] + ([(f, code) for f, code, *_ in LANGUAGES] if page in TRANSLATED else [])
        for folder, _ in versions:
            lines.append("  <url>")
            lines.append(f"    <loc>{page_url(page, folder)}</loc>")
            if len(versions) > 1:
                for f, code in versions:
                    lines.append(f'    <xhtml:link rel="alternate" hreflang="{code}" href="{page_url(page, f)}"/>')
            lines.append("  </url>")
    lines.append("</urlset>")
    return "\n".join(lines) + "\n"


def build(out: Path) -> None:
    problems = check()
    if problems:
        raise SystemExit("The translations are not ready:\n  " + "\n  ".join(problems[:60]) +
                         (f"\n  ... and {len(problems) - 60} more" if len(problems) > 60 else ""))
    out.mkdir(parents=True, exist_ok=True)
    for path in sorted(SITE.glob("*.html")):
        source = path.read_text(encoding="utf-8")
        (out / path.name).write_text(build_page(source, path.name, None, {}), encoding="utf-8")
        if path.name in TRANSLATED:
            for folder in FOLDERS:
                (out / folder).mkdir(exist_ok=True)
                (out / folder / path.name).write_text(build_page(source, path.name, folder, load(folder)), encoding="utf-8")
    (out / "sitemap.xml").write_text(sitemap(), encoding="utf-8")


def todo(folder: str) -> dict:
    wanted = wanted_phrases()
    have = load(folder)
    return {key: {"en": english, "t": ""} for key, english in wanted.items()
            if key not in have or tidy(have[key].get("en", "")) != english or not have[key].get("t")}


def merge(folder: str, translations_file: Path) -> int:
    """Files a language's translations ({key: translation}) with their English
    beside them, keeping what it had for the phrases not given."""
    given = json.loads(translations_file.read_text(encoding="utf-8"))
    wanted = wanted_phrases()
    phrases = load(folder)
    unknown = [k for k in given if k not in wanted]
    if unknown:
        print(f"{folder}: left out {len(unknown)} key(s) no phrase needs: {unknown[:8]}")
    for key, text in given.items():
        if key not in wanted:
            continue
        # (A translation given as {"t": ..., "same": true}: the same as the English on purpose.)
        if isinstance(text, dict):
            phrases[key] = {"en": wanted[key], "t": text["t"], **({"same": True} if text.get("same") else {})}
        else:
            phrases[key] = {"en": wanted[key], "t": text}
    ordered = {k: phrases[k] for k in wanted if k in phrases}
    I18N.mkdir(exist_ok=True)
    with open(I18N / f"{folder}.json", "w", encoding="utf-8", newline="\n") as out:  # (\n on Windows too)
        out.write(json.dumps({"language": folder, "phrases": ordered}, ensure_ascii=False, indent=1) + "\n")
    return len(given)


def main(argv: list[str]) -> int:
    sys.stdout.reconfigure(encoding="utf-8")  # (♥, हिन्दी: also on a Windows console)
    if len(argv) == 3 and argv[0] == "merge":
        print(f"{argv[1]}: {merge(argv[1], Path(argv[2]))} phrase(s) filed")
        return 0
    if len(argv) >= 2 and argv[0] == "build":
        build(Path(argv[1]))
        print(f"built: {len(list(SITE.glob('*.html')))} English pages, {len(TRANSLATED)} pages in {len(FOLDERS)} languages")
        return 0
    if argv[:1] == ["check"]:
        problems = check()
        for p in problems:
            print(p)
        print(f"{len(problems)} problem(s)" if problems else "every translation is there, current and intact")
        return 1 if problems else 0
    if len(argv) == 2 and argv[0] == "todo":
        sys.stdout.reconfigure(encoding="utf-8")
        print(json.dumps(todo(argv[1]), ensure_ascii=False, indent=1))
        return 0
    if argv[:1] == ["phrases"]:
        sys.stdout.reconfigure(encoding="utf-8")
        print(json.dumps(wanted_phrases(), ensure_ascii=False, indent=1))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
