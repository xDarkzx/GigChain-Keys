#include "Sfz.h"

#include <QStringList>

#include <cmath>
#include <functional>
#include <map>
#include <optional>

using namespace Qt::StringLiterals;

namespace gigchain::engine::sfz {

namespace {

std::optional<double> number(const QString& text)
{
    bool ok = false;
    const double v = text.toDouble(&ok);
    return ok && std::isfinite(v) ? std::optional(v) : std::nullopt;
}

std::optional<int64_t> whole(const QString& text)
{
    bool ok = false;
    const qlonglong v = text.toLongLong(&ok);
    return ok ? std::optional(static_cast<int64_t>(v)) : std::nullopt;
}

using Setter = std::function<bool(Region&, const QString&)>;

Setter toInt(int Region::* field)
{
    return [field](Region& r, const QString& v) {
        const auto n = whole(v);
        if (!n || *n < -100000 || *n > 100000) return false;
        r.*field = static_cast<int>(*n);
        return true;
    };
}

Setter toDouble(double Region::* field)
{
    return [field](Region& r, const QString& v) {
        const auto n = number(v);
        if (!n) return false;
        r.*field = *n;
        return true;
    };
}

const std::map<QString, Setter>& setters()
{
    static const std::map<QString, Setter> all{
        {u"sample"_s, [](Region& r, const QString& v) { r.sample = QString(v).replace(u'\\', u'/'); return !v.isEmpty(); }},
        {u"lokey"_s, toInt(&Region::loKey)},
        {u"hikey"_s, toInt(&Region::hiKey)},
        {u"pitch_keycenter"_s, toInt(&Region::keyCenter)},
        {u"lovel"_s, toInt(&Region::loVel)},
        {u"hivel"_s, toInt(&Region::hiVel)},
        {u"tune"_s, toDouble(&Region::tuneCents)},
        {u"volume"_s, toDouble(&Region::volumeDb)},
        {u"global_volume"_s, toDouble(&Region::globalVolumeDb)},
        {u"amp_veltrack"_s, toDouble(&Region::ampVeltrack)},
        {u"offset"_s,
         [](Region& r, const QString& v) {
             const auto n = whole(v);
             if (!n || *n < 0) return false;
             r.offset = *n;
             return true;
         }},
        {u"trigger"_s,
         [](Region& r, const QString& v) {
             if (v != u"attack"_s && v != u"release"_s) return false;
             r.release = v == u"release"_s;
             return true;
         }},
        {u"ampeg_attack"_s, toDouble(&Region::attack)},
        {u"ampeg_decay"_s, toDouble(&Region::decay)},
        {u"ampeg_sustain"_s, toDouble(&Region::sustainPercent)},
        {u"ampeg_release"_s, toDouble(&Region::releaseSeconds)},
        {u"rt_decay"_s, toDouble(&Region::rtDecayDb)},
        {u"seq_length"_s, toInt(&Region::seqLength)},
        {u"seq_position"_s, toInt(&Region::seqPosition)},
    };
    return all;
}

} // namespace

core::Result<std::vector<Region>> parse(const QString& text)
{
    enum class Scope : uint8_t { Global, Group, Region };
    sfz::Region global;
    sfz::Region group;
    sfz::Region region;
    Scope scope = Scope::Global;
    int regionLine = 0;
    std::vector<sfz::Region> regions;
    const auto finishRegion = [&]() -> core::Result<void> {
        if (scope != Scope::Region) return {};
        if (region.sample.isEmpty()) {
            return core::fail(core::ErrorCode::ParseFailed, u"The region at line %1 has no sample"_s.arg(regionLine));
        }
        regions.push_back(region);
        return {};
    };

    const QStringList lines = text.split(u'\n');
    for (qsizetype n = 0; n < lines.size(); ++n) {
        const auto lineNumber = static_cast<int>(n + 1);
        QString line = lines.at(n);
        if (const qsizetype comment = line.indexOf(u"//"_s); comment >= 0) line.truncate(comment);
        line = line.trimmed();
        qsizetype at = 0;
        while (at < line.size()) {
            if (line.at(at).isSpace()) {
                ++at;
                continue;
            }
            if (line.at(at) == u'<') {
                const qsizetype end = line.indexOf(u'>', at);
                if (end < 0) return core::fail(core::ErrorCode::ParseFailed, u"An unclosed header at line %1"_s.arg(lineNumber));
                const QString header = line.mid(at + 1, end - at - 1);
                if (auto done = finishRegion(); !done) return tl::unexpected(done.error());
                if (header == u"global"_s) {
                    scope = Scope::Global;
                } else if (header == u"group"_s) {
                    group = global;
                    scope = Scope::Group;
                } else if (header == u"region"_s) {
                    region = group;
                    scope = Scope::Region;
                    regionLine = lineNumber;
                } else {
                    return core::fail(core::ErrorCode::ParseFailed, u"Unknown header <%1> at line %2"_s.arg(header).arg(lineNumber));
                }
                at = end + 1;
                continue;
            }
            const qsizetype equals = line.indexOf(u'=', at);
            if (equals < 0) return core::fail(core::ErrorCode::ParseFailed, u"Not a setting at line %1"_s.arg(lineNumber));
            const QString key = line.mid(at, equals - at).trimmed();
            QString value;
            if (key == u"sample"_s) { // a file name may have spaces: the rest of the line
                value = line.mid(equals + 1).trimmed();
                at = line.size();
            } else {
                qsizetype end = equals + 1;
                while (end < line.size() && !line.at(end).isSpace()) ++end;
                value = line.mid(equals + 1, end - equals - 1);
                at = end;
            }
            const auto setter = setters().find(key);
            if (setter == setters().end()) {
                return core::fail(core::ErrorCode::ParseFailed, u"Unknown setting \"%1\" at line %2"_s.arg(key).arg(lineNumber));
            }
            sfz::Region& target = scope == Scope::Global ? global : (scope == Scope::Group ? group : region);
            if (!setter->second(target, value)) {
                return core::fail(core::ErrorCode::ParseFailed, u"\"%1=%2\" at line %3 is not a value it can take"_s.arg(key, value).arg(lineNumber));
            }
            // A global setting after a group has started still reaches it.
            if (scope == Scope::Global) group = global;
        }
    }
    if (auto done = finishRegion(); !done) return tl::unexpected(done.error());
    return regions;
}

} // namespace gigchain::engine::sfz
