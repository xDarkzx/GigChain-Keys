// Piano Pro's recorded pianos: their SFZ maps (which recording plays for
// which key and how hard) are read as written, and the recordings are
// loaded at the playing rate, kept to a few loudness layers, trimmed to a
// sensible length with a fade, and stored small.
#include "PianoSamples.h"
#include "Sfz.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

using namespace gigchain;
using namespace gigchain::engine;
using namespace Qt::StringLiterals;

namespace {

// A 16-bit PCM stereo WAV: a sine at `frequency`, `level` loud.
void writeWav(const QString& path, int rate, double seconds, double frequency, double level)
{
    const auto frames = static_cast<quint32>(seconds * rate);
    const quint32 dataBytes = frames * 4;
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32{36 + dataBytes};
    out.writeRawData("WAVEfmt ", 8);
    out << quint32{16} << quint16{1} << quint16{2} << static_cast<quint32>(rate) << static_cast<quint32>(rate * 4) << quint16{4}
        << quint16{16};
    out.writeRawData("data", 4);
    out << dataBytes;
    for (quint32 i = 0; i < frames; ++i) {
        const auto v = static_cast<qint16>(std::lround(level * std::sin(2.0 * std::numbers::pi * frequency * i / rate) * 32767.0));
        out << v << v;
    }
}

void writeText(const QString& path, const QString& text)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(text.toUtf8());
}

// A small piano as VCSL's are laid out: two notes, each with five loudness
// layers, and a release sample per note.
QString writeSmallPiano(const QTemporaryDir& dir)
{
    QDir(dir.path()).mkpath(u"Small Piano/Sustains"_s);
    QDir(dir.path()).mkpath(u"Small Piano/Releases"_s);
    QString sfz = u"<global>\nglobal_volume=2\namp_veltrack=88\n\n<group>\ntrigger=attack\nampeg_attack=0.004\nampeg_release=0.4\n"_s;
    const std::array<int, 6> edges{0, 30, 55, 80, 105, 128};
    for (const int key : {60, 62}) {
        for (int layer = 0; layer < 5; ++layer) {
            const QString name = u"Small Piano/Sustains/note%1_v%2.wav"_s.arg(key).arg(layer + 1);
            writeWav(dir.filePath(name), 44100, 4.0, 440.0 * std::exp2((key - 69) / 12.0), 0.1 + 0.15 * layer);
            sfz += u"\n<region>\nlovel=%1\nhivel=%2\nsample=%3\npitch_keycenter=%4\nlokey=%4\nhikey=%5\ntune=-12\nvolume=3\n"_s
                       .arg(edges.at(static_cast<std::size_t>(layer)))
                       .arg(edges.at(static_cast<std::size_t>(layer) + 1) - 1)
                       .arg(name)
                       .arg(key)
                       .arg(key + 1);
        }
    }
    sfz += u"\n<group>\ntrigger=release\nrt_decay=4\n"_s;
    for (const int key : {60, 62}) {
        const QString name = u"Small Piano/Releases/rel%1.wav"_s.arg(key);
        writeWav(dir.filePath(name), 44100, 0.5, 1000.0, 0.05);
        sfz += u"\n<region>\nsample=%1\npitch_keycenter=%2\nlokey=%2\nhikey=%3\n"_s.arg(name).arg(key).arg(key + 1);
    }
    writeText(dir.filePath(u"Small Piano.sfz"_s), sfz);
    return dir.filePath(u"Small Piano.sfz"_s);
}

} // namespace

