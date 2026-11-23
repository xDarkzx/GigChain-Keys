#!/usr/bin/env python3
"""Tests of tools/site_i18n.py (docs/i18n/website.md).
  python3 tools/site_i18n_test.py
"""
import json
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import site_i18n as s  # noqa: E402

PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Free MainStage Alternative &amp; More | GigChain Keys</title>
<meta name="description" content="Play your plugins live, free.">
<link rel="canonical" href="https://xdarkzx.github.io/GigChain-Keys/support.html">
<meta property="og:type" content="website">
<meta property="og:url" content="https://xdarkzx.github.io/GigChain-Keys/support.html">
<link rel="stylesheet" href="guide.css">
<script type="application/ld+json">
{"@context": "https://schema.org", "@type": "FAQPage", "mainEntity": [
  {"@type": "Question", "name": "Is it free?", "acceptedAnswer": {"@type": "Answer", "text": "Yes, always."}}],
 "url": "https://xdarkzx.github.io/GigChain-Keys/support.html"}
</script>
<style>.x { color: red; }</style>
</head>
<body>
<header><nav><a href="./#download">Download</a> <a href="support.html">Support</a> <a href="https://github.com/x">GitHub</a></nav></header>
<h1>Support the app</h1>
<p>Donate with <b>Buy Me a Coffee</b>, or <a href="cantabile-alternative.html">compare it</a>.</p>
<img src="images/a.png" alt="The mixer, with two channels">
<div><span>Windows</span></div>
<p>Version {version} is out.</p>
<script>var text = "Never translated: code";</script>
<script type="application/json" id="i18n">{"downloadFor": "Download for {os}"}</script>
</body>
</html>
"""


def phrases_for(source, change=lambda en: "«" + en + "»"):
    """A translation of every phrase of `source`: its words changed, its tags kept."""
    out = {}
    for text in s.page_phrases(source):
        if s.names_only(text):
            continue
        # Change only the words between tags (tags and placeholders kept as they are).
        parts = re.split(r"(<[^>]+>|\{\w+\})", text)
        translated = "".join(p if p.startswith("<") or p.startswith("{") or not p.strip() else change(p) for p in parts)
        out[s.key_of(text)] = {"en": s.tidy(text), "t": translated}
    return out


class Extraction(unittest.TestCase):
    def setUp(self):
        self.texts = s.page_phrases(PAGE)

    def test_blocks_keep_their_tags_and_entities(self):
        self.assertIn('Donate with <b>Buy Me a Coffee</b>, or <a href="cantabile-alternative.html">compare it</a>.',
                      self.texts)
        self.assertIn("Free MainStage Alternative &amp; More | GigChain Keys", self.texts)

    def test_menu_links_are_each_a_phrase(self):
        self.assertIn('<a href="./#download">Download</a>', self.texts)
        self.assertIn('<a href="support.html">Support</a>', self.texts)

    def test_attributes_meta_and_structured_data(self):
        for expected in ("The mixer, with two channels", "Play your plugins live, free.", "Is it free?", "Yes, always.",
                         "Download for {os}"):
            self.assertIn(expected, self.texts)

    def test_code_styles_and_links_are_not_phrases(self):
        joined = " ".join(self.texts)
        self.assertNotIn("Never translated", joined)
        self.assertNotIn("color: red", joined)
        self.assertNotIn("https://schema.org", joined)

    def test_names_alone_need_no_translation(self):
        self.assertTrue(s.names_only("Windows"))
        self.assertTrue(s.names_only('<a href="https://github.com/x">GitHub</a>'))
        self.assertFalse(s.names_only("Support the app"))

    def test_the_fingerprint_ignores_spacing_only(self):
        self.assertEqual(s.key_of("Support  the\napp"), s.key_of("Support the app"))
        self.assertNotEqual(s.key_of("Support the app"), s.key_of("Support this app"))


class Checks(unittest.TestCase):
    def setUp(self):
        self.wanted = {s.key_of(t): s.tidy(t) for t in s.page_phrases(PAGE) if not s.names_only(t)}
        self.good = phrases_for(PAGE)

    def problems(self, phrases):
        return s.problems_of("xx", self.wanted, phrases)

    def test_a_complete_translation_passes(self):
        self.assertEqual(self.problems(self.good), [])

    def test_a_missing_phrase(self):
        phrases = dict(self.good)
        phrases.pop(s.key_of("Support the app"))
        self.assertTrue(any("not translated: Support the app" in p for p in self.problems(phrases)))

    def test_english_changed_since(self):
        phrases = dict(self.good)
        key = s.key_of("Support the app")
        phrases[key] = dict(phrases[key], en="Support the old app")
        self.assertTrue(any("English changed" in p for p in self.problems(phrases)))

    def test_a_lost_or_changed_link(self):
        key = s.key_of('Donate with <b>Buy Me a Coffee</b>, or <a href="cantabile-alternative.html">compare it</a>.')
        for broken in ('Donar con <b>Buy Me a Coffee</b>, o compáralo.',
                       'Donar con <b>Buy Me a Coffee</b>, o <a href="elsewhere.html">compáralo</a>.'):
            phrases = dict(self.good)
            phrases[key] = dict(phrases[key], t=broken)
            self.assertTrue(any("tags or links" in p for p in self.problems(phrases)), broken)

    def test_a_lost_placeholder(self):
        key = s.key_of("Download for {os}")
        phrases = dict(self.good)
        phrases[key] = dict(phrases[key], t="Descargar")
        self.assertTrue(any("placeholders" in p for p in self.problems(phrases)))

    def test_a_lost_name(self):
        key = s.key_of('Donate with <b>Buy Me a Coffee</b>, or <a href="cantabile-alternative.html">compare it</a>.')
        phrases = dict(self.good)
        phrases[key] = dict(phrases[key], t='Donar con <b>un café</b>, o <a href="cantabile-alternative.html">compáralo</a>.')
        self.assertTrue(any("'Buy Me a Coffee' is missing" in p for p in self.problems(phrases)))

    def test_left_in_english(self):
        key = s.key_of("Support the app")
        phrases = dict(self.good)
        phrases[key] = dict(phrases[key], t="Support the app")
        self.assertTrue(any("left in English" in p for p in self.problems(phrases)))

    def test_a_phrase_no_page_has(self):
        phrases = dict(self.good)
        phrases["000000000000"] = {"en": "Gone", "t": "Weg"}
        self.assertTrue(any("no page has this phrase" in p for p in self.problems(phrases)))


class Building(unittest.TestCase):
    def setUp(self):
        self.out = s.build_page(PAGE, "support.html", "es", phrases_for(PAGE))

    def test_the_words_are_translated_and_the_rest_kept(self):
        self.assertIn("<h1>«Support the app»</h1>", self.out)
        self.assertIn('alt="«The mixer, with two channels»"', self.out)
        self.assertIn('<script>var text = "Never translated: code";</script>', self.out)
        self.assertIn(".x { color: red; }", self.out)
        self.assertIn("<span>Windows</span>", self.out)  # (a name: as it is)

    def test_the_page_says_its_language_and_address(self):
        self.assertIn('<html lang="es">', self.out)
        self.assertIn('<link rel="canonical" href="https://xdarkzx.github.io/GigChain-Keys/es/support.html">', self.out)
        self.assertIn('<meta property="og:url" content="https://xdarkzx.github.io/GigChain-Keys/es/support.html">', self.out)
        self.assertIn('<meta property="og:locale" content="es">', self.out)

    def test_every_language_is_linked_for_search_engines(self):
        self.assertIn('hreflang="en" href="https://xdarkzx.github.io/GigChain-Keys/support.html"', self.out)
        self.assertIn('hreflang="pt-BR" href="https://xdarkzx.github.io/GigChain-Keys/pt-br/support.html"', self.out)
        self.assertIn('hreflang="x-default"', self.out)
        self.assertEqual(self.out.count('rel="alternate" hreflang='), len(s.LANGUAGES) + 2)

    def test_links_reach_this_language_or_the_shared_files(self):
        self.assertIn('href="../guide.css"', self.out)
        self.assertIn('src="../images/a.png"', self.out)
        self.assertIn('href="./#download"', self.out)  # this language's home page
        self.assertIn('href="support.html"', self.out)  # translated: this language's
        self.assertIn('href="../cantabile-alternative.html"', self.out)  # English only
        self.assertIn('href="https://github.com/x"', self.out)

    def test_the_language_menu_is_in_the_header(self):
        header = self.out.split("<header>")[1].split("</header>")[0]
        self.assertIn('class="lang-menu"', header)
        self.assertIn('<a href="../support.html" hreflang="en" lang="en">English</a>', header)
        self.assertIn('<a href="../de/support.html" hreflang="de" lang="de">Deutsch</a>', header)
        self.assertIn('hreflang="es" lang="es" aria-current="true">Español</a>', header)
        # A wording that reads wrong: reported in a click, titled with its language and page.
        self.assertIn('labels=translation&amp;title=Translation%20%28es%2C%20support.html%29%3A%20">✎ Informar un error de traducción</a>',
                      header)
        english_header = s.build_page(PAGE, "support.html", None, {}).split("<header>")[1].split("</header>")[0]
        self.assertNotIn('class="lang-report"', english_header)  # (English is the source: nothing to report)

    def test_structured_data_and_script_words_stay_valid_json(self):
        blocks = re.findall(r'<script type="application/(?:ld\+)?json"[^>]*>(.*?)</script>', self.out, re.S)
        self.assertEqual(len(blocks), 2)
        ld = json.loads(blocks[0])
        self.assertEqual(ld["mainEntity"][0]["name"], "«Is it free?»")
        self.assertEqual(ld["url"], "https://xdarkzx.github.io/GigChain-Keys/es/support.html")
        self.assertEqual(json.loads(blocks[1])["downloadFor"], "«Download for »{os}")

    def test_english_pages_get_the_menu_and_links_but_keep_their_words(self):
        english = s.build_page(PAGE, "support.html", None, {})
        self.assertIn("<h1>Support the app</h1>", english)
        self.assertIn('<html lang="en">', english)
        self.assertIn('href="guide.css"', english)
        self.assertIn('class="lang-menu"', english)
        self.assertIn('<a href="es/support.html" hreflang="es" lang="es">Español</a>', english)

    def test_the_language_switch_runs_before_the_page_shows(self):
        english = s.build_page(PAGE, "support.html", None, {})
        head = english.split("</head>")[0]
        self.assertIn("location.replace", head)  # (an English page takes a reader to their language)
        self.assertIn('"pt-BR": {"url": "pt-br/support.html"', head)
        self.assertIn('localStorage.getItem("gck-lang")', head)  # (a language they chose is kept)
        self.assertNotIn("location.replace", english.split("</head>")[1])

    def test_a_page_only_in_english_gets_no_other_languages(self):
        english = s.build_page(PAGE, "cantabile-alternative.html", None, {})
        self.assertNotIn('hreflang="es"', english)
        self.assertIn('hreflang="en"', english)

    def test_an_attribute_inside_a_phrase_is_translated_with_it(self):
        # (Its text is part of the sentence's translation: replaced once, never twice.)
        page = PAGE.replace("<h1>Support the app</h1>",
                            '<h1>Support the app</h1>\n<p>See <a href="support.html" title="Help">support</a>.</p>')
        texts = s.page_phrases(page)
        self.assertIn('See <a href="support.html" title="Help">support</a>.', texts)
        self.assertNotIn("Help", texts)  # (not a phrase of its own)
        phrases = phrases_for(page)
        key = s.key_of('See <a href="support.html" title="Help">support</a>.')
        phrases[key] = dict(phrases[key], t='Mira <a href="support.html" title="Ayuda de la app">el soporte</a>.')
        self.assertEqual(s.problems_of("es", {s.key_of(t): s.tidy(t) for t in texts if not s.names_only(t)}, phrases), [])
        out = s.build_page(page, "support.html", "es", phrases)
        self.assertIn('<p>Mira <a href="support.html" title="Ayuda de la app">el soporte</a>.</p>', out)

    def test_a_missing_translation_stops_the_build(self):
        phrases = phrases_for(PAGE)
        phrases.pop(s.key_of("Support the app"))
        with self.assertRaises(KeyError):
            s.build_page(PAGE, "support.html", "es", phrases)


class TheRealSite(unittest.TestCase):
    """The site as it is: every translated page parses, and its phrases rebuild it."""

    def test_every_page_rebuilds_with_its_phrases_in_place(self):
        for page in s.TRANSLATED:
            source = (s.SITE / page).read_text(encoding="utf-8")
            out = s.build_page(source, page, "fr", phrases_for(source))
            self.assertIn('<html lang="fr">', out, page)
            self.assertNotIn("../https:", out, page)
            for href in re.findall(r'\bhref="([^"]+)"', out):
                if not re.match(r"^(https?:|mailto:|#|\.\./|\./)", href):
                    self.assertIn(href.split("#")[0], s.TRANSLATED, f"{page}: {href}")

    def test_the_sitemap_lists_every_language(self):
        xml = s.sitemap()
        self.assertIn("<loc>https://xdarkzx.github.io/GigChain-Keys/hi/</loc>", xml)
        self.assertIn("<loc>https://xdarkzx.github.io/GigChain-Keys/zh-cn/support.html</loc>", xml)
        self.assertIn("<loc>https://xdarkzx.github.io/GigChain-Keys/cantabile-alternative.html</loc>", xml)
        self.assertNotIn("/es/cantabile-alternative.html", xml)


if __name__ == "__main__":
    unittest.main(verbosity=1)
