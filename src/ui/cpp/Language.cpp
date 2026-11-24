#include "Language.h"

#include <QCoreApplication>
#include <QFile>
#include <QLoggingCategory>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {

const QString Language::kSettingKey = u"general/language"_s;

const std::vector<Language::Info>& Language::available()
{
    static const std::vector<Info> all{
        {.code = u"hi"_s, .name = u"हिन्दी"_s},        {.code = u"pt_BR"_s, .name = u"Português"_s},
        {.code = u"es"_s, .name = u"Español"_s},     {.code = u"id"_s, .name = u"Bahasa Indonesia"_s},
        {.code = u"de"_s, .name = u"Deutsch"_s},     {.code = u"fr"_s, .name = u"Français"_s},
        {.code = u"ru"_s, .name = u"Русский"_s},     {.code = u"ja"_s, .name = u"日本語"_s},
        {.code = u"ko"_s, .name = u"한국어"_s},       {.code = u"zh_CN"_s, .name = u"简体中文"_s},
    };
    return all;
}

namespace {

bool known(const QString& code)
{
    return std::ranges::any_of(Language::available(), [&code](const Language::Info& l) { return l.code == code; });
}

// A computer's language tag ("pt-BR", "zh-Hans-CN") as a code the app has,
// or empty: Portuguese is the Brazilian translation, Chinese only Simplified.
QString codeFor(const QString& tag)
{
    const QString lower = tag.toLower().replace(u'_', u'-');
    const QString main = lower.section(u'-', 0, 0);
    if (main == u"pt"_s) return u"pt_BR"_s;
    if (main == u"zh"_s) {
        // The script decides when the tag names one ("zh-Hans-HK": Simplified);
        // else the territory (Taiwan, Hong Kong, Macao write Traditional).
        const QStringList parts = lower.split(u'-');
        if (parts.contains(u"hans"_s)) return u"zh_CN"_s;
        if (parts.contains(u"hant"_s)) return {};
        const bool traditional = parts.contains(u"tw"_s) || parts.contains(u"hk"_s) || parts.contains(u"mo"_s);
        return traditional ? QString() : u"zh_CN"_s;
    }
    return known(main) ? main : QString();
}

} // namespace

QString Language::resolve(const QString& setting, const QStringList& uiLanguages)
{
    if (setting == u"en"_s) return {};
    if (known(setting)) return setting;
    for (const QString& tag : uiLanguages) {
        if (tag.section(u'-', 0, 0).compare(u"en"_s, Qt::CaseInsensitive) == 0) return {}; // English first: English
        if (const QString code = codeFor(tag); !code.isEmpty()) return code;
    }
    return {};
}

QString Language::englishName()
{
    return QCoreApplication::translate("Language", "English");
}

Language::~Language()
{
    if (m_app) QCoreApplication::removeTranslator(m_app.get());
    if (m_qt) QCoreApplication::removeTranslator(m_qt.get());
}

core::Result<void> Language::install(const QString& code)
{
    if (!known(code)) {
        return core::fail(core::ErrorCode::InvalidData, u"No translations for the language \"%1\": English stays"_s.arg(code));
    }
    auto app = std::make_unique<QTranslator>();
    const QString file = u":/i18n/gigchain_%1.qm"_s.arg(code);
    if (!app->load(file)) {
        return core::fail(core::ErrorCode::FileReadFailed,
                          u"The %1 translations could not be loaded (%2): English stays"_s.arg(code, file));
    }
    if (!QCoreApplication::installTranslator(app.get())) {
        return core::fail(core::ErrorCode::FileReadFailed, u"The %1 translations are empty (%2)"_s.arg(code, file));
    }
    m_app = std::move(app);
    qCInfo(lcUi).noquote() << "Language:" << code;
    // Qt's own (the standard dialogs' buttons), where Qt has the language
    // (none for some: not a failure). One that is there but fails is said.
    const QString qtFile = u":/i18n/qtbase_%1.qm"_s.arg(code);
    if (!QFile::exists(qtFile)) return {};
    auto qt = std::make_unique<QTranslator>();
    if (!qt->load(qtFile)) {
        return core::fail(core::ErrorCode::FileReadFailed,
                          u"Qt's own %1 translations could not be loaded (%2): the app speaks %1, Qt's standard dialog "
                          "buttons stay English"_s.arg(code, qtFile));
    }
    if (!QCoreApplication::installTranslator(qt.get())) {
        return core::fail(core::ErrorCode::FileReadFailed,
                          u"Qt's own %1 translations are empty (%2): the app speaks %1, Qt's standard dialog buttons "
                          "stay English"_s.arg(code, qtFile));
    }
    m_qt = std::move(qt);
    return {};
}

} // namespace gigchain::ui
