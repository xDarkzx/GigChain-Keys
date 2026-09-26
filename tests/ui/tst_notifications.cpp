// Messages for the user: each with its level, never piling up, the same
// message not repeated, and worse news shown longer.
#include "Notifications.h"

#include <QSignalSpy>
#include <QtTest>

using namespace gigchain::ui;
using namespace Qt::StringLiterals;

class TestNotifications : public QObject
{
    Q_OBJECT

private slots:
    void aMessageIsShownWithItsLevel()
    {
        Notifications n;
        n.post(u"MIDI input connected: Impact GXP61"_s, Notifications::Info);
        QCOMPARE(n.rowCount(), 1);
        QCOMPARE(n.text(0), u"MIDI input connected: Impact GXP61"_s);
        QCOMPARE(n.level(0), Notifications::Info);
    }

    void theSameMessageAgainIsNotRepeated()
    {
        Notifications n;
        n.post(u"MIDI input connected: A"_s, Notifications::Info);
        QSignalSpy changed(&n, &QAbstractItemModel::dataChanged);
        n.post(u"MIDI input connected: A"_s, Notifications::Info);
        QCOMPARE(n.rowCount(), 1);
        QCOMPARE(n.data(n.index(0), Notifications::RepeatsRole).toInt(), 2); // shown again, counted
        QCOMPARE(changed.count(), 1);
    }

    void onlyTheLatestFewAreShown()
    {
        Notifications n;
        for (int i = 0; i < Notifications::kMaxShown + 2; ++i) n.post(u"message %1"_s.arg(i), Notifications::Info);
        QCOMPARE(n.rowCount(), Notifications::kMaxShown);
        QCOMPARE(n.text(0), u"message 2"_s); // the oldest went first
        QCOMPARE(n.text(Notifications::kMaxShown - 1), u"message %1"_s.arg(Notifications::kMaxShown + 1));
    }

    void dismissingRemovesOnlyThatOne()
    {
        Notifications n;
        n.post(u"first"_s, Notifications::Info);
        n.post(u"second"_s, Notifications::Error);
        const int first = n.data(n.index(0), Notifications::IdRole).toInt();
        n.dismiss(first);
        QCOMPARE(n.rowCount(), 1);
        QCOMPARE(n.text(0), u"second"_s);
        n.dismiss(first); // already gone: nothing happens
        QCOMPARE(n.rowCount(), 1);
    }

    // cppcheck-suppress functionStatic ; a Qt Test slot, called through moc: cannot be static
    void worseNewsStaysLonger()
    {
        QVERIFY(Notifications::shownFor(Notifications::Info) > 0);
        QVERIFY(Notifications::shownFor(Notifications::Warning) > Notifications::shownFor(Notifications::Info));
        QVERIFY(Notifications::shownFor(Notifications::Error) > Notifications::shownFor(Notifications::Warning));
    }

    void emptyMessagesAreIgnored()
    {
        Notifications n;
        n.post(QString(), Notifications::Error);
        QCOMPARE(n.rowCount(), 0);
    }
};

QTEST_GUILESS_MAIN(TestNotifications)
#include "tst_notifications.moc"
