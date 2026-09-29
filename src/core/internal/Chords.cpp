#include "gigchain/core/Chords.h"

#include <QStringView>

#include <utility>

using namespace Qt::StringLiterals;

namespace gigchain::core {
namespace {

constexpr uint16_t bit(int interval)
{
    return static_cast<uint16_t>(1U << (((interval % 12) + 12) % 12));
}

int letterClass(QChar letter)
{
    switch (letter.unicode()) {
    case u'C': return 0;
    case u'D': return 2;
    case u'E': return 4;
    case u'F': return 5;
    case u'G': return 7;
    case u'A': return 9;
    case u'B': return 11;
    default: return -1;
    }
}

// A note name at the start of `text` ("F#", "Bb", "E"): its pitch class and
// how many characters it takes; {-1, 0} when there is none.
std::pair<int, qsizetype> readNote(QStringView text)
{
    if (text.isEmpty()) return {-1, 0};
    const int letter = letterClass(text.front());
    if (letter < 0) return {-1, 0};
    if (text.size() > 1) {
        const QChar accidental = text.at(1);
        if (accidental == u'#' || accidental == u'♯') return {(letter + 1) % 12, 2};
        if (accidental == u'b' || accidental == u'♭') return {(letter + 11) % 12, 2};
    }
    return {letter, 1};
}

} // namespace

std::optional<ChordShape> parseChordName(const QString& name)
{
    const QString text = name.trimmed();
    const auto [root, rootLength] = readNote(text);
    if (root < 0) return std::nullopt;
    QString quality = text.mid(rootLength);

    ChordShape shape;
    shape.root = root;
    // "/E" is a bass note; "6/9" is not one.
    if (const qsizetype slash = quality.lastIndexOf(u'/'); slash >= 0) {
        const auto [bass, length] = readNote(QStringView(quality).sliced(slash + 1));
        if (bass >= 0 && slash + 1 + length == quality.size()) {
            shape.bass = bass;
            quality.truncate(slash);
        }
    }
    quality.remove(u'(').remove(u')').remove(u' ');

    const auto take = [&quality](QStringView token, Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive) {
        if (!quality.startsWith(token, sensitivity)) return false;
        quality.remove(0, token.size());
        return true;
    };
    int third = 4;
    int fifth = 7;
    int seventh = -1;
    bool majorSeventh = false; // "maj7", "M9": the seventh a numbered chord adds is major
    bool power = false;
    int sus = -1;
    uint16_t added = 0;

    // The chord's kind first.
    if (take(u"maj") || take(u"M", Qt::CaseSensitive)) {
        majorSeventh = true;
    } else if (take(u"Δ")) { // Δ: a major seventh, even written alone
        majorSeventh = true;
        seventh = 11;
    } else if (take(u"min") || take(u"m", Qt::CaseSensitive) || take(u"-")) {
        third = 3;
        if (take(u"maj") || take(u"M", Qt::CaseSensitive)) majorSeventh = true; // mMaj7
    } else if (take(u"dim") || take(u"°") || take(u"o", Qt::CaseSensitive)) {
        third = 3;
        fifth = 6;
        if (take(u"7")) seventh = 9; // a diminished seventh
    } else if (take(u"aug") || take(u"+")) {
        fifth = 8;
    } else if (take(u"ø")) { // ø: half-diminished
        third = 3;
        fifth = 6;
        seventh = 10;
        (void)take(u"7");
    }
    const auto addSeventh = [&seventh, majorSeventh] {
        if (seventh < 0) seventh = majorSeventh ? 11 : 10;
    };
    // Then numbers, sus, add and alterations, in any order; anything else is skipped.
    while (!quality.isEmpty()) {
        if (take(u"sus2")) {
            sus = 2;
        } else if (take(u"sus4") || take(u"sus")) {
            sus = 5;
        } else if (take(u"add9") || take(u"add2")) {
            added |= bit(2);
        } else if (take(u"add11") || take(u"add4")) {
            added |= bit(5);
        } else if (take(u"no3")) {
            third = -1;
        } else if (take(u"13")) {
            addSeventh();
            added |= bit(2) | bit(9);
        } else if (take(u"11")) {
            addSeventh();
            added |= bit(2) | bit(5);
        } else if (take(u"9")) {
            addSeventh();
            added |= bit(2);
        } else if (take(u"7")) {
            addSeventh();
        } else if (take(u"6")) {
            added |= bit(9);
        } else if (take(u"b5")) {
            fifth = 6;
        } else if (take(u"#5")) {
            fifth = 8;
        } else if (take(u"5")) {
            power = true;
        } else if (take(u"b9")) {
            added |= bit(1);
        } else if (take(u"#9")) {
            added |= bit(3);
        } else if (take(u"#11")) {
            added |= bit(6);
        } else if (take(u"b13")) {
            added |= bit(8);
        } else {
            quality.remove(0, 1);
        }
    }
    if (power || sus >= 0) third = -1;

    uint16_t intervals = bit(0) | bit(fifth) | added;
    if (third >= 0) intervals |= bit(third);
    if (seventh >= 0) intervals |= bit(seventh);
    if (sus >= 0) intervals |= bit(sus);
    for (int interval = 0; interval < 12; ++interval) {
        if ((intervals & bit(interval)) != 0) shape.family |= bit(root + interval);
    }
    shape.third = third;
    shape.colour = third >= 0 ? -1 : sus >= 0 ? sus : 7;
    return shape;
}

} // namespace gigchain::core
