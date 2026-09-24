#include "gigchain/core/Limits.h"
#include "gigchain/core/SetlistFile.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace gigchain::core;
using namespace Qt::StringLiterals;

namespace {

Setlist sample()
{
    Setlist setlist;
    setlist.songs.push_back(makeSong(u"Opener"_s));
    return setlist;
}

QByteArray readAll(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

bool writeAll(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

} // namespace

class TestSetlistFile : public QObject
{
    Q_OBJECT

private slots:
    void savesAndLoadsRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"gig.gigchain.json"_s);
        const Setlist original = sample();
        QVERIFY(saveSetlistFile(original, path).has_value());
        const auto loaded = loadSetlistFile(path);
        QVERIFY(loaded.has_value());
        QVERIFY(*loaded == original);
        QVERIFY(readAll(path).contains("\"formatVersion\": 1"));
    }

    void missingFileIsReported()
    {
        QTemporaryDir dir;
        const auto loaded = loadSetlistFile(dir.filePath(u"nope.gigchain.json"_s));
        QVERIFY(!loaded);
        QVERIFY(loaded.error().code == ErrorCode::FileNotFound);
    }

    void directoryIsNotAFile()
    {
        QTemporaryDir dir;
        const auto loaded = loadSetlistFile(dir.path());
        QVERIFY(!loaded);
        QVERIFY(loaded.error().code == ErrorCode::FileReadFailed);
    }

    void oversizedFileIsRejected()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"huge.gigchain.json"_s);
        QVERIFY(writeAll(path, QByteArray(static_cast<qsizetype>(limits::kMaxFileBytes + 1), ' ')));
        const auto loaded = loadSetlistFile(path);
        QVERIFY(!loaded);
        QVERIFY(loaded.error().code == ErrorCode::FileTooLarge);
    }

    void garbageFileIsParseError()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"garbage.gigchain.json"_s);
        QVERIFY(writeAll(path, "\x00\xff not json"));
        const auto loaded = loadSetlistFile(path);
        QVERIFY(!loaded);
        QVERIFY(loaded.error().code == ErrorCode::ParseFailed);
    }

    void failedSaveLeavesExistingFileUntouched()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"gig.gigchain.json"_s);
        QVERIFY(saveSetlistFile(sample(), path).has_value());
        const QByteArray before = readAll(path);

        Setlist invalid = sample();
        invalid.songs.front().name = u"   "_s;
        const auto saved = saveSetlistFile(invalid, path);
        QVERIFY(!saved);
        QVERIFY(saved.error().code == ErrorCode::InvalidData);
        QCOMPARE(readAll(path), before);
    }

    void saveIntoMissingFolderFails()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"missing/folder/gig.gigchain.json"_s);
        const auto saved = saveSetlistFile(sample(), path);
        QVERIFY(!saved);
        QVERIFY(saved.error().code == ErrorCode::FileWriteFailed);
        QVERIFY(!QFile::exists(path));
    }
};

QTEST_GUILESS_MAIN(TestSetlistFile)
#include "tst_setlist_file.moc"
