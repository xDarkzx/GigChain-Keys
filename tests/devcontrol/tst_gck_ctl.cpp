// gck-ctl, the live test control's command line, against a control in this test.
#include "DevControl.h"

#include "gigchain/platform/Process.h"

#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

using namespace gigchain;
using namespace gigchain::devcontrol;
using namespace Qt::StringLiterals;

class TestGckCtl : public QObject
{
    Q_OBJECT

    std::unique_ptr<DevControl> m_control;
    QString m_name;

    struct Run
    {
        int code = -1;
        QString output;
    };

    // Runs gck-ctl with `args`; this test's event loop answers it meanwhile.
    Run run(const QStringList& args) const
    {
        QProcess tool;
        tool.setProcessChannelMode(QProcess::MergedChannels);
        tool.start(QCoreApplication::applicationDirPath() + u'/' + platform::programFileName(u"gck-ctl"_s), args);
        if (!tool.waitForStarted(5000)) return {};
        if (!QTest::qWaitFor([&tool] { return tool.state() == QProcess::NotRunning; }, 15000)) return {};
        return Run{.code = tool.exitCode(), .output = QString::fromUtf8(tool.readAll())};
    }

private slots:
    void init()
    {
        m_name = u"gck-cli-"_s + QUuid::createUuid().toString(QUuid::Id128).left(12);
        m_control = std::make_unique<DevControl>(DevControl::Context{.socketName = m_name});
        QVERIFY(m_control->listen().has_value());
    }

    void cleanup() { m_control.reset(); }

    void aCommandPrintsItsAnswer()
    {
        const Run help = run({u"--socket"_s, m_name, u"help"_s});
        QCOMPARE(help.code, 0);
        QVERIFY2(help.output.contains(u"\"commands\""_s), qPrintable(help.output));
    }

    void anErrorExitsWithOne()
    {
        const Run jump = run({u"--socket"_s, m_name, u"jump"_s, u"3"_s});
        QCOMPARE(jump.code, 1);
        QVERIFY2(jump.output.contains(u"No such command: jump"_s), qPrintable(jump.output));
    }

    void nothingListeningExitsWithTwo()
    {
        const Run nobody = run({u"--socket"_s, m_name + u"-nobody"_s, u"help"_s});
        QCOMPARE(nobody.code, 2);
        QVERIFY2(nobody.output.contains(u"not running"_s), qPrintable(nobody.output));
    }

    // Numbers go as numbers, quoted words as one text.
    void argumentsKeepTheirKinds()
    {
        QJsonArray seen;
        m_control->add(u"echo"_s, u"echo"_s, [&seen](const DevRequest& request, const Reply& reply) {
            seen = request.args;
            reply.ok();
        });
        QCOMPARE(run({u"--socket"_s, m_name, u"echo"_s, u"60"_s, u"KeyLab mkII 88"_s, u"0.5"_s}).code, 0);
        QCOMPARE(seen, (QJsonArray{60, u"KeyLab mkII 88"_s, 0.5}));
    }

    void aCommandFileStopsAtTheFirstError()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(u"check.txt"_s);
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write("# a check\nhelp\n\njump \"two words\"\nhelp\n");
        out.close();
        int helps = 0;
        m_control->add(u"help"_s, u"help"_s, [&helps](const DevRequest&, const Reply& reply) {
            ++helps;
            reply.ok();
        });
        const Run checked = run({u"--socket"_s, m_name, u"run"_s, file});
        QCOMPARE(checked.code, 1);
        QCOMPARE(helps, 1); // stopped at jump
        QVERIFY2(checked.output.contains(u"jump"_s), qPrintable(checked.output));
    }
};

QTEST_MAIN(TestGckCtl)
#include "tst_gck_ctl.moc"
