// Inviting players in, never nagging: the welcome once, What's new once per
// update, at most two support asks in the app's life.
#include "CommunityController.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace gigchain::ui;
using namespace Qt::StringLiterals;

namespace {

const QString kChangelog = uR"(# Changelog

## [Unreleased]

- **Not yet** out.

## [0.12.0] - 2026-12-09 (alpha)

The community update.

### Added

- **A welcome** on the first start, with the
  [Discord](https://discord.gg/x) link.
- What's new after each update.

### Changed

- `Help` has a Support page.

## [0.11.0] - 2026-11-16 (alpha)

- Older things.
)"_s;

} // namespace

class TestCommunity : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<QSettings> m_settings;

    [[nodiscard]] std::unique_ptr<CommunityController> start(const QString& version = u"0.12.0"_s,
                                                             const QDate& today = QDate(2026, 12, 10)) const
    {
        auto community = std::make_unique<CommunityController>(*m_settings, version, kChangelog);
        community->noteStart(today);
        return community;
    }

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_settings = std::make_unique<QSettings>(m_dir->filePath(u"s.ini"_s), QSettings::IniFormat);
    }

    // A version's bullets as plain text, a wrapped bullet as one line; the
    // next version's are not taken; a version not there has none.
    void aVersionsChangesAreReadFromTheChangelog()
    {
        QCOMPARE(CommunityController::changesFor(kChangelog, u"0.12.0"_s),
                 (QStringList{u"A welcome on the first start, with the Discord link."_s, u"What's new after each update."_s,
                              u"Help has a Support page."_s}));
        QCOMPARE(CommunityController::changesFor(kChangelog, u"0.11.0"_s), (QStringList{u"Older things."_s}));
        QVERIFY(CommunityController::changesFor(kChangelog, u"9.9.9"_s).isEmpty());
    }

    // The app's own CHANGELOG is bundled and has this version's changes.
    void theBundledChangelogIsRead()
    {
        const CommunityController community(*m_settings, u"0.11.0"_s);
        QVERIFY2(!community.whatsNew().isEmpty(), "no changes for 0.11.0 in :/changelog/CHANGELOG.md");
    }

    // The very first start: the welcome (not What's new), once.
    void aFirstStartIsWelcomedOnce()
    {
        auto first = start();
        QVERIFY(first->welcomeDue());
        QVERIFY(!first->whatsNewDue());
        first->welcomeSeen();
        QVERIFY(!first->welcomeDue());
        auto next = start();
        QVERIFY(!next->welcomeDue());
        QVERIFY(!next->whatsNewDue());
    }

    // Someone who used an earlier version: What's new (no welcome), once per version.
    void anUpdateShowsWhatsNewOncePerVersion()
    {
        m_settings->setValue(u"session/lastFile"_s, u"C:/gig.gigchain"_s); // (they had used it)
        auto updated = start();
        QVERIFY(!updated->welcomeDue());
        QVERIFY(updated->whatsNewDue());
        updated->whatsNewSeen();
        QVERIFY(!start()->whatsNewDue()); // the same version again: nothing
        m_settings->setValue(u"community/lastVersion"_s, u"0.11.0"_s);
        QVERIFY(start()->whatsNewDue());  // the next update: again
        QVERIFY(!start(u"9.9.9"_s)->whatsNewDue()); // (a version with no notes: nothing to show)
    }

    // Two asks in the app's life: after a real use of Perform, and after a
    // month; each once, and none at all after "Don't ask again".
    void atMostTwoSupportAsksEver()
    {
        auto community = start(u"0.12.0"_s, QDate(2026, 12, 1));
        QCOMPARE(community->supportAskDue(), QString());
        community->addPerformMinutes(CommunityController::kPerformMinutes - 1);
        QCOMPARE(community->supportAskDue(), QString()); // not yet a real use
        QSignalSpy changed(community.get(), &CommunityController::changed);
        community->addPerformMinutes(1);
        QCOMPARE(community->supportAskDue(), u"perform"_s);
        QCOMPARE(changed.count(), 1);
        community->supportAsked(u"perform"_s);
        QCOMPARE(community->supportAskDue(), QString());

        auto monthLater = start(u"0.12.0"_s, QDate(2026, 12, 31));
        QCOMPARE(monthLater->supportAskDue(), u"month"_s);
        monthLater->supportAsked(u"month"_s);
        QCOMPARE(start(u"0.12.0"_s, QDate(2027, 6, 1))->supportAskDue(), QString()); // never a third
    }

    void dontAskAgainEndsTheAsks()
    {
        auto community = start();
        community->addPerformMinutes(60);
        QCOMPARE(community->supportAskDue(), u"perform"_s);
        community->neverAskAgain();
        QCOMPARE(community->supportAskDue(), QString());
        QCOMPARE(start(u"0.12.0"_s, QDate(2027, 6, 1))->supportAskDue(), QString());
    }

    // 0.11 asked once for a star: that was one of the two.
    void theOldStarAskCountsAsOne()
    {
        m_settings->setValue(u"session/starAsked"_s, true);
        auto community = start(u"0.12.0"_s, QDate(2026, 12, 1));
        QCOMPARE(start(u"0.12.0"_s, QDate(2027, 1, 30))->supportAskDue(), QString()); // the month's is spent
        community->addPerformMinutes(30);
        QCOMPARE(community->supportAskDue(), u"perform"_s);
        QVERIFY(!m_settings->contains(u"session/starAsked"_s));
    }

    // Minutes count only while Perform is on.
    void performMinutesAddUpOnlyWhilePerforming()
    {
        auto community = start();
        community->performing(false); // (off, never on: nothing)
        QVERIFY(!m_settings->contains(u"community/performMs"_s));
        community->performing(true);
        QTest::qWait(30);
        community->performing(false);
        const qint64 spent = m_settings->value(u"community/performMs"_s, 0).toLongLong();
        QVERIFY2(spent >= 20 && spent < 60000, qPrintable(QString::number(spent))); // (kept to the millisecond)
    }

    // Short visits to Perform add up: forty half-minutes are the twenty minutes.
    void shortPerformVisitsAddUp()
    {
        auto community = start();
        for (int i = 0; i < 39; ++i) community->addPerformTime(30000);
        QCOMPARE(community->supportAskDue(), QString());
        community->addPerformTime(30000);
        QCOMPARE(community->supportAskDue(), u"perform"_s);
    }

    // A changelog that can't be read: the cause kept, and the update's news
    // still owed (shown on a later start, once it can be read).
    void anUnreadableChangelogKeepsTheNewsOwed()
    {
        m_settings->setValue(u"community/lastVersion"_s, u"0.11.0"_s);
        CommunityController broken(*m_settings, u"0.12.0"_s, gigchain::core::fail(gigchain::core::ErrorCode::FileReadFailed, u"no such file"_s));
        broken.noteStart(QDate(2026, 12, 10));
        QVERIFY(broken.changelogError().contains(u"no such file"_s));
        QVERIFY(!broken.whatsNewDue());
        QCOMPARE(m_settings->value(u"community/lastVersion"_s).toString(), u"0.11.0"_s);
        QVERIFY(start()->whatsNewDue());
    }

    // Without a test date every check reads the clock: a month after the
    // first start the ask is due, however long ago the app was opened.
    void theMonthAskReadsTheClock()
    {
        m_settings->setValue(u"community/firstStart"_s, QDate::currentDate().addDays(-CommunityController::kMonthDays).toString(Qt::ISODate));
        CommunityController community(*m_settings, u"0.12.0"_s, kChangelog);
        community.noteStart();
        QCOMPARE(community.supportAskDue(), u"month"_s);
    }

    // "Don't ask again" turns every invitation off (What's new hides its own).
    void dontAskAgainTurnsInvitationsOff()
    {
        auto community = start();
        QVERIFY(!community->asksOff());
        QSignalSpy changed(community.get(), &CommunityController::changed);
        community->neverAskAgain();
        QVERIFY(community->asksOff());
        QCOMPARE(changed.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestCommunity)
#include "tst_community.moc"