class TestPianoSamples : public QObject
{
    Q_OBJECT

private slots:
    void anSfzMapIsReadWithItsGroupsSettingsInherited()
    {
        const auto parsed = sfz::parse(u"// a comment\n<global>\nglobal_volume=2\namp_veltrack=88\n"
                                       u"<group> trigger=attack ampeg_attack=0.004 ampeg_release=0.4\n"
                                       u"<region>\nlovel=0 hivel=54\nsample=Grand Piano, K/Sustains/GPiano_sus_A#-1_v2.flac\n"
                                       u"pitch_keycenter=22\nlokey=21\nhikey=23\ntune=81\nvolume=21.8\noffset=122 // trailing note\n"
                                       u"<group>\ntrigger=release\nrt_decay=4\n<region>\nsample=rel.flac\nlokey=21 hikey=23 pitch_keycenter=22\n"
                                       u"seq_length=2 seq_position=2\n"_s);
        QVERIFY2(parsed.has_value(), parsed ? "" : qPrintable(parsed.error().message));
        QCOMPARE(parsed->size(), std::size_t{2});
        const sfz::Region& a = parsed->front();
        QCOMPARE(a.sample, u"Grand Piano, K/Sustains/GPiano_sus_A#-1_v2.flac"_s);
        QCOMPARE(a.loKey, 21);
        QCOMPARE(a.hiKey, 23);
        QCOMPARE(a.keyCenter, 22);
        QCOMPARE(a.loVel, 0);
        QCOMPARE(a.hiVel, 54);
        QCOMPARE(a.tuneCents, 81.0);
        QCOMPARE(a.volumeDb, 21.8);
        QCOMPARE(a.globalVolumeDb, 2.0);
        QCOMPARE(a.ampVeltrack, 88.0);
        QCOMPARE(a.offset, int64_t{122});
        QCOMPARE(a.attack, 0.004);
        QCOMPARE(a.releaseSeconds, 0.4);
        QVERIFY(!a.release);
        const sfz::Region& r = parsed->back();
        QVERIFY(r.release);
        QCOMPARE(r.rtDecayDb, 4.0);
        QCOMPARE(r.attack, 0.0); // the release group's own settings, not the attack group's
        QCOMPARE(r.ampVeltrack, 88.0); // the global's
        QCOMPARE(r.seqLength, 2);
        QCOMPARE(r.seqPosition, 2);
    }

    void anSfzMapWithSomethingUnknownOrBrokenIsRefusedWithTheLine()
    {
        const auto unknown = sfz::parse(u"<region>\nsample=a.wav\nlokey=10\nloop_mode=one_shot\n"_s);
        QVERIFY(!unknown.has_value());
        QVERIFY2(unknown.error().message.contains(u"loop_mode"_s) && unknown.error().message.contains(u"line 4"_s),
                 qPrintable(unknown.error().message));
        const auto broken = sfz::parse(u"<region>\nsample=a.wav\nlokey=ten\n"_s);
        QVERIFY(!broken.has_value());
        QVERIFY2(broken.error().message.contains(u"line 3"_s), qPrintable(broken.error().message));
        const auto noSample = sfz::parse(u"<region>\nlokey=10\n"_s);
        QVERIFY(!noSample.has_value());
    }

