#pragma once

#include "gigchain/core/Error.h"

#include <QString>
#include <QStringList>
#include <QTranslator>

#include <memory>
#include <vector>

namespace gigchain::ui {

// The language the app speaks (docs/i18n/app.md). Its translations come
// with the app (":/i18n/gigchain_<code>.qm"), with Qt's own for the
// standard dialogs where Qt has one (":/i18n/qtbase_<code>.qm").
class Language
{
public:
    struct Info
    {
        QString code; // "pt_BR": the translation files' code
        QString name; // "Português": as the language names itself
    };

    // The settings key: empty for the computer's language, "en" for English,
    // or a code of available().
    static const QString kSettingKey;

    // The languages the app has, besides English, in the website's order.
    [[nodiscard]] static const std::vector<Info>& available();

    // What the app shows: `setting` when it names a language the app has
    // ("en": English), else the first of `uiLanguages` (QLocale's
    // uiLanguages: "pt-BR", "zh-Hans-CN"...) it has. Empty means English.
    [[nodiscard]] static QString resolve(const QString& setting, const QStringList& uiLanguages);

    // "English", in the language installed (the language picker's word for it).
    [[nodiscard]] static QString englishName();

    Language() = default;
    ~Language();
    Language(const Language&) = delete;
    Language& operator=(const Language&) = delete;
    Language(Language&&) = delete;
    Language& operator=(Language&&) = delete;

    // Installs `code`'s translations on the application, for as long as this
    // lives. An unknown code, or translations that will not load, fail with
    // the cause (English stays).
    core::Result<void> install(const QString& code);

private:
    std::unique_ptr<QTranslator> m_app;
    std::unique_ptr<QTranslator> m_qt;
};

} // namespace gigchain::ui
