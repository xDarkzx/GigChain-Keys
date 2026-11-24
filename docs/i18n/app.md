# The app in other languages

GigChain Keys in the same ten languages as the website
([website.md](website.md)): Hindi, Portuguese (Brazil), Spanish,
Indonesian, German, French, Russian, Japanese, Korean and Chinese
(Simplified), with the same glossary ([glossary.md](glossary.md)). The aim
is that a player reads the app in their language without a translation
program: readable and natural first, perfect later.

## What the player gets

- **The app starts in their language**: the computer's own language when
  the app has it, English otherwise.
- **The installer speaks it too** (Windows): it opens in the computer's
  language, and the language picked there is the app's first language.
- **Settings > General > Language**: *The computer's language* (the
  default), or any of the eleven. A change shows after a restart, and the
  app says so.
- Every screen, dialog, menu, tooltip and message, the engine's too (a
  plugin that did not load, no audio output, a file it cannot read). Not
  translated: the user guide (next), the What's new list (it is the changelog's
  words), plugin and instrument names, preset names, and the knob labels
  on the app's own instruments (LOW, RATE, ENHANCE...: like a hardware
  synth's panel, they stay English everywhere). Also English on purpose:
  what a plugin, the audio driver or the system says (quoted after the
  app's own words, as in "Could not open X: <the driver's words>"), the
  technical detail in brackets after "settings are damaged", the plugin
  scan's reasons (kept in the scan's cache, so a later language change
  would leave old ones), the developer test control's messages, and
  messages only a programming mistake can show ("has no parameter 7").
- The translations are AI-written and not yet reviewed by native
  speakers: readable, not perfect. A player can report a wording from the
  issues page.

## How it works

- **Qt's own translation system.** The text is already marked (`qsTr` in
  QML, `tr` in C++); the engine's messages are marked too
  (`QCoreApplication::translate("Engine", ...)`, whole sentences, never
  pieces put together). `lupdate` collects every marked text, the UI's
  and the engine's (`SOURCE_TARGETS gigchain_ui gigchain_engine`), into
  `src/ui/translations/gigchain_<language>.ts`;
  `qt_add_translations` compiles them (`lrelease`) into the app's
  resources at build time, so nothing extra is shipped or installed.
- **Startup** (`main.cpp`, before the first window): the language is read
  from the setting (`general/language`, empty for the computer's), and the
  app's translator plus Qt's own (`qtbase_<language>.qm`, standard dialog
  buttons, where Qt has one) are installed.
- **The installer** (`installer/setup.iss`): a `[Languages]` entry per
  language, Inno Setup's own translations where it has them
  (BrazilianPortuguese, Spanish, German, French, Russian, Japanese,
  Korean) and Inno's community files in `installer/languages/` (from
  jrsoftware/issrc, unchanged) for Hindi, Indonesian and Chinese
  (Simplified); the messages Hindi's and Indonesian's files predate are
  translated in setup.iss's `[Messages]`. It writes the language chosen to
  the app's setting (`HKCU\Software\<organization>\<name>\general\language`)
  only when the app has no language set yet, so reinstalling never
  overrides what the player chose in Settings. A setting written by the
  installer alone does not stop the earlier name's settings being carried
  over (SettingsMigration).
- **Fonts**: the theme's font (Segoe UI) has no Devanagari or CJK; Windows'
  font fallback draws them (checked in the running app). The off-screen
  test renderer has no fallback, so its screenshots show boxes there.

## Checks

- `tools/app_i18n.py check` (the `translations_complete` test, in CI and
  the gate) fails when a language's file:
  - has a text with no translation, or one marked unfinished;
  - has the wrong number of plural forms for the language;
  - lost or changed a placeholder (`%1`, `%n`, `%L1`) or a tag;
  - left a text in English that is not marked as kept on purpose;
  - differs from the other languages' texts, or is not as lupdate writes it;
  - is behind the source: the check runs lupdate on a copy, so a text new
    in the source (or gone from it) fails it until the
    `update_translations` target is built and the text translated.
- `tst_language`: the language chosen from the setting and the computer's
  languages, every language's translations bundled and loading, and an
  engine message coming out in the language.
- `tst_qml_smoke`: the main window loads in German, Russian, Hindi and
  Japanese with no QML warnings and shows the language (in its own script);
  Settings > General offers every language.

## Not now

- The user guide (`docs/help`) in other languages: the next step.
- The system code's log-only messages (src/platform: crash reports,
  memory use, the single-instance lock): the player never sees them. Its
  texts the player does see (the audio driver names, "Could not open
  Explorer for...") are translated, and every system's files are
  scanned on every system, so Windows and Linux check the same texts.
- Right-to-left languages.
- Changing language without a restart.
