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
    piano.instrument = PluginSlot{.pluginId = QStringLiteral("fake.grand-piano"), .displayName = QStringLiteral("Grand Piano"),
                                  .bypass = false,
                                  .state = QByteArray("GCS1\x00\x01\xff binary sound settings", 30)}; // opaque to core
    piano.effects.push_back(
        PluginSlot{.pluginId = QStringLiteral("fake.eq"), .displayName = QStringLiteral("Channel EQ"), .bypass = true, .state = {}});
    piano.volumeDb = -6.5;
    piano.keyLow = 21;
    piano.keyHigh = 59;
    piano.transpose = -12;
    piano.midiChannel = 3;
    piano.mute = true;
    piano.pan = -0.25;
    piano.velocityLow = 20;
    piano.velocityHigh = 90;
    piano.mappings.push_back(ControlMapping{.midiChannel = 1, .controller = 74, .target = -1, .parameter = 4000000000U,
                                            .parameterName = QStringLiteral("Brightness"), .minimum = 0.25, .maximum = 0.8});
    piano.mappings.push_back(ControlMapping{.midiChannel = 0, .controller = 11, .target = 0, .parameter = 12,
                                            .parameterName = QStringLiteral("Gain"), .minimum = 1.0, .maximum = 0.0});
    Channel empty = makeChannel(QStringLiteral("Spare"));
    Channel mic = makeChannel(QStringLiteral("Vocal"));
    mic.inputLeft = 1;
    song.patches.front().channels = {piano, empty, mic};
    song.patches.push_back(makePatch(QStringLiteral("Chorus")));
    song.chart = QStringLiteral("{title: Café}\n[Dm]I love [C#m7]you so much[D/E]\n");
    song.key = QStringLiteral("Dm");
    song.tempo = 72.5;
    song.notes = QStringLiteral("Capo 2 on the guitar; keys play the pad");
    song.links.push_back(SongLink{QStringLiteral("Chords"), QStringLiteral("https://tabs.example/cafe")});
    song.attachments.push_back(QStringLiteral("cafe-chords.pdf"));
    song.backingTrack = QStringLiteral("cafe backing.mp3");
    song.timeNumerator = 6;
    song.timeDenominator = 8;
    song.switchEarly = true;
    song.sections.push_back(SectionSetup{.name = QStringLiteral("Verse 1"), .occurrence = 1, .bars = 8, .assigned = true,
                                         .channels = {piano.id, mic.id}});
    song.sections.push_back(SectionSetup{.name = QStringLiteral("Chorus"), .occurrence = 2, .bars = 0, .assigned = true,
                                         .channels = {}}); // a silent break
    song.loopSync = false;
    song.loopBars = 8;
    setlist.songs.push_back(song);
    setlist.loopControls.buttons.at(LoopControls::Record) = LearnedControl{.kind = 0xB0, .channel = 1, .number = 64};
    setlist.loopControls.buttons.at(LoopControls::NextChannel) = LearnedControl{.kind = 0x90, .channel = 10, .number = 36};
    setlist.loopControls.selector = LearnedControl{.kind = 0xB0, .channel = 1, .number = 21};
    setlist.loopControls.selectorMode = LoopControls::Relative;
    setlist.songs.push_back(makeSong(QStringLiteral("Second")));
    return setlist;
}

// The JSON for richSetlist(), as an object tests can tamper with.
QJsonObject richJson() { return QJsonDocument::fromJson(toJson(richSetlist())).object(); }

