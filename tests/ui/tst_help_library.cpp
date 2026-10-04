// The user guide inside the app (Help > User guide): its pages, the links
// between them, and the search over them.
#include "HelpLibrary.h"

#include <QRegularExpression>
#include <QtTest>

using namespace gigchain::ui;
using namespace Qt::StringLiterals;

class TestHelpLibrary : public QObject
{
    Q_OBJECT

    static QStringList ids()
    {
        QStringList list;
        for (const QVariant& topic : HelpLibrary::topics()) list << topic.toMap().value(u"id"_s).toString();
        return list;
    }

private slots:
    // Every topic has a title, a group and a page that reads.
    void everyTopicHasAPage()
    {
        const QVariantList topics = HelpLibrary::topics();
        QVERIFY(topics.size() >= 10);
        for (const QVariant& t : topics) {
            const QVariantMap topic = t.toMap();
            const QString id = topic.value(u"id"_s).toString();
            QVERIFY2(!topic.value(u"title"_s).toString().isEmpty(), qPrintable(id));
            QVERIFY2(!topic.value(u"group"_s).toString().isEmpty(), qPrintable(id));
            const QString page = HelpLibrary::page(id);
            QVERIFY2(page.startsWith(u"# "_s), qPrintable(id + u": "_s + page.left(80)));
            QVERIFY2(page.size() > 400, qPrintable(id)); // a real page, not a stub
        }
    }

    // The guide covers what a player needs.
    void theGuideCoversTheBasics()
    {
        const QStringList list = ids();
        for (const QString& needed : {u"getting-started"_s, u"audio-and-midi"_s, u"instruments"_s, u"charts"_s,
                                      u"perform"_s, u"practice"_s, u"shortcuts"_s, u"troubleshooting"_s}) {
            QVERIFY2(list.contains(needed), qPrintable(needed));
        }
        QCOMPARE(list.first(), u"getting-started"_s); // what opens first
    }

    // A link to another page ([text](help:practice)) always leads somewhere.
    void everyLinkLeadsToAPage()
    {
        const QStringList list = ids();
        static const QRegularExpression link(u"\\]\\(help:([a-z0-9-]+)\\)"_s);
        int links = 0;
        for (const QString& id : list) {
            auto it = link.globalMatch(HelpLibrary::page(id));
            while (it.hasNext()) {
                const QString target = it.next().captured(1);
                QVERIFY2(list.contains(target), qPrintable(id + u" links to "_s + target));
                ++links;
            }
        }
        QVERIFY(links >= 10); // the pages lead to each other
    }

    // An unknown page says so (never a blank window).
    void anUnknownPageSaysSo()
    {
        const QString page = HelpLibrary::page(u"no-such-page"_s);
        QVERIFY(page.contains(u"no-such-page"_s));
        QVERIFY(page.startsWith(u"# "_s));
    }

    // A page shown in the window: its links in the colour asked for (Qt's
    // own dark blue cannot be read on the dark theme).
    void aPageIsShownWithItsLinksInTheThemesColour()
    {
        const QString html = HelpLibrary::toHtml(u"# Title\n\nSee [the charts](help:charts) and **this**.\n"_s, u"#7ab8ff"_s);
        QVERIFY(html.contains(u"href=\"help:charts\""_s));
        QVERIFY2(html.contains(u"#7ab8ff"_s, Qt::CaseInsensitive), qPrintable(html));
        QVERIFY2(!html.contains(u"#0000ff"_s, Qt::CaseInsensitive), qPrintable(html));
        QVERIFY(html.contains(u"Title"_s));
        // The window's font and size, not the document's own.
        const QRegularExpression bodyFont(u"<body[^>]*font-(size|family)"_s);
        QVERIFY2(!bodyFont.match(html).hasMatch(), qPrintable(html.left(400)));
    }

    // Search: every word must be in the page, any case; titles first.
    void searchFindsThePagesWithEveryWord()
    {
        const QVariantList found = HelpLibrary::search(u"WAIT for ME"_s);
        QVERIFY(!found.isEmpty());
        QCOMPARE(found.first().toMap().value(u"id"_s).toString(), u"practice"_s);
        QVERIFY(!found.first().toMap().value(u"snippet"_s).toString().isEmpty());

        const QVariantList looper = HelpLibrary::search(u"loop"_s);
        QVERIFY(!looper.isEmpty());
        QCOMPARE(looper.first().toMap().value(u"id"_s).toString(), u"looper"_s); // its title has the word

        QVERIFY(HelpLibrary::search(u"zzqx nothing"_s).isEmpty());
        QVERIFY(HelpLibrary::search(u"   "_s).isEmpty());
    }
};

QTEST_MAIN(TestHelpLibrary) // (a page laid out as HTML needs fonts)
#include "tst_help_library.moc"
