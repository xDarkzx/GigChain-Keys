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
- Every screen, dialog, menu, tooltip and message. Not translated: the
  engine's own messages (a plugin that did not load, no audio: next), the
  user guide (after that), the What's new list (it is the changelog's
  words), plugin and instrument names, preset names, and the knob labels
  on the app's own instruments (LOW, RATE, ENHANCE...: like a hardware
  synth's panel, they stay English everywhere).
- The translations are AI-written and not yet reviewed by native
  speakers: readable, not perfect. A player can report a wording from the
  issues page.

## How it works

- **Qt's own translation system.** The text is already marked (`qsTr` in
  QML, `tr` in C++); the engine's messages are marked too
  (`QCoreApplication::translate("Engine", ...)`). `lupdate` collects every
  marked text into `src/ui/translations/gigchain_<language>.ts`;
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
  - differs from the other languages' texts, or is not as lupdate writes it.
  A new text in the source is found by running the `update_translations`
  target (lupdate), which adds it to every file as untranslated: the check
  then fails until it is translated.
- `tst_language`: the language chosen from the setting and the computer's
  languages, and every language's translations bundled and loading.
- `tst_qml_smoke`: the main window loads in German, Russian, Hindi and
  Japanese with no QML warnings and shows the language (in its own script);
  Settings > General offers every language.

## Not now

- The engine's messages (`QCoreApplication::translate("Engine", ...)` in
  src/engine, src/core and src/platform, about 210): the next step.
- The user guide (`docs/help`) in other languages: after that.
- Right-to-left languages.
- Changing language without a restart.