// richSetlist() JSON with songs[0][key] replaced.
QByteArray withFirstSongField(const QString& key, const QJsonValue& value)
{
    QJsonObject root = richJson();
    QJsonArray songs = root.value(u"songs").toArray();
    QJsonObject song = songs.at(0).toObject();
    song.insert(key, value);
    songs.replace(0, song);
    root.insert(u"songs", songs);
    return QJsonDocument(root).toJson();
}

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

    void pluginSettingsAreOptional()
    {
        QJsonObject slot{{u"pluginId"_s, u"fake.piano"_s}, {u"displayName"_s, u"Piano"_s}, {u"bypass"_s, false}};
        const auto parsed = fromJson(withFirstChannelField(u"instrument"_s, slot));
        QVERIFY2(parsed.has_value(), parsed ? "" : qPrintable(parsed.error().message));
        const auto& instrument = parsed->songs.at(0).patches.at(0).channels.at(0).instrument;
        if (!instrument) QFAIL("the instrument was lost");
        QVERIFY(instrument->state.isEmpty()); // the plugin's defaults
    }

    void rejectsBrokenPluginSettings()
    {
        QJsonObject slot{{u"pluginId"_s, u"fake.piano"_s}, {u"displayName"_s, u"Piano"_s}, {u"bypass"_s, false},
                         {u"state"_s, u"this is not base64 !!"_s}};
        const auto parsed = fromJson(withFirstChannelField(u"instrument"_s, slot));
        QVERIFY(!parsed);
        QVERIFY(parsed.error().code == ErrorCode::InvalidData);
        QVERIFY2(parsed.error().message.contains(u"instrument.state"_s), qPrintable(parsed.error().message));

        slot.insert(u"state"_s, QString::fromLatin1(QByteArray(limits::kMaxPluginStateBytes + 3, 'x').toBase64()));
        const auto huge = fromJson(withFirstChannelField(u"instrument"_s, slot));
        QVERIFY(!huge);
        QVERIFY(huge.error().code == ErrorCode::LimitExceeded || huge.error().code == ErrorCode::FileTooLarge);
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

    void versionTwoFilesOpenWithTheNewFieldsAtTheirDefaults()
    {
        // A file saved before velocity ranges, knobs, inputs and backing tracks.
        QJsonObject root = richJson();
        root.insert(u"formatVersion", 2);
        QJsonArray songs = root.value(u"songs").toArray();
        QJsonObject song = songs.at(0).toObject();
        song.remove(u"backingTrack");
        QJsonArray patches = song.value(u"patches").toArray();
        QJsonObject patch = patches.at(0).toObject();
        QJsonArray channels = patch.value(u"channels").toArray();
        QJsonObject channel = channels.at(0).toObject();
        for (const auto& key : {u"velocityLow", u"velocityHigh", u"inputLeft", u"inputRight", u"mappings"}) channel.remove(key);
        channels.replace(0, channel);
        patch.insert(u"channels", channels);
        patches.replace(0, patch);
        song.insert(u"patches", patches);
        songs.replace(0, song);
        root.insert(u"songs", songs);
        const auto parsed = fromJson(QJsonDocument(root).toJson());
        QVERIFY2(parsed.has_value(), parsed ? "" : qPrintable(parsed.error().message));
        const Channel& piano = parsed->songs.at(0).patches.at(0).channels.at(0);
        QCOMPARE(piano.velocityLow, 1);
        QCOMPARE(piano.velocityHigh, 127);
        QCOMPARE(piano.inputLeft, 0);
        QVERIFY(piano.mappings.empty());
        QVERIFY(parsed->songs.at(0).backingTrack.isEmpty());
    }

    void rejectsBadNewFields()
    {
        const auto inverted = fromJson(withFirstChannelField(u"velocityLow"_s, 100));
        QVERIFY(!inverted); // 100 above velocityHigh (90)
        QVERIFY2(inverted.error().message.contains(u"velocityLow"_s), qPrintable(inverted.error().message));
        QVERIFY(!fromJson(withFirstChannelField(u"velocityHigh"_s, 128)));
        QVERIFY(!fromJson(withFirstChannelField(u"inputRight"_s, 3))); // on the piano: inputRight without inputLeft
        const QJsonArray badTarget{QJsonObject{{u"midiChannel"_s, 0}, {u"controller"_s, 1}, {u"target"_s, 5},
                                               {u"parameter"_s, 1}, {u"parameterName"_s, u"x"_s},
                                               {u"minimum"_s, 0.0}, {u"maximum"_s, 1.0}}};
        const auto noSuchEffect = fromJson(withFirstChannelField(u"mappings"_s, badTarget));
        QVERIFY(!noSuchEffect);
        QVERIFY2(noSuchEffect.error().message.contains(u"target"_s), qPrintable(noSuchEffect.error().message));

        // A backing track is a plain file name in the setlist's folder, never a path.
        QJsonObject root = richJson();
        QJsonArray songs = root.value(u"songs").toArray();
        QJsonObject song = songs.at(0).toObject();
        song.insert(u"backingTrack", u"C:/Windows/system32/evil.wav"_s);
        songs.replace(0, song);
        root.insert(u"songs", songs);
        QVERIFY(!fromJson(QJsonDocument(root).toJson()));
    }

    void versionThreeFilesOpenWithoutSections()
    {
        QJsonObject root = richJson();
        root.insert(u"formatVersion", 3);
        QJsonArray songs = root.value(u"songs").toArray();
        QJsonObject song = songs.at(0).toObject();
        for (const auto& key : {u"timeSignature", u"switchEarly", u"sections", u"loopSync", u"loopBars"}) song.remove(key);
        songs.replace(0, song);
        root.insert(u"songs", songs);
        const auto parsed = fromJson(QJsonDocument(root).toJson());
        QVERIFY2(parsed.has_value(), parsed ? "" : qPrintable(parsed.error().message));
        const Song& s = parsed->songs.at(0);
        QCOMPARE(s.timeNumerator, 4);
        QCOMPARE(s.timeDenominator, 4);
        QVERIFY(!s.switchEarly);
        QVERIFY(s.sections.empty());
        QVERIFY(s.loopSync);
        QCOMPARE(s.loopBars, 4);
    }

    void rejectsBadSections()
    {
        QVERIFY(!fromJson(withFirstSongField(u"timeSignature"_s, u"5/5"_s)));
        QVERIFY(!fromJson(withFirstSongField(u"timeSignature"_s, u"four"_s)));
        QVERIFY(!fromJson(withFirstSongField(u"switchEarly"_s, u"yes"_s)));
        QVERIFY(!fromJson(withFirstSongField(u"loopBars"_s, 65)));
        QVERIFY(!fromJson(withFirstSongField(u"loopBars"_s, -1)));
        QVERIFY(fromJson(withFirstSongField(u"loopBars"_s, 0))); // open
        const auto section = [](const QJsonValue& bars, const QJsonValue& channel) {
            return QJsonArray{QJsonObject{{u"name"_s, u"Verse"_s}, {u"occurrence"_s, 1}, {u"bars"_s, bars},
                                          {u"assigned"_s, true}, {u"channels"_s, QJsonArray{channel}}}};
        };
        const auto tooLong = fromJson(withFirstSongField(u"sections"_s, section(1000, u"x"_s)));
        QVERIFY(!tooLong);
        QVERIFY2(tooLong.error().message.contains(u"bars"_s), qPrintable(tooLong.error().message));
        QVERIFY(!fromJson(withFirstSongField(u"sections"_s, section(4, u""_s))));  // an empty channel id
        QVERIFY(!fromJson(withFirstSongField(u"sections"_s, section(4, 12))));      // not text
        QJsonArray many;
        for (int i = 0; i <= limits::kMaxSectionsPerSong; ++i) many.append(section(4, u"x"_s).at(0));
        QVERIFY(!fromJson(withFirstSongField(u"sections"_s, many)));
        QVERIFY(fromJson(withFirstSongField(u"sections"_s, section(0, u"x"_s)))); // 0 = guessed
    }

    void rejectsBadLoopControls()
    {
        const auto withSelector = [](const QJsonObject& selector, int mode) {
            QJsonObject root = richJson();
            QJsonObject controls = root.value(u"loopControls").toObject();
            controls.insert(u"selector", selector);
            controls.insert(u"selectorMode", mode);
            root.insert(u"loopControls", controls);
            return QJsonDocument(root).toJson();
        };
        const QJsonObject knob{{u"kind"_s, 0xB0}, {u"channel"_s, 1}, {u"number"_s, 21}};
        QVERIFY(fromJson(withSelector(knob, 2)));
        QVERIFY(!fromJson(withSelector(knob, 3)));
        const QJsonObject note{{u"kind"_s, 0x90}, {u"channel"_s, 1}, {u"number"_s, 21}};
        const auto notAKnob = fromJson(withSelector(note, 0));
        QVERIFY(!notAKnob);
        QVERIFY2(notAKnob.error().message.contains(u"selector"_s), qPrintable(notAKnob.error().message));
        const QJsonObject noChannel{{u"kind"_s, 0xB0}, {u"channel"_s, 0}, {u"number"_s, 21}};
        QVERIFY(!fromJson(withSelector(noChannel, 0)));
        // Files from before: no controls learned, loops synced.
        QJsonObject root = richJson();
        root.remove(u"loopControls");
        const auto old = fromJson(QJsonDocument(root).toJson());
        QVERIFY(old);
        QVERIFY(old->loopControls == LoopControls{});
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

    void opensVersionOneFiles()
    {
        // Format 1 had no song charts: such files open with empty ones.
        QJsonObject root = richJson();
        root.insert(u"formatVersion", 1);
        QJsonArray songs = root.value(u"songs").toArray();
        QJsonObject song = songs.at(0).toObject();
        for (const char* key : {"chart", "key", "tempo", "notes", "links", "attachments"}) song.remove(QLatin1String(key));
        songs.replace(0, song);
        root.insert(u"songs", songs);
        const auto parsed = fromJson(QJsonDocument(root).toJson());
        QVERIFY2(parsed.has_value(), parsed ? "" : qPrintable(parsed.error().message));
        QVERIFY(parsed->songs[0].chart.isEmpty());
        QCOMPARE(parsed->songs[0].tempo, 0.0);
        QVERIFY(parsed->songs[0].links.empty());
    }

    void rejectsFutureVersion()
    {
        QJsonObject root = richJson();
        root.insert(u"formatVersion", kSetlistFormatVersion + 1);
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

    void aSongFollowsItsChordsUnlessToldOtherwise()
    {
        Setlist setlist;
        setlist.songs.push_back(makeSong(u"By tempo"_s));
        setlist.songs.front().followChords = false;
        const auto read = fromJson(toJson(setlist));
        QVERIFY2(read.has_value(), read ? "" : qPrintable(read.error().message));
        QVERIFY(!read->songs.front().followChords);
        // Files from before chord follow: on.
        QJsonObject root = QJsonDocument::fromJson(toJson(setlist)).object();
        QJsonArray songs = root.value(u"songs"_s).toArray();
        QJsonObject song = songs.at(0).toObject();
        song.remove(u"followChords"_s);
        songs.replace(0, song);
        root.insert(u"songs"_s, songs);
        const auto older = fromJson(QJsonDocument(root).toJson());
        QVERIFY2(older.has_value(), older ? "" : qPrintable(older.error().message));
        QVERIFY(older->songs.front().followChords);
    }

    // The inversion chosen for each chord (the chord diagram) is kept with the
    // song; an older file has none; a bad one is refused with where it is.
    void aSongKeepsItsChosenInversions()
    {
        Setlist setlist;
        setlist.songs.push_back(makeSong(u"Slow"_s));
        setlist.songs.front().chordInversions = {{u"E/D#"_s, 1}, {u"C#m"_s, 2}};
        const auto read = fromJson(toJson(setlist));
        QVERIFY2(read.has_value(), read ? "" : qPrintable(read.error().message));
        QCOMPARE(read->songs.front().chordInversions, (std::map<QString, int>{{u"E/D#"_s, 1}, {u"C#m"_s, 2}}));
        QJsonObject root = QJsonDocument::fromJson(toJson(setlist)).object();
        QJsonArray songs = root.value(u"songs"_s).toArray();
        QJsonObject song = songs.at(0).toObject();
        song.remove(u"chordInversions"_s);
        songs.replace(0, song);
        root.insert(u"songs"_s, songs);
        const auto older = fromJson(QJsonDocument(root).toJson());
        QVERIFY2(older.has_value(), older ? "" : qPrintable(older.error().message));
        QVERIFY(older->songs.front().chordInversions.empty());
        song.insert(u"chordInversions"_s, QJsonObject{{u"C"_s, 9}});
        songs.replace(0, song);
        root.insert(u"songs"_s, songs);
        const auto bad = fromJson(QJsonDocument(root).toJson());
        QVERIFY(!bad.has_value());
        QVERIFY2(bad.error().message.contains(u"chordInversions"_s), qPrintable(bad.error().message));
    }
};

QTEST_GUILESS_MAIN(TestSetlistJson)
#include "tst_setlist_json.moc"
