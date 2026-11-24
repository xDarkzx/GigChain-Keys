// The language the app speaks (docs/i18n/app.md): the player's choice, else
// the computer's, else English; and the translations that come with the app.
#include "Language.h"

#include <QCoreApplication>
#include <QtTest>

using namespace gigchain::ui;
using namespace Qt::StringLiterals;

class TestLanguage : public QObject
{
    Q_OBJECT

private slots:
    void theComputersLanguageWhenNoneIsChosen()
    {
        QCOMPARE(Language::resolve({}, {u"de-DE"_s, u"en-US"_s}), u"de"_s);
        QCOMPARE(Language::resolve({}, {u"fr-CA"_s}), u"fr"_s);
        QCOMPARE(Language::resolve({}, {u"hi-IN"_s}), u"hi"_s);
        QCOMPARE(Language::resolve({}, {u"id-ID"_s}), u"id"_s);
        QCOMPARE(Language::resolve({}, {u"ja-JP"_s}), u"ja"_s);
        QCOMPARE(Language::resolve({}, {u"ko-KR"_s}), u"ko"_s);
        QCOMPARE(Language::resolve({}, {u"ru-RU"_s}), u"ru"_s);
        QCOMPARE(Language::resolve({}, {u"es-MX"_s}), u"es"_s);
    }

    // The first of the computer's languages the app has; English first means English.
    void theFirstLanguageTheAppHas()
    {
        QCOMPARE(Language::resolve({}, {u"nl-NL"_s, u"es-ES"_s, u"de-DE"_s}), u"es"_s);
        QCOMPARE(Language::resolve({}, {u"en-US"_s, u"de-DE"_s}), QString());
        QCOMPARE(Language::resolve({}, {u"nl-NL"_s}), QString()); // (none it has: English)
        QCOMPARE(Language::resolve({}, {}), QString());
    }

    // Portuguese is the Brazilian translation; Chinese is Simplified only
    // (Taiwan and Hong Kong read Traditional: English until it exists).
    void portugueseAndChinese()
    {
        QCOMPARE(Language::resolve({}, {u"pt-BR"_s}), u"pt_BR"_s);
        QCOMPARE(Language::resolve({}, {u"pt-PT"_s}), u"pt_BR"_s);
        QCOMPARE(Language::resolve({}, {u"zh-Hans-CN"_s}), u"zh_CN"_s);
        QCOMPARE(Language::resolve({}, {u"zh-CN"_s}), u"zh_CN"_s);
        QCOMPARE(Language::resolve({}, {u"zh"_s}), u"zh_CN"_s);
        QCOMPARE(Language::resolve({}, {u"zh-TW"_s}), QString());
        QCOMPARE(Language::resolve({}, {u"zh-Hant-HK"_s}), QString());
        // The script, when the tag names one, decides over the territory.
        QCOMPARE(Language::resolve({}, {u"zh-Hans-HK"_s}), u"zh_CN"_s);
        QCOMPARE(Language::resolve({}, {u"zh-Hant-CN"_s}), QString());
        QCOMPARE(Language::resolve({}, {u"zh-HK"_s}), QString());
    }

    // What the player chose in Settings wins, English included; a value the
    // app does not know (a setting from a later version) is ignored.
    void thePlayersChoiceWins()
    {
        QCOMPARE(Language::resolve(u"de"_s, {u"fr-FR"_s}), u"de"_s);
        QCOMPARE(Language::resolve(u"pt_BR"_s, {u"en-US"_s}), u"pt_BR"_s);
        QCOMPARE(Language::resolve(u"en"_s, {u"de-DE"_s}), QString());
        QCOMPARE(Language::resolve(u"xx"_s, {u"ja-JP"_s}), u"ja"_s);
    }

    // The ten languages, each named in itself (how a player finds theirs).
    void theLanguagesNameThemselves()
    {
        const auto& all = Language::available();
        QCOMPARE(all.size(), std::size_t{10});
        QStringList codes;
        for (const auto& language : all) {
            codes << language.code;
            QVERIFY(!language.name.isEmpty());
        }
        QCOMPARE(codes, (QStringList{u"hi"_s, u"pt_BR"_s, u"es"_s, u"id"_s, u"de"_s, u"fr"_s, u"ru"_s, u"ja"_s, u"ko"_s,
                                     u"zh_CN"_s}));
        QCOMPARE(all.at(4).name, u"Deutsch"_s);
        QCOMPARE(all.at(7).name, u"日本語"_s);
    }

    // Every language's translations come with the app, and change its words;
    // a language it does not have says so.
    void everyLanguagesTranslationsAreBundled()
    {
        QCOMPARE(Language::englishName(), u"English"_s);
        for (const auto& language : Language::available()) {
            {
                Language installed;
                const auto loaded = installed.install(language.code);
                QVERIFY2(loaded.has_value(), loaded ? "" : qPrintable(loaded.error().message));
                // ("English", as the language picker says it: in that language.)
                QVERIFY2(Language::englishName() != u"English"_s, qPrintable(language.code));
            }
            QCOMPARE(Language::englishName(), u"English"_s); // (gone with it)
        }
        Language none;
        const auto missing = none.install(u"xx"_s);
        QVERIFY(!missing.has_value());
        QVERIFY(missing.error().message.contains(u"xx"_s));
    }
};

QTEST_MAIN(TestLanguage)
#include "tst_language.moc"
