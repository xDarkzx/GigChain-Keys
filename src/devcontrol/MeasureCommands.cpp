// The live test control's measurements (levels, CPU, keys held, memory, a
// recording of the mix) and what the app said (its log, the messages shown).
#include "DevControl.h"
#include "DevLog.h"

#include "gigchain/engine/IEngine.h"
#include "gigchain/platform/MemoryUse.h"

#include <QAbstractItemModel>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTimer>

#include <cmath>
#include <memory>

using namespace Qt::StringLiterals;

namespace gigchain::devcontrol {
namespace {

// The log's lines written after byte `from` (all of it when it started again, smaller).
QStringList logSince(const QString& path, qint64 from)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    if (from > file.size()) from = 0;
    file.seek(from);
    return QString::fromUtf8(file.readAll()).split(u'\n', Qt::SkipEmptyParts);
}

// "<log bytes>:<messages shown>", from `mark`.
struct Mark
{
    qint64 log = 0;
    qsizetype notices = 0;
};
std::optional<Mark> parseMark(const QString& text)
{
    if (text.isEmpty()) return Mark{};
    const QStringList parts = text.split(u':');
    bool logOk = false;
    bool noticesOk = false;
    const Mark mark{.log = parts.value(0).toLongLong(&logOk), .notices = parts.value(1).toLongLong(&noticesOk)};
    if (parts.size() != 2 || !logOk || !noticesOk) return std::nullopt;
    return mark;
}

// Every message the player was shown since the control started (Notifications), in order.
using Notices = std::vector<QJsonObject>;

// Polls `sample` every `everyMs` until `forMs` passed, then `done` (each only while the client waits).
void sampleFor(QObject* context, int forMs, int everyMs, const Reply& reply, std::function<void()> sample, std::function<void()> done)
{
    auto* timer = new QTimer(context); // (deleted when done)
    timer->setTimerType(Qt::PreciseTimer);
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    sample(); // (the first at once; before it moves into the timer)
    QObject::connect(timer, &QTimer::timeout, timer, [timer, clock, forMs, reply, sample = std::move(sample), done = std::move(done)] {
        if (!reply.alive()) { // (the client went away)
            timer->deleteLater();
            return;
        }
        sample();
        if (clock->elapsed() < forMs) return;
        timer->stop();
        timer->deleteLater();
        done();
    });
    timer->start(everyMs);
}

} // namespace