    void aPianoLoadsAtThePlayingRateWithThreeLayersAndTrimmedTails()
    {
        QTemporaryDir dir;
        const QString sfzPath = writeSmallPiano(dir);
        PianoLoadOptions options;
        options.maxLayers = 3;
        options.longestSeconds = 2.0; // the lowest key's
        options.shortestSeconds = 2.0;
        options.fadeSeconds = 0.25;
        const auto loaded = loadSampleSet(sfzPath, 48000.0, options);
        QVERIFY2(loaded.has_value(), loaded ? "" : qPrintable(loaded.error().message));
        const SampleSet& set = *loaded;
        QCOMPARE(set.sampleRate, 48000.0);
        QCOMPARE(set.attack.size(), std::size_t{6}); // 2 notes x 3 layers
        QCOMPARE(set.release.size(), std::size_t{2});
        // Every velocity finds a layer, and the layers cover 0-127 with no gap.
        for (int velocity = 1; velocity <= 127; ++velocity) {
            const SampleZone* zone = set.find(60, velocity, false, 0);
            QVERIFY2(zone != nullptr, qPrintable(QString::number(velocity)));
            QVERIFY(zone->loVel <= velocity && velocity <= zone->hiVel);
        }
        QVERIFY(set.find(60, 1, false, 0) != set.find(60, 127, false, 0)); // soft and hard differ
        QCOMPARE(set.find(61, 100, false, 0)->keyCenter, 60);
        QVERIFY(set.find(64, 100, false, 0) == nullptr); // nothing mapped there
        QCOMPARE(set.find(60, 100, true, 0)->keyCenter, 60);
        const SampleZone& soft = *set.find(60, 1, false, 0);
        // Trimmed: 2 s at 48 kHz (from 4 s), fading to silence at the end.
        QVERIFY2(std::abs(static_cast<double>(soft.length()) - 96000.0) < 10.0, qPrintable(QString::number(soft.length())));
        const auto tail = std::span(soft.frames).last(200);
        QVERIFY(std::ranges::all_of(tail, [](int16_t s) { return std::abs(s) < 300; }));
        // Small: two bytes a sample.
        const auto samples = [](const std::vector<SampleZone>& zones) {
            return std::accumulate(zones.begin(), zones.end(), std::size_t{0},
                                   [](std::size_t total, const SampleZone& z) { return total + z.frames.size(); });
        };
        QCOMPARE(set.bytes(), (samples(set.attack) + samples(set.release)) * sizeof(int16_t));
        // The map's settings come through.
        QCOMPARE(soft.tuneCents, -12.0);
        QVERIFY(std::abs(soft.gain - static_cast<float>(std::pow(10.0, (3.0 + 2.0) / 20.0))) < 1e-4F);
        QCOMPARE(set.velocityTrack, 0.88F);
    }

    void aMissingRecordingIsAnErrorNamingIt()
    {
        QTemporaryDir dir;
        const QString sfzPath = writeSmallPiano(dir);
        QVERIFY(QFile::remove(dir.filePath(u"Small Piano/Sustains/note62_v3.wav"_s)));
        const auto loaded = loadSampleSet(sfzPath, 48000.0);
        QVERIFY(!loaded.has_value());
        QVERIFY2(loaded.error().message.contains(u"note62_v3.wav"_s), qPrintable(loaded.error().message));
        const auto noMap = loadSampleSet(dir.filePath(u"No Such Piano.sfz"_s), 48000.0);
        QVERIFY(!noMap.has_value());
        QVERIFY2(noMap.error().message.contains(u"No Such Piano.sfz"_s), qPrintable(noMap.error().message));
    }

    void stoppingTheLoadStopsIt()
    {
        QTemporaryDir dir;
        const QString sfzPath = writeSmallPiano(dir);
        const std::atomic<bool> cancel{true};
        QVERIFY(!loadSampleSet(sfzPath, 48000.0, {}, &cancel).has_value());
    }

    // The real pianos (fetched by tools/fetch-samples), when they are here.
    void theBundledPianosLoad()
    {
        const QString folder = pianoSampleFolder();
        if (!QFileInfo::exists(folder + u"/Grand Piano, K.sfz"_s)) QSKIP("The piano samples are not fetched here");
        for (const QString& name : {u"Grand Piano, K.sfz"_s, u"Upright Piano, Y.sfz"_s}) {
            const auto loaded = loadSampleSet(folder + u'/' + name, 48000.0);
            QVERIFY2(loaded.has_value(), loaded ? "" : qPrintable(loaded.error().message));
            qInfo().noquote() << u"%1: %2 zones, %3 MB"_s.arg(name).arg(loaded->attack.size() + loaded->release.size())
                                     .arg(static_cast<double>(loaded->bytes()) / 1e6, 0, 'f', 1);
            QVERIFY(loaded->bytes() < 160'000'000); // keeps to a sensible size in memory
            for (int note = 21; note <= 108; ++note) QVERIFY2(loaded->find(note, 100, false, 0) != nullptr, qPrintable(QString::number(note)));
        }
    }
};

QTEST_GUILESS_MAIN(TestPianoSamples)
#include "tst_piano_samples.moc"
