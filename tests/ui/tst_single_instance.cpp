// One app at a time: a second start hands its setlist to the running app.
#include "SingleInstance.h"

#include <QSignalSpy>
#include <QUuid>
#include <QtTest>

#include <future>

using namespace gigchain::ui;
using namespace Qt::StringLiterals;

class TestSingleInstance : public QObject
{
    Q_OBJECT

    // A name of this test's own, so the app running meanwhile is not asked.
    static QString uniqueName() { return u"tst_single_instance-"_s + QUuid::createUuid().toString(QUuid::WithoutBraces); }

    // A later start is another process: here, another thread (the running
    // one reads in its event loop, which QTRY_ keeps turning).
    static std::future<bool> startAgain(const QString& name, const QString& path)
    {
        return std::async(std::launch::async, [name, path] { return SingleInstance(name).handOver(path); });
    }

private slots:
    void onlyTheFirstStartIsFirst()
    {
        const QString name = uniqueName();
        {
            SingleInstance running(name);
            QVERIFY(running.first());
            SingleInstance again(name);
            QVERIFY(!again.first());
        }
        SingleInstance afterItEnded(name); // the app was closed: the next start is the first
        QVERIFY(afterItEnded.first());
    }

    // A setlist double-clicked while the app runs: the running app opens it.
    void theRunningAppGetsTheSetlist()
    {
        const QString name = uniqueName();
        SingleInstance running(name);
        QVERIFY(running.first());
        QVERIFY2(running.listen().has_value(), "could not listen");
        QSignalSpy opened(&running, &SingleInstance::opened);

        auto handed = startAgain(name, u"C:/Gigs/Friday Night (late).gigchain"_s);
        QTRY_COMPARE(opened.size(), 1);
        QVERIFY(handed.get());
        QCOMPARE(opened.at(0).at(0).toString(), u"C:/Gigs/Friday Night (late).gigchain"_s);

        // Started again with no setlist: the running app only comes to the front.
        handed = startAgain(name, QString());
        QTRY_COMPARE(opened.size(), 2);
        QVERIFY(handed.get());
        QCOMPARE(opened.at(1).at(0).toString(), QString());
    }

    // Names outside Latin-1 arrive whole.
    void anyNameArrivesWhole()
    {
        const QString name = uniqueName();
        SingleInstance running(name);
        QVERIFY(running.first());
        QVERIFY(running.listen().has_value());
        QSignalSpy opened(&running, &SingleInstance::opened);
        const QString path = u"C:/Música/Répertoire — 日本.gigchain"_s;
        auto handed = startAgain(name, path);
        QTRY_COMPARE(opened.size(), 1);
        QVERIFY(handed.get());
        QCOMPARE(opened.at(0).at(0).toString(), path);
    }

    // The running app not listening (it could not): said, not hidden.
    void aRunningAppThatDoesNotListenIsReported()
    {
        const QString name = uniqueName();
        SingleInstance running(name);
        QVERIFY(running.first());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Could not reach the running app"_s));
        QVERIFY(!SingleInstance(name).handOver(u"C:/Gigs/x.gigchain"_s));
    }
};

QTEST_GUILESS_MAIN(TestSingleInstance)
#include "tst_single_instance.moc"
