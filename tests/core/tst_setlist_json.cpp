#include "LeakCheck.h"
#include "gigchain/core/Limits.h"
#include "gigchain/core/SetlistJson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace gigchain::core;
using namespace Qt::StringLiterals;

namespace {

Setlist richSetlist()
{
    Setlist setlist;
    Song song = makeSong(QStringLiteral("Café ☕ 🎹"));
    Channel piano = makeChannel(QStringLiteral("Piano"));
    piano.instrument = PluginSlot{QStringLiteral("fake.grand-piano"), QStringLiteral("Grand Piano"), false};
    piano.effects.push_back(PluginSlot{QStringLiteral("fake.eq"), QStringLiteral("Channel EQ"), true});
    piano.volumeDb = -6.5;
    piano.keyLow = 21;
    piano.keyHigh = 59;
    piano.transpose = -12;
    piano.midiChannel = 3;
    piano.mute = true;
    piano.pan = -0.25;
    Channel empty = makeChannel(QStringLiteral("Spare"));
    song.patches.front().channels = {piano, empty};
    song.patches.push_back(makePatch(QStringLiteral("Chorus")));
    setlist.songs.push_back(song);
    setlist.songs.push_back(makeSong(QStringLiteral("Second")));
    return setlist;
}

// The JSON for richSetlist(), as an object tests can tamper with.
QJsonObject richJson() { return QJsonDocument::fromJson(toJson(richSetlist())).object(); }

// richSetlist() JSON with songs[0].patches[0].channels[0][key] replaced.
QByteArray withFirstChannelField(const QString& key, const QJsonValue& value)
{
    QJsonObject root = richJson();
    QJsonArray songs = root.value(u"songs").toArray();
    QJsonObject song = songs.at(0).toObject();
    QJsonArray patches = song.value(u"patches").toArray();
    QJsonObject patch = patches.at(0).toObject();
    QJsonArray channels = patch.value(u"channels").toArray();
    QJsonObject channel = channels.at(0).toObject();
    channel.insert(key, value);
    channels.replace(0, channel);
    patch.insert(u"channels", channels);
    patches.replace(0, patch);
    song.insert(u"patches", patches);
    songs.replace(0, song);
    root.insert(u"songs", songs);
    return QJsonDocument(root).toJson();
}

} // namespace

