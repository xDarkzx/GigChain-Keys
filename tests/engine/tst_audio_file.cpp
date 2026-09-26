#include "AudioFile.h"

#include <QDataStream>
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

// A 16-bit PCM WAV of a sine: `channels` 1 or 2, the right side (stereo)
// at half the left's level so the sides can be told apart.
void writeWav(const QString& path, int rate, int channels, double seconds, double frequency, double level)
{
    const auto frames = static_cast<quint32>(seconds * rate);
    const quint32 dataBytes = frames * static_cast<quint32>(channels) * 2;
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32{36 + dataBytes};
    out.writeRawData("WAVEfmt ", 8);
    out << quint32{16} << quint16{1} << static_cast<quint16>(channels) << static_cast<quint32>(rate)
        << static_cast<quint32>(rate * channels * 2) << static_cast<quint16>(channels * 2) << quint16{16};
    out.writeRawData("data", 4);
    out << dataBytes;
    for (quint32 i = 0; i < frames; ++i) {
        const double v = level * std::sin(2.0 * std::numbers::pi * frequency * i / rate);
        out << static_cast<qint16>(std::lround(v * 32767.0));
        if (channels == 2) out << static_cast<qint16>(std::lround(v * 0.5 * 32767.0));
    }
}

float peak(const std::vector<float>& samples)
{
    return std::accumulate(samples.begin(), samples.end(), 0.0F, [](float p, float s) { return std::max(p, std::abs(s)); });
}

} // namespace

class TestAudioFile : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

private slots:
    void aWavIsReadAndResampledToThePlayingRate()
    {
        const QString path = m_dir.filePath(u"track.wav"_s);
        writeWav(path, 44100, 2, 1.0, 440.0, 0.5);
        const auto clip = decodeAudioFile(path, 48000.0);
        QVERIFY2(clip.has_value(), clip ? "" : qPrintable(clip.error().message));
        // One second at 44.1 kHz is one second at 48 kHz: 48000 frames.
        QVERIFY2(std::abs(clip->frames() - 48000) < 480, qPrintable(QString::number(clip->frames())));
        QVERIFY(std::abs(clip->seconds() - 1.0) < 0.01);
        QVERIFY2(std::abs(peak(clip->left) - 0.5F) < 0.03F, qPrintable(QString::number(peak(clip->left))));
        QVERIFY2(std::abs(peak(clip->right) - 0.25F) < 0.03F, qPrintable(QString::number(peak(clip->right))));
    }

    void aMonoFilePlaysOnBothSides()
    {
        const QString path = m_dir.filePath(u"mono.wav"_s);
        writeWav(path, 48000, 1, 0.5, 220.0, 0.4);
        const auto clip = decodeAudioFile(path, 48000.0);
        QVERIFY2(clip.has_value(), clip ? "" : qPrintable(clip.error().message));
        QVERIFY(std::abs(peak(clip->left) - 0.4F) < 0.03F);
        QVERIFY(std::abs(peak(clip->right) - 0.4F) < 0.03F);
    }

    void aMissingFileIsAnError()
    {
        const auto clip = decodeAudioFile(m_dir.filePath(u"nothing.wav"_s), 48000.0);
        QVERIFY(!clip);
        QVERIFY(clip.error().code == core::ErrorCode::FileNotFound);
    }

    void aFileThatIsNotAudioIsAnError()
    {
        const QString path = m_dir.filePath(u"notes.mp3"_s);
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("these are the lyrics, not audio");
        }
        const auto clip = decodeAudioFile(path, 48000.0);
        QVERIFY(!clip);
        QVERIFY2(clip.error().message.contains(u"notes.mp3"_s), qPrintable(clip.error().message));
    }

    void withoutARunningOutputItIsAnError()
    {
        const QString path = m_dir.filePath(u"any.wav"_s);
        writeWav(path, 48000, 2, 0.1, 440.0, 0.5);
        QVERIFY(!decodeAudioFile(path, 0.0));
    }

    void cancellingStopsTheRead()
    {
        const QString path = m_dir.filePath(u"long.wav"_s);
        writeWav(path, 48000, 2, 5.0, 440.0, 0.5);
        const std::atomic<bool> cancel{true};
        const auto clip = decodeAudioFile(path, 48000.0, &cancel);
        QVERIFY(!clip);
        QVERIFY2(clip.error().message.contains(u"cancelled"_s), qPrintable(clip.error().message));
    }
};

QTEST_GUILESS_MAIN(TestAudioFile)
#include "tst_audio_file.moc"
