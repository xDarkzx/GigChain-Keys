# The website in other languages

The download site (`site/`, published by `.github/workflows/pages.yml`) in
the languages GigChain Keys' players speak, so a keyboard player finds it
by searching in their own language and reads it as if it were written in
it. The app follows the same way of working next (the same glossary, the
same checks).

## Languages

| Folder | Language | `hreflang` |
| --- | --- | --- |
| `hi/` | Hindi | `hi` |
| `pt-br/` | Portuguese (Brazil) | `pt-BR` |
| `es/` | Spanish | `es` |
| `id/` | Indonesian | `id` |
| `de/` | German | `de` |
| `fr/` | French | `fr` |
| `ru/` | Russian | `ru` |
| `ja/` | Japanese | `ja` |
| `ko/` | Korean | `ko` |
| `zh-cn/` | Chinese (Simplified) | `zh-Hans` |

English stays at the site's root (`/GigChain-Keys/`), as today: no link
anyone has shared breaks.

## Which pages

Translated: the download page (`index.html`), `support.html`,
`mainstage-for-windows.html`, `free-mainstage-alternatives.html`,
`play-vst-plugins-live.html`. The comparisons with other hosts
(`cantabile-alternative.html`, `gig-performer-alternative.html`,
`carla-vs-gigchain-keys.html`) answer English searches about English
products and stay English; links to them from a translated page go to the
English page.

## How it works

- **English is the source.** The pages in `site/` are written in English as
  before. `tools/site_i18n.py` reads them and finds every piece of text a
  reader sees: each block of text (a heading, a paragraph, a list item, a
  button, with its links and bold kept inside it), the page title and
  description, image descriptions (`alt`), the FAQ in the search engines'
  structured data, and the words the page's own script writes (in a
  `<script type="application/json" id="i18n">` block of named phrases).
- **One file per language**, `site/i18n/<folder>.json`: each phrase's English
  and its translation, keyed by the English text's fingerprint. When the
  English changes, its fingerprint changes and that phrase is reported as
  needing translation again: a page never quietly keeps an old sentence.
- **Built when the site is published.** `pages.yml` runs
  `python3 tools/site_i18n.py build _site`: the English pages as they are,
  plus `<folder>/<page>.html` for every language, each with
  - its `lang`, its own canonical address, and `hreflang` links to every
    language's version of the page (so search engines show each reader
    their own);
  - a language menu in the header;
  - links to its own language's pages, and to the shared CSS and images;
  - the reader's own language, at once: an English page goes straight to
    the version in their browser's language, when the site has it (in the
    page's `<head>`, so the English never flashes). A language they pick in
    the menu is remembered and always kept. A page already in another
    language (a link someone shared) stays, and offers theirs in one line
    instead ("Esta página também está em português"). Search engines read
    the English pages as they are;
  - the sitemap listing every page in every language.
- **Checked before it is published**, and on every pull request that
  touches `site/` (`python3 tools/site_i18n.py check`, with its tests in
  `tools/site_i18n_test.py`). A check fails when:
  - a phrase has no translation, or its English has changed since;
  - a translation lost or changed a link, a tag, or a placeholder (`{os}`);
  - a name that must not change is missing (GigChain Keys, MainStage,
    Windows, macOS, Linux, VST3, MIDI);
  - a translation is the English left untranslated (names-only phrases
    excepted);
  - a language's file has phrases no page uses (left over).

## Translations that make sense

Written for the reader, not word for word, following
[glossary.md](glossary.md): what stays in English (the terms keyboard
players use as they are, in every country: VST, MIDI, setlist, plugin
names), how each language addresses the reader, and its tone.

The first translations (November 2026) were written by the project's
AI assistant to that glossary, and pass every check above. They have not
yet been reviewed by a native speaker of each language. Until they are,
every translated page's language menu ends with a link to report a
wording that reads wrong (an issue labelled `translation`, titled with the
language and the page). A fix changes that phrase in
`site/i18n/<folder>.json` (`tools/site_i18n.py merge`) and nothing else.

## Not now

- The comparison pages (above).
- The app, then its user guide: next, the same way.
- Choosing a language by the reader's location: their browser's language
  says what they read (a Hindi speaker in Canada, an English speaker in
  Brazil), with no outside location service.