class TestSetlistJson : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsEveryField()
    {
        const Setlist original = richSetlist();
        const auto parsed = fromJson(toJson(original));
        QVERIFY2(parsed.has_value(), parsed ? "" : qPrintable(parsed.error().message));
        QVERIFY(*parsed == original);
    }

    void writesFormatVersion()
    {
        const QJsonObject root = richJson();
        QCOMPARE(root.value(u"formatVersion").toInt(), kSetlistFormatVersion);
    }

    void filesWithoutPanLoadCentred()
    {
        QJsonObject root = richJson();
        QJsonArray songs = root.value(u"songs").toArray();
        QJsonObject song = songs.at(0).toObject();
        QJsonArray patches = song.value(u"patches").toArray();
        QJsonObject patch = patches.at(0).toObject();
        QJsonArray channels = patch.value(u"channels").toArray();
        QJsonObject channel = channels.at(0).toObject();
        channel.remove(u"pan");
        channels.replace(0, channel);
        patch.insert(u"channels", channels);
        patches.replace(0, patch);
        song.insert(u"patches", patches);
        songs.replace(0, song);
        root.insert(u"songs", songs);
        const auto parsed = fromJson(QJsonDocument(root).toJson());
        QVERIFY2(parsed.has_value(), parsed ? "" : qPrintable(parsed.error().message));
        QCOMPARE(parsed->songs[0].patches[0].channels[0].pan, 0.0);
    }

    void rejectsNonJson()
    {
        const auto parsed = fromJson("{ not json");
        QVERIFY(!parsed);
        QVERIFY(parsed.error().code == ErrorCode::ParseFailed);
    }

    void rejectsTopLevelArray()
    {
        const auto parsed = fromJson("[]");
        QVERIFY(!parsed);
        QVERIFY(parsed.error().code == ErrorCode::ParseFailed);
    }

    void rejectsTruncatedFile()
    {
        const QByteArray full = toJson(richSetlist());
        const auto parsed = fromJson(full.left(full.size() / 2));
        QVERIFY(!parsed);
        QVERIFY(parsed.error().code == ErrorCode::ParseFailed);
    }

    void rejectsFutureVersion()
    {
        QJsonObject root = richJson();
        root.insert(u"formatVersion", 2);
        const auto parsed = fromJson(QJsonDocument(root).toJson());
        QVERIFY(!parsed);
        QVERIFY(parsed.error().code == ErrorCode::UnsupportedVersion);
    }

    void rejectsMissingVersion()
    {
        QJsonObject root = richJson();
        root.remove(u"formatVersion");
        const auto parsed = fromJson(QJsonDocument(root).toJson());
        QVERIFY(!parsed);
        QVERIFY(parsed.error().code == ErrorCode::InvalidData);
        QVERIFY(parsed.error().message.contains(u"formatVersion"));
    }

    void rejectsOversizedInput()
    {
        const QByteArray huge(static_cast<qsizetype>(limits::kMaxFileBytes + 1), ' ');
        const auto parsed = fromJson(huge);
        QVERIFY(!parsed);
        QVERIFY(parsed.error().code == ErrorCode::FileTooLarge);
    }

    void rejectsTooManySongsBeforeReadingThem()
    {
        QJsonArray songs;
        for (int i = 0; i <= limits::kMaxSongs; ++i) {
            songs.append(QJsonObject{});
        }
        const QJsonObject root{{u"formatVersion"_s, 1}, {u"songs"_s, songs}};
        const auto parsed = fromJson(QJsonDocument(root).toJson());
        QVERIFY(!parsed);
        QVERIFY(parsed.error().code == ErrorCode::LimitExceeded);
    }

    void rejectsWrongTypes()
    {
        const auto parsed = fromJson(withFirstChannelField(u"name"_s, 42));
        QVERIFY(!parsed);
        QVERIFY(parsed.error().code == ErrorCode::InvalidData);
        QVERIFY(parsed.error().message.contains(u"songs[0].patches[0].channels[0].name"));
    }

    void rejectsNonIntegralAndHugeNumbers()
    {
        const auto fractional = fromJson(withFirstChannelField(u"keyLow"_s, 60.5));
        QVERIFY(!fractional);
        QVERIFY(fractional.error().code == ErrorCode::InvalidData);
        QVERIFY(fractional.error().message.contains(u"keyLow"));

        const auto outOfRange = fromJson(withFirstChannelField(u"keyLow"_s, 200));
        QVERIFY(!outOfRange);
        QVERIFY(outOfRange.error().code == ErrorCode::OutOfRange);

        const auto badPan = fromJson(withFirstChannelField(u"pan"_s, 3));
        QVERIFY(!badPan);
        QVERIFY(badPan.error().code == ErrorCode::OutOfRange);

        const auto hugeVolume = fromJson(withFirstChannelField(u"volumeDb"_s, 1e308));
        QVERIFY(!hugeVolume);
        QVERIFY(hugeVolume.error().code == ErrorCode::OutOfRange);

        QJsonObject root = richJson();
        root.insert(u"formatVersion", 0);
        const auto zeroVersion = fromJson(QJsonDocument(root).toJson());
        QVERIFY(!zeroVersion);
        QVERIFY(zeroVersion.error().code == ErrorCode::OutOfRange);
    }

    void rejectsDuplicateIds()
    {
        QJsonObject root = richJson();
        QJsonArray songs = root.value(u"songs").toArray();
        songs.append(songs.at(0));
        root.insert(u"songs", songs);
        const auto parsed = fromJson(QJsonDocument(root).toJson());
        QVERIFY(!parsed);
        QVERIFY(parsed.error().code == ErrorCode::InvalidData);
    }

    void parsingDoesNotLeak()
    {
        const QByteArray good = toJson(richSetlist());
        const QByteArray bad = withFirstChannelField(u"keyLow"_s, 500);
        QCOMPARE(gigchain::test::leakedBlocks([&] {
                     const auto a = fromJson(good);
                     const auto b = fromJson(bad);
                     Q_UNUSED(a);
                     Q_UNUSED(b);
                 }),
                 0LL);
    }
};

QTEST_GUILESS_MAIN(TestSetlistJson)
#include "tst_setlist_json.moc"
