#include "gigchain/engine/MidiSetup.h"
#include "gigchain/engine/MidiControl.h"

#include <QCoreApplication>

#include <QStringView>

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

namespace gigchain::engine {

QStringList portNames(const QStringList& numbered)
{
    QStringList plain; // without the place number, before telling the same ones apart
    QStringList names;
    for (qsizetype i = 0; i < numbered.size(); ++i) {
        QString name = numbered.at(i);
        const QString place = u' ' + QString::number(i);
        if (name.endsWith(place)) name.chop(place.size());
        const auto before = std::ranges::count(plain, name);
        plain << name;
        names << (before == 0 ? name : u"%1 (%2)"_s.arg(name).arg(before + 1));
    }
    return names;
}

namespace {

QString withoutPlace(const QString& name)
{
    const qsizetype space = name.lastIndexOf(u' ');
    if (space <= 0 || space == name.size() - 1) return name;
    const QStringView place = QStringView(name).sliced(space + 1);
    return std::ranges::all_of(place, [](QChar c) { return c.isDigit(); }) ? name.left(space) : name;
}

} // namespace

MidiSetup withoutPortPlaces(MidiSetup saved)
{
    for (QString& name : saved.enabled) name = withoutPlace(name);
    saved.enabled.removeDuplicates();
    std::map<QString, int> channels;
    for (const auto& [name, channel] : saved.channels) channels.try_emplace(withoutPlace(name), channel);
    saved.channels = std::move(channels);
    saved.clockOutput = withoutPlace(saved.clockOutput);
    return saved;
}

std::vector<MidiPort> resolveMidiInputs(const QStringList& present, const MidiSetup& setup)
{
    std::vector<MidiPort> ports;
    ports.reserve(static_cast<std::size_t>(present.size()));
    for (const QString& name : present) {
        const auto channel = setup.channels.find(name);
        ports.push_back(MidiPort{.name = name,
                                 .enabled = setup.configured && setup.enabled.contains(name),
                                 .channel = channel != setup.channels.end() ? std::clamp(channel->second, 0, 16) : 0});
    }
    const bool anyOn = std::ranges::any_of(ports, [](const MidiPort& p) { return p.enabled; });
    if (!anyOn && !ports.empty()) ports.front().enabled = true;
    return ports;
}

QString MidiTrigger::describe() const
{
    static constexpr std::array<const char*, 12> kNotes{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const QString channelText = QCoreApplication::translate("MidiTrigger", "channel %1").arg(channel + 1);
    switch (kind) {
    case ControlChange:
        return QCoreApplication::translate("MidiTrigger", "Pedal/CC %1 (%2)").arg(number).arg(channelText);
    case Note:
        return QCoreApplication::translate("MidiTrigger", "Note %1%2 (%3)")
            .arg(QString::fromLatin1(kNotes.at(number % 12)))
            .arg(number / 12 - 1)
            .arg(channelText);
    case ProgramChange:
        return QCoreApplication::translate("MidiTrigger", "Program %1 (%2)").arg(number + 1).arg(channelText);
    case None:
        break;
    }
    return QCoreApplication::translate("MidiTrigger", "Not set");
}

} // namespace gigchain::engine
