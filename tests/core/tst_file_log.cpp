#include "gigchain/core/FileLog.h"

#include <QFile>
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <QtTest>

using namespace gigchain::core;
using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcTest, "gigchain.test")

namespace {

QString readAll(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll());
}

} // namespace

class TestFileLog : public QObject
{
    Q_OBJECT

private slots:
    void writesWarningsWithLevelAndCategory()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"logs/openstage.log"_s);
        QVERIFY(FileLog::install(path).has_value());
        qCWarning(lcTest) << "plugin exploded";
        qCInfo(lcTest) << "loaded fine";
        FileLog::uninstall();

        const QString text = readAll(path);
        QVERIFY2(text.contains(u"warning"_s) && text.contains(u"gigchain.test"_s) &&
                     text.contains(u"plugin exploded"_s),
                 qPrintable(text));
        QVERIFY(text.contains(u"loaded fine"_s));
    }

    void keepsEarlierSessions()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"gigchain.log"_s);
        QVERIFY(FileLog::install(path).has_value());
        qCWarning(lcTest) << "first session";
        FileLog::uninstall();
        QVERIFY(FileLog::install(path).has_value());
        qCWarning(lcTest) << "second session";
        FileLog::uninstall();

        const QString text = readAll(path);
        QVERIFY(text.contains(u"first session"_s));
        QVERIFY(text.contains(u"second session"_s));
    }

    void rotatesWhenTooLarge()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"gigchain.log"_s);
        {
            QFile big(path);
            QVERIFY(big.open(QIODevice::WriteOnly));
            big.write(QByteArray(static_cast<qsizetype>(FileLog::kMaxBytes + 1), 'x'));
        }
        QVERIFY(FileLog::install(path).has_value());
        FileLog::uninstall();
        QVERIFY(QFile::exists(path + u".1"_s));
        QVERIFY(QFileInfo(path).size() < FileLog::kMaxBytes);
    }

    void unwritableLocationIsAnError()
    {
        QTemporaryDir dir;
        const QString blocker = dir.filePath(u"blocker"_s);
        {
            QFile file(blocker); // a file where the log folder should be
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        const auto installed = FileLog::install(blocker + u"/openstage.log"_s);
        QVERIFY(!installed);
        QVERIFY(installed.error().code == ErrorCode::FileWriteFailed);
        QVERIFY(!installed.error().message.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestFileLog)
#include "tst_file_log.moc"
