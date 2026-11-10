#pragma once

// A SoundFont 2 file made for the tests (SF2.01): one tone, 480 Hz with a
// 12 kHz overtone, looped, as the preset 000:000 "Tone" and the kit
// 128:000 "Kit". Its attack is `attack` seconds and its release 0.05 s; its
// reverb and chorus sends 50%; `extra` adds generators (number, amount).

#include <QByteArray>
#include <QFile>
#include <QString>

#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

namespace gigchain::testing {

inline constexpr int kSoundFontRoot = 69;      // the tone's key: played there, it is not transposed
inline constexpr int kSoundFontFrames = 4800;  // 0.1 s at 48 kHz: 48 cycles of 480 Hz, a seamless loop

namespace sf2file {
inline QByteArray le16(int v)
{
    QByteArray b(2, '\0');
    b[0] = static_cast<char>(v & 0xFF);
    b[1] = static_cast<char>((v >> 8) & 0xFF);
    return b;
}
inline QByteArray le32(qint64 v)
{
    QByteArray b(4, '\0');
    for (int i = 0; i < 4; ++i) b[i] = static_cast<char>((v >> (8 * i)) & 0xFF);
    return b;
}
inline QByteArray name20(const char* text)
{
    QByteArray b(text);
    b.resize(20, '\0');
    return b;
}
inline QByteArray chunk(const char* id, const QByteArray& data)
{
    QByteArray b = QByteArray(id, 4) + le32(data.size()) + data;
    if (data.size() % 2 != 0) b.append('\0');
    return b;
}
inline QByteArray list(const char* type, const QByteArray& chunks)
{
    return chunk("LIST", QByteArray(type, 4) + chunks);
}
} // namespace sf2file

// Seconds in the SoundFont's time unit (timecents).
inline int timecents(double seconds)
{
    return static_cast<int>(std::lround(1200.0 * std::log2(seconds)));
}

// Writes it to `folder`/`fileName`.sf2; its path, or empty when it could not be written.
inline QString makeSoundFont(const QString& folder, const QString& fileName = QStringLiteral("Test Tone"), double attack = 0.3,
                             const std::vector<std::pair<int, int>>& extra = {})
{
    using namespace sf2file;
    QByteArray samples;
    for (int n = 0; n < kSoundFontFrames + 46; ++n) { // (46 zero points after each sample, as SF2.01 asks)
        double x = 0.0;
        if (n < kSoundFontFrames) {
            const double t = n / 48000.0;
            x = 0.5 * std::sin(2.0 * std::numbers::pi * 480.0 * t) + 0.25 * std::sin(2.0 * std::numbers::pi * 12000.0 * t);
        }
        samples += le16(static_cast<int>(std::lround(x * 32767.0)));
    }
    const QByteArray info = chunk("ifil", le16(2) + le16(1)) + chunk("isng", QByteArray("EMU8000\0", 8))
                          + chunk("INAM", QByteArray("Test\0\0", 6));
    const QByteArray sdta = chunk("smpl", samples);

    // Presets: 000:000 Tone, 128:000 Kit (both the one instrument).
    QByteArray phdr;
    phdr += name20("Tone") + le16(0) + le16(0) + le16(0) + le32(0) + le32(0) + le32(0);
    phdr += name20("Kit") + le16(0) + le16(128) + le16(1) + le32(0) + le32(0) + le32(0);
    phdr += name20("EOP") + le16(0) + le16(0) + le16(2) + le32(0) + le32(0) + le32(0);
    const QByteArray pbag = le16(0) + le16(0) + le16(1) + le16(0) + le16(2) + le16(0);
    const QByteArray pmod(10, '\0');
    const QByteArray pgen = le16(41) + le16(0) + le16(41) + le16(0) + le16(0) + le16(0);
    const QByteArray inst = name20("Tone") + le16(0) + name20("EOI") + le16(1);
    // Its own values, which every control at its minimum keeps.
    QByteArray igen;
    igen += le16(16) + le16(500);               // reverb send 50%
    igen += le16(15) + le16(500);               // chorus send 50%
    igen += le16(34) + le16(timecents(attack)); // attack
    igen += le16(38) + le16(timecents(0.05));   // release 0.05 s
    for (const auto& [gen, amount] : extra) igen += le16(gen) + le16(amount);
    igen += le16(54) + le16(1);                 // looped
    igen += le16(53) + le16(0);                 // the sample (last)
    const int gens = 6 + static_cast<int>(extra.size());
    igen += le16(0) + le16(0);
    const QByteArray ibag = le16(0) + le16(0) + le16(gens) + le16(0);
    const QByteArray imod(10, '\0');
    QByteArray shdr;
    shdr += name20("Tone") + le32(0) + le32(kSoundFontFrames) + le32(0) + le32(kSoundFontFrames) + le32(48000);
    shdr += QByteArray(1, static_cast<char>(kSoundFontRoot)) + QByteArray(1, '\0') + le16(0) + le16(1);
    shdr += name20("EOS") + QByteArray(20, '\0') + QByteArray(2, '\0') + le16(0) + le16(0);
    const QByteArray pdta = chunk("phdr", phdr) + chunk("pbag", pbag) + chunk("pmod", pmod) + chunk("pgen", pgen)
                          + chunk("inst", inst) + chunk("ibag", ibag) + chunk("imod", imod) + chunk("igen", igen)
                          + chunk("shdr", shdr);
    const QByteArray body = QByteArray("sfbk", 4) + list("INFO", info) + list("sdta", sdta) + list("pdta", pdta);
    const QString path = folder + u'/' + fileName + QStringLiteral(".sf2");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(QByteArray("RIFF", 4) + le32(body.size()) + body);
    return path;
}

} // namespace gigchain::testing