void addMeasureCommands(DevControl& control)
{
    auto notices = std::make_shared<Notices>();
    QString unused;
    if (auto* shown = qobject_cast<QAbstractItemModel*>(resolve(control, u"document.notifications"_s, unused))) {
        const int textRole = shown->roleNames().key("text", Qt::DisplayRole);
        const int levelRole = shown->roleNames().key("level", -1);
        const auto keep = [shown, notices, textRole, levelRole](int first, int last) {
            for (int row = first; row <= last; ++row) {
                const QModelIndex index = shown->index(row, 0);
                notices->push_back(QJsonObject{{u"text"_s, shown->data(index, textRole).toString()},
                                               {u"level"_s, levelRole < 0 ? 0 : shown->data(index, levelRole).toInt()}});
            }
        };
        QObject::connect(shown, &QAbstractItemModel::rowsInserted, &control, [keep](const QModelIndex&, int first, int last) { keep(first, last); });
        // (A message said again is counted on the one already shown.)
        QObject::connect(shown, &QAbstractItemModel::dataChanged, &control, [keep](const QModelIndex& from, const QModelIndex& to) { keep(from.row(), to.row()); });
    }
    const QString logFile = control.context().logFile;

    control.add(u"mark"_s, u"mark: a place in the log and the messages shown (log, notices and wait take it)"_s,
                [logFile, notices](const DevRequest&, const Reply& reply) {
        reply.ok({{u"mark"_s, u"%1:%2"_s.arg(QFileInfo(logFile).size()).arg(notices->size())}});
    });
    control.add(u"log"_s, u"log [mark] [pattern]: the log lines written since (those matching)"_s, [logFile](const DevRequest& request, const Reply& reply) {
        const auto mark = parseMark(argText(request, 0));
        if (!mark) return reply.fail(u"Not a mark (mark gives one): %1"_s.arg(argText(request, 0)));
        const QRegularExpression pattern(argText(request, 1));
        if (!pattern.isValid()) return reply.fail(u"Not a pattern: %1"_s.arg(pattern.errorString()));
        QStringList lines = logSince(logFile, mark->log);
        lines.removeIf([&pattern](const QString& line) { return !pattern.match(line).hasMatch(); });
        constexpr qsizetype kMost = 2000;
        if (lines.size() > kMost) lines = lines.mid(lines.size() - kMost);
        reply.ok({{u"lines"_s, QJsonArray::fromStringList(lines)}});
    });
    control.add(u"notices"_s, u"notices [mark]: the messages the player was shown since"_s, [notices](const DevRequest& request, const Reply& reply) {
        const auto mark = parseMark(argText(request, 0));
        if (!mark) return reply.fail(u"Not a mark (mark gives one): %1"_s.arg(argText(request, 0)));
        QJsonArray list;
        for (std::size_t i = static_cast<std::size_t>(std::max<qsizetype>(mark->notices, 0)); i < notices->size(); ++i) list.append(notices->at(i));
        reply.ok({{u"notices"_s, list}});
    });
    control.add(u"wait"_s, u"wait <pattern> <ms> [mark]: until a log line or a message shown matches, or says it never came"_s,
                [&control, logFile, notices](const DevRequest& request, const Reply& reply) {
        const QRegularExpression pattern(argText(request, 0));
        const int ms = request.args.size() > 1 ? request.args.at(1).toInt(-1) : -1;
        const auto mark = parseMark(argText(request, 2));
        if (argText(request, 0).isEmpty() || !pattern.isValid() || ms < 0 || ms > 600'000 || !mark) {
            return reply.fail(u"wait <pattern> <ms 0-600000> [mark]"_s);
        }
        auto found = std::make_shared<QString>();
        const auto look = [found, pattern, logFile, notices, mark = *mark] {
            if (!found->isEmpty()) return;
            for (const QString& line : logSince(logFile, mark.log)) {
                if (line.contains(u"Dev control: {"_s)) continue; // (the requests themselves, this one's pattern included)
                if (pattern.match(line).hasMatch()) return void(*found = line);
            }
            for (auto i = static_cast<std::size_t>(mark.notices); i < notices->size(); ++i) {
                const QString text = notices->at(i).value(u"text"_s).toString();
                if (pattern.match(text).hasMatch()) return void(*found = u"(shown) "_s + text);
            }
        };
        auto* timer = new QTimer(&control);
        auto clock = std::make_shared<QElapsedTimer>();
        clock->start();
        QObject::connect(timer, &QTimer::timeout, timer, [timer, clock, ms, reply, look, found, text = argText(request, 0)] {
            look();
            if (!reply.alive() || !found->isEmpty() || clock->elapsed() >= ms) {
                timer->stop();
                timer->deleteLater();
                if (!found->isEmpty()) reply.ok({{u"line"_s, *found}});
                else reply.fail(u"\"%1\" never came in %2 ms"_s.arg(text).arg(ms));
            }
        });
        timer->start(50);
    });

    control.add(u"level"_s, u"level <ms> [channel id]: the loudest peak and RMS of the output (or a channel) over that long"_s,
                [&control](const DevRequest& request, const Reply& reply) {
        engine::IEngine* engine = control.context().engine;
        const int ms = request.args.isEmpty() ? -1 : request.args.at(0).toInt(-1);
        if (engine == nullptr || ms < 0 || ms > 60'000) return reply.fail(u"level <ms 0-60000> [channel id]"_s);
        const QString channel = argText(request, 1);
        if (!channel.isEmpty()) { // (an id the song has: status lists them)
            QString error;
            const auto* channels = qobject_cast<const QAbstractItemModel*>(resolve(control, u"channelModel"_s, error));
            bool known = false;
            for (int row = 0; channels != nullptr && row < channels->rowCount() && !known; ++row) {
                for (const int role : channels->roleNames().keys()) known = known || channels->data(channels->index(row, 0), role).toString() == channel;
            }
            if (!known) return reply.fail(u"No channel %1 in this sound (status lists them)"_s.arg(channel));
        }
        auto loudest = std::make_shared<engine::LevelReading>();
        sampleFor(&control, ms, 5, reply, [engine, channel, loudest] {
            const engine::LevelReading now = channel.isEmpty() ? engine->masterLevel() : engine->channelLevel(core::ChannelId(channel));
            loudest->peak = std::max(loudest->peak, now.peak);
            loudest->rms = std::max(loudest->rms, now.rms);
        }, [reply, loudest] {
            const double db = loudest->peak > 0.0F ? 20.0 * std::log10(static_cast<double>(loudest->peak)) : -200.0;
            reply.ok({{u"peak"_s, loudest->peak}, {u"rms"_s, loudest->rms}, {u"peakDb"_s, std::round(db * 10.0) / 10.0}});
        });
    });
    control.add(u"cpu"_s, u"cpu <ms>: the audio's load (highest, average) and the dropouts logged in that time"_s,
                [&control, logFile](const DevRequest& request, const Reply& reply) {
        engine::IEngine* engine = control.context().engine;
        const int ms = request.args.isEmpty() ? -1 : request.args.at(0).toInt(-1);
        if (engine == nullptr || ms < 0 || ms > 600'000) return reply.fail(u"cpu <ms 0-600000>"_s);
        const qint64 from = QFileInfo(logFile).size();
        auto sum = std::make_shared<std::pair<double, int>>(0.0, 0);
        auto highest = std::make_shared<float>(0.0F);
        sampleFor(&control, ms, 5, reply, [engine, sum, highest] {
            const float load = engine->cpuLoad();
            *highest = std::max(*highest, load);
            sum->first += load;
            ++sum->second;
        }, [reply, sum, highest, logFile, from] {
            // The engine says "Audio dropped out <n> time(s) on <device>" (AudioDevice::poll).
            static const QRegularExpression dropped(u"Audio dropped out (\\d+) time"_s);
            qint64 dropouts = 0;
            for (const QString& line : logSince(logFile, from)) {
                if (const auto found = dropped.match(line); found.hasMatch()) dropouts += found.captured(1).toLongLong();
            }
            reply.ok({{u"highest"_s, *highest}, {u"average"_s, sum->second > 0 ? sum->first / sum->second : 0.0}, {u"dropouts"_s, dropouts}});
        });
    });
    control.add(u"keys"_s, u"keys: the keys, pedal and wheels the instruments hear now (a stuck note shows here)"_s,
                [&control](const DevRequest&, const Reply& reply) {
        const engine::IEngine* engine = control.context().engine;
        if (engine == nullptr) return reply.fail(u"No engine"_s);
        const engine::MidiActivity now = engine->keyboardActivity();
        QJsonArray down;
        for (std::size_t key = 0; key < now.velocity.size(); ++key) {
            if (now.velocity.at(key) > 0) down.append(QJsonObject{{u"key"_s, static_cast<int>(key)}, {u"velocity"_s, now.velocity.at(key)}});
        }
        reply.ok({{u"down"_s, down}, {u"sustain"_s, now.sustain}, {u"pitchBend"_s, now.pitchBend}, {u"modWheel"_s, now.modWheel}});
    });
    control.add(u"process"_s, u"process: the memory the app uses now"_s, [](const DevRequest&, const Reply& reply) {
        const auto bytes = platform::residentBytes();
        if (!bytes) return reply.fail(bytes.error().message);
        reply.ok({{u"memoryMb"_s, std::round(static_cast<double>(*bytes) / (1024.0 * 1024.0) * 10.0) / 10.0}});
    });
    control.add(u"record"_s, u"record <seconds 0.1-120> <file.wav>: the mix, as the audience hears it, to a WAV file"_s,
                [&control](const DevRequest& request, const Reply& reply) {
        engine::IEngine* engine = control.context().engine;
        const double seconds = request.args.isEmpty() ? -1.0 : request.args.at(0).toDouble(-1.0);
        const QString file = QFileInfo(argText(request, 1)).absoluteFilePath();
        if (engine == nullptr || seconds < 0.1 || seconds > 120.0 || argText(request, 1).isEmpty()) {
            return reply.fail(u"record <seconds 0.1-120> <file.wav>"_s);
        }
        if (auto started = engine->startRecording(file); !started) return reply.fail(started.error().message);
        QTimer::singleShot(static_cast<int>(seconds * 1000.0), &control, [engine, reply, file] {
            const auto stopped = engine->stopRecording();
            if (!stopped) return reply.fail(stopped.error().message);
            reply.ok({{u"file"_s, QDir::toNativeSeparators(file)}, {u"seconds"_s, *stopped}});
        });
    });
}

} // namespace gigchain::devcontrol
