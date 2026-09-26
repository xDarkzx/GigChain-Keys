#include "PluginIcons.h"

#include <QStringList>

#include <algorithm>
#include <array>
#include <utility>

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

using Rule = std::pair<QStringList, QString>; // any of these words -> icon

// Checked in order against the lower-cased sub-categories and name.
const std::array<Rule, 9>& instrumentRules()
{
    static const std::array<Rule, 9> rules = {{
        {{u"piano"_s, u"grand"_s}, u"piano"_s},
        {{u"organ"_s, u"b-3"_s, u"farfisa"_s, u"continental"_s, u"rhodes"_s, u"stage-73"_s, u"wurli"_s, u"clav"_s,
          u"electric piano"_s},
         u"keyboard"_s},
        {{u"drum"_s, u"percussion"_s, u"beat"_s, u"kit"_s}, u"vinyl"_s},
        {{u"guitar"_s, u"bass guitar"_s}, u"guitar-pick"_s},
        {{u"vocal"_s, u"voice"_s, u"choir"_s, u"vocoder"_s}, u"microphone"_s},
        {{u"bell"_s, u"mallet"_s, u"glock"_s}, u"bell"_s},
        {{u"orchestra"_s, u"string"_s, u"solina"_s, u"brass"_s, u"symphony"_s}, u"music"_s},
        {{u"lead"_s}, u"wave-saw-tool"_s},
        {{u"synth"_s, u"pad"_s, u"sampler"_s}, u"wave-sine"_s},
    }};
    return rules;
}

const std::array<Rule, 2>& effectRules()
{
    static const std::array<Rule, 2> rules = {{
        {{u"reverb"_s, u"pro-r"_s, u"room"_s, u"delay"_s, u"echo"_s}, u"ripple"_s},
        {{u"meter"_s, u"analyzer"_s}, u"volume"_s},
    }};
    return rules;
}

template <std::size_t N>
QString match(const std::array<Rule, N>& rules, const QString& text)
{
    for (const auto& [words, icon] : rules) {
        if (std::ranges::any_of(words, [&text](const QString& word) { return text.contains(word); })) return icon;
    }
    return {};
}

} // namespace

QString iconFor(const engine::PluginInfo& plugin)
{
    // For instruments a specific word in the name wins: vendors often tag
    // everything "Synth" (Arturia does), which says little. Sub-categories
    // then cover the rest; effects are the other way round.
    const QString categories = plugin.subCategories.toLower();
    const QString name = plugin.name.toLower();
    if (plugin.kind == engine::PluginKind::Instrument) {
        if (QString icon = match(instrumentRules(), name); !icon.isEmpty()) return icon;
        if (QString icon = match(instrumentRules(), categories); !icon.isEmpty()) return icon;
        return u"music"_s;
    }
    if (QString icon = match(effectRules(), categories); !icon.isEmpty()) return icon;
    if (QString icon = match(effectRules(), name); !icon.isEmpty()) return icon;
    return u"adjustments-horizontal"_s;
}

QString iconUrl(const QString& icon)
{
    return u"qrc:/qt/qml/GigChain/Ui/icons/%1.svg"_s.arg(icon);
}

} // namespace gigchain::ui
