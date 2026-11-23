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
- Every screen, dialog, menu, tooltip and message, the engine's messages
  (a plugin that did not load, no audio) included. Not translated: the
  user guide (next), the What's new list (it is the changelog's words),
  plugin and instrument names, preset names.

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
  Korean) and translation files in `installer/languages/` for Hindi,
  Indonesian and Chinese (Simplified). It writes the language chosen to the
  app's setting only when the app has no language set yet, so reinstalling
  never overrides what the player chose in Settings.

## Checks

- `tools/app_i18n.py check` (a test the gate runs, like the source scans)
  fails when a language's file:
  - has a text with no translation, or one marked unfinished;
  - lost or changed a placeholder (`%1`, `%n`, `%L1`) or a keyboard
    shortcut's `&`;
  - is out of date with the source (lupdate has new or vanished texts);
  - left a whole sentence in English (glossary names excepted).
- The smoke tests start the app in German, Russian, Hindi and Japanese
  (long words, other scripts) and check the main window, Settings and
  the Perform view lay out with nothing cut off or overlapping, and with
  the language's own script rendered (no missing-character boxes).

## Not now

- The user guide (`docs/help`) in other languages: next.
- Right-to-left languages.
- Changing language without a restart.
