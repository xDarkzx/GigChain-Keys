#include "PianoSamples.h"

#include "AudioFile.h"
#include "EngineLog.h"
#include "Sfz.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <tuple>

using namespace Qt::StringLiterals;

namespace gigchain::engine {

namespace {

// The layers kept of one note's: `max` spread from the softest to the
// loudest, each covering the velocities of the ones left out below it.
std::vector<sfz::Region> fewerLayers(std::vector<sfz::Region> layers, int max)
{
    std::ranges::sort(layers, {}, &sfz::Region::loVel);
    const auto n = static_cast<int>(layers.size());
    if (n <= max || max < 1) return layers;
    std::vector<int> picks;
    picks.reserve(static_cast<std::size_t>(max));
    for (int j = 0; j < max; ++j) {
        picks.push_back(max == 1 ? n - 1 : static_cast<int>(std::lround(static_cast<double>(j) * (n - 1) / (max - 1))));
    }
    std::vector<sfz::Region> kept;
    for (std::size_t j = 0; j < picks.size(); ++j) {
        sfz::Region layer = layers.at(static_cast<std::size_t>(picks.at(j)));
        layer.loVel = j == 0 ? layers.front().loVel : layers.at(static_cast<std::size_t>(picks.at(j - 1)) + 1).loVel;
        kept.push_back(layer);
    }
    return kept;
}

std::vector<sfz::Region> fewerLayersPerNote(const std::vector<sfz::Region>& regions, int max)
{
    std::map<std::tuple<int, int, int, int>, std::vector<sfz::Region>> notes;
    for (const sfz::Region& r : regions) notes[{r.loKey, r.hiKey, r.seqLength, r.seqPosition}].push_back(r);
    std::vector<sfz::Region> kept;
    for (auto& [key, layers] : notes) std::ranges::move(fewerLayers(std::move(layers), max), std::back_inserter(kept));
    return kept;
}

int16_t toInt16(float v)
{
    return static_cast<int16_t>(std::lround(std::clamp(v, -1.0F, 1.0F) * 32767.0F));
}

} // namespace

const SampleZone* SampleSet::find(int note, int velocity, bool releaseZone, int roundRobin) const
{
    const std::vector<SampleZone>& zones = releaseZone ? release : attack;
    const auto fits = [&](const SampleZone& z) { return note >= z.loKey && note <= z.hiKey && velocity >= z.loVel && velocity <= z.hiVel; };
    const SampleZone* any = nullptr;
    for (const SampleZone& z : zones) {
        if (!fits(z)) continue;
        if (z.seqLength <= 1 || z.seqPosition == (std::max(roundRobin, 0) % z.seqLength) + 1) return &z;
        if (any == nullptr) any = &z;
    }
    return any;
}

std::size_t SampleSet::bytes() const
{
    const auto sum = [](const std::vector<SampleZone>& zones) {
        return std::accumulate(zones.begin(), zones.end(), std::size_t{0},
                               [](std::size_t total, const SampleZone& z) { return total + (z.frames.size() * sizeof(int16_t)); });
    };
    return sum(attack) + sum(release);
}

core::Result<SampleSet> loadSampleSet(const QString& sfzPath, double sampleRate, const PianoLoadOptions& options, const std::atomic<bool>* cancel)
{
    QElapsedTimer took;
    took.start();
    QFile file(sfzPath);
    if (!file.exists()) return core::fail(core::ErrorCode::FileNotFound, u"Piano not found: %1"_s.arg(sfzPath));
    if (!file.open(QIODevice::ReadOnly)) {
        return core::fail(core::ErrorCode::FileReadFailed, u"Cannot read the piano %1: %2"_s.arg(sfzPath, file.errorString()));
    }
    auto regions = sfz::parse(QString::fromUtf8(file.readAll()));
    if (!regions) return core::fail(core::ErrorCode::ParseFailed, u"The piano %1: %2"_s.arg(sfzPath, regions.error().message));
    if (regions->empty()) return core::fail(core::ErrorCode::InvalidData, u"The piano %1 maps no recordings"_s.arg(sfzPath));

    std::vector<sfz::Region> attacks;
    std::vector<sfz::Region> releases;
    for (const sfz::Region& r : *regions) (r.release ? releases : attacks).push_back(r);

    SampleSet set;
    set.name = QFileInfo(sfzPath).completeBaseName();
    set.sampleRate = sampleRate;
    set.velocityTrack = static_cast<float>(std::clamp(regions->front().ampVeltrack / 100.0, 0.0, 1.0));
    const QString folder = QFileInfo(sfzPath).absolutePath();
    const auto read = [&](const std::vector<sfz::Region>& from, std::vector<SampleZone>& into) -> core::Result<void> {
        for (const sfz::Region& r : fewerLayersPerNote(from, options.maxLayers)) {
            if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
                return core::fail(core::ErrorCode::InvalidData, u"Reading the piano %1 was cancelled"_s.arg(set.name));
            }
            const double place = std::clamp((r.keyCenter - 21) / 87.0, 0.0, 1.0);
            const double keepSeconds = options.longestSeconds + ((options.shortestSeconds - options.longestSeconds) * place);
            constexpr double kOffsetRoom = 0.25; // the map's start offsets are a few milliseconds
            auto clip = decodeSampleFile(folder + u'/' + r.sample, sampleRate, keepSeconds + kOffsetRoom, cancel);
            if (!clip) return tl::unexpected(clip.error());
            const auto skip = std::min(static_cast<std::size_t>(std::lround(static_cast<double>(r.offset) * sampleRate / clip->sourceRate)),
                                       clip->clip.left.size());
            const std::size_t available = clip->clip.left.size() - skip;
            const auto keep = std::min(available, static_cast<std::size_t>(keepSeconds * sampleRate));
            const bool trimmed = keep < available;
            const auto fade = trimmed ? std::min(keep, static_cast<std::size_t>(options.fadeSeconds * sampleRate)) : std::size_t{0};
            SampleZone zone;
            zone.loKey = r.loKey;
            zone.hiKey = r.hiKey;
            zone.keyCenter = r.keyCenter;
            zone.loVel = r.loVel;
            zone.hiVel = r.hiVel;
            zone.tuneCents = r.tuneCents;
            zone.gain = static_cast<float>(std::pow(10.0, (r.volumeDb + r.globalVolumeDb) / 20.0));
            zone.attack = r.attack;
            zone.decay = r.decay;
            zone.sustain = std::clamp(r.sustainPercent / 100.0, 0.0, 1.0);
            zone.release = r.releaseSeconds;
            zone.rtDecayDb = r.rtDecayDb;
            zone.seqLength = r.seqLength;
            zone.seqPosition = r.seqPosition;
            zone.frames.resize(keep * 2);
            auto left = std::next(clip->clip.left.cbegin(), static_cast<std::ptrdiff_t>(skip));
            auto right = std::next(clip->clip.right.cbegin(), static_cast<std::ptrdiff_t>(skip));
            auto out = zone.frames.begin();
            for (std::size_t i = 0; i < keep; ++i) {
                const float g = i + fade >= keep && fade > 0 ? static_cast<float>(keep - i) / static_cast<float>(fade) : 1.0F;
                *out++ = toInt16(*left++ * g);
                *out++ = toInt16(*right++ * g);
            }
            into.push_back(std::move(zone));
        }
        return {};
    };
    if (auto done = read(attacks, set.attack); !done) return tl::unexpected(done.error());
    if (auto done = read(releases, set.release); !done) return tl::unexpected(done.error());
    qCInfo(lcEngine).noquote() << u"Piano read: %1, %2 recordings, %3 MB, in %4 s"_s.arg(set.name)
                                      .arg(set.attack.size() + set.release.size())
                                      .arg(static_cast<double>(set.bytes()) / 1e6, 0, 'f', 1)
                                      .arg(static_cast<double>(took.elapsed()) / 1000.0, 0, 'f', 1);
    return set;
}

QString pianoSampleFolder()
{
    if (const QString set = qEnvironmentVariable("GIGCHAIN_PIANO_SAMPLES"); !set.isEmpty()) return set;
    // A Mac app keeps them in its Resources (no such folder elsewhere); otherwise beside the app.
    QString resources = QCoreApplication::applicationDirPath() + u"/../Resources/samples/piano"_s;
    if (QFileInfo::exists(resources)) return resources;
    return QCoreApplication::applicationDirPath() + u"/samples/piano"_s;
}

} // namespace gigchain::engine
