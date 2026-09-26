#include "PluginCatalog.h"
#include "PluginLoadGuard.h"
#include "PluginModules.h"

#include "EngineLog.h"
#include "LoaderErrors.h"

#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

using namespace Qt::StringLiterals;

namespace gigchain::engine {
namespace {

constexpr int kMaxDepth = 8; // also stops junction loops

void findBundles(const QString& folder, int depth, QStringList& bundles)
{
    if (depth > kMaxDepth) return;
    const QDir dir(folder);
    const auto entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& entry : entries) {
        if (entry.fileName().endsWith(u".vst3"_s, Qt::CaseInsensitive)) {
            bundles << entry.absoluteFilePath(); // a bundle folder or a single-file plugin: do not look inside
        } else if (entry.isDir()) {
            findBundles(entry.absoluteFilePath(), depth + 1, bundles);
        }
    }
}

} // namespace

QString PluginCatalog::standardFolder()
{
    return u"C:/Program Files/Common Files/VST3"_s;
}

namespace {

// What decides whether a plugin changed: its binary's size and date. For a
// bundle folder that is the module inside it.
struct Fingerprint
{
    qint64 size = -1;
    qint64 modified = 0;

    bool operator==(const Fingerprint&) const = default;
};

Fingerprint fingerprintOf(const QString& bundle)
{
    QFileInfo file(bundle);
    if (file.isDir()) {
        const QFileInfo module(bundle + u"/Contents/x86_64-win/"_s + file.fileName());
        if (module.exists()) file = module;
    }
    return Fingerprint{.size = file.isFile() ? file.size() : -1, .modified = file.lastModified().toMSecsSinceEpoch()};
}

struct CacheEntry
{
    Fingerprint fingerprint;
    std::optional<PluginInfo> info; // nullopt: it failed to load
    QString error;
    bool retry = false; // not remembered: read again at the next scan (it hung, or the scanner failed)
};

QJsonObject toJson(const QString& bundle, const CacheEntry& entry)
{
    QJsonObject o{{u"path"_s, bundle},
                  {u"size"_s, entry.fingerprint.size},
                  {u"modified"_s, entry.fingerprint.modified}};
    if (!entry.info) {
        o.insert(u"error"_s, entry.error);
        return o;
    }
    const PluginInfo& p = *entry.info;
    o.insert(u"name"_s, p.name);
    o.insert(u"vendor"_s, p.vendor);
    o.insert(u"instrument"_s, p.kind == PluginKind::Instrument);
    o.insert(u"subCategories"_s, p.subCategories);
    o.insert(u"version"_s, p.version);
    o.insert(u"classId"_s, p.classId);
    o.insert(u"website"_s, p.website);
    o.insert(u"email"_s, p.email);
    o.insert(u"sdkVersion"_s, p.sdkVersion);
    return o;
}

CacheEntry fromJson(const QJsonObject& o)
{
    CacheEntry entry;
    entry.fingerprint = Fingerprint{.size = o.value(u"size"_s).toInteger(-1), .modified = o.value(u"modified"_s).toInteger(0)};
    if (o.contains(u"error"_s)) {
        entry.error = o.value(u"error"_s).toString();
        return entry;
    }
    entry.info = PluginInfo{.id = o.value(u"path"_s).toString(),
                            .name = o.value(u"name"_s).toString(),
                            .vendor = o.value(u"vendor"_s).toString(),
                            .kind = o.value(u"instrument"_s).toBool() ? PluginKind::Instrument : PluginKind::Effect,
                            .subCategories = o.value(u"subCategories"_s).toString(),
                            .version = o.value(u"version"_s).toString(),
                            .classId = o.value(u"classId"_s).toString(),
                            .website = o.value(u"website"_s).toString(),
                            .email = o.value(u"email"_s).toString(),
                            .sdkVersion = o.value(u"sdkVersion"_s).toString()};
    return entry;
}

constexpr int kCacheFormat = 1;

std::map<QString, CacheEntry> readCache(const QString& cacheFile)
{
    std::map<QString, CacheEntry> cache;
    if (cacheFile.isEmpty() || !QFileInfo::exists(cacheFile)) return cache;
    QFile file(cacheFile);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcEngine).noquote() << "Plugin cache" << cacheFile << "is unreadable (" << file.errorString()
                                      << "); scanning every plugin";
        return cache;
    }
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcEngine).noquote() << "Plugin cache" << cacheFile << "is unreadable (" << error.errorString()
                                      << "); scanning every plugin";
        return cache;
    }
    if (doc.object().value(u"format"_s).toInt() != kCacheFormat) {
        qCInfo(lcEngine).noquote() << "Plugin cache" << cacheFile << "is from another version; scanning every plugin";
        return cache;
    }
    const QJsonArray entries = doc.object().value(u"plugins"_s).toArray();
    for (const auto& value : entries) {
        const QJsonObject o = value.toObject();
        cache.emplace(o.value(u"path"_s).toString(), fromJson(o));
    }
    return cache;
}

void writeCache(const QString& cacheFile, const std::map<QString, CacheEntry>& cache)
{
    if (cacheFile.isEmpty()) return;
    QJsonArray plugins;
    for (const auto& [bundle, entry] : cache) plugins.append(toJson(bundle, entry));
    const QJsonDocument doc(QJsonObject{{u"format"_s, kCacheFormat}, {u"plugins"_s, plugins}});
    if (!QDir().mkpath(QFileInfo(cacheFile).absolutePath())) {
        qCWarning(lcEngine).noquote() << "Cannot create the folder for the plugin cache" << cacheFile;
        return;
    }
    QSaveFile out(cacheFile);
    if (!out.open(QIODevice::WriteOnly) || out.write(doc.toJson(QJsonDocument::Indented)) < 0 || !out.commit()) {
        qCWarning(lcEngine).noquote() << "Cannot write the plugin cache" << cacheFile << ":" << out.errorString();
    }
}

// Opens one plugin and reads its factory.
CacheEntry openAndRead(const QString& bundle)
{
    CacheEntry entry;
    entry.fingerprint = fingerprintOf(bundle);
    try {
        const SilentLoaderErrors silent;
        std::string error;
        const auto module = PluginModules::get(bundle, error);
        if (!module) {
            entry.error = QString::fromStdString(error);
            return entry;
        }
        const auto factory = module->getFactory();
        for (const auto& info : factory.classInfos()) {
            if (info.category() != kVstAudioEffectClass) continue;
            QString vendor = QString::fromStdString(info.vendor());
            if (vendor.isEmpty()) vendor = QString::fromStdString(factory.info().vendor());
            const bool instrument = QString::fromStdString(info.subCategoriesString()).contains(u"Instrument"_s);
            entry.info = PluginInfo{.id = bundle,
                                    .name = QString::fromStdString(info.name()),
                                    .vendor = vendor,
                                    .kind = instrument ? PluginKind::Instrument : PluginKind::Effect,
                                    .subCategories = QString::fromStdString(info.subCategoriesString()),
                                    .version = QString::fromStdString(info.version()),
                                    .classId = QString::fromStdString(info.ID().toString()),
                                    .website = QString::fromStdString(factory.info().url()),
                                    .email = QString::fromStdString(factory.info().email()),
                                    .sdkVersion = QString::fromStdString(info.sdkVersion())};
            return entry; // v1: one plugin per bundle (the first audio class), matching Vst3Node::load
        }
        entry.error = u"no audio processor class"_s;
    } catch (const std::exception& e) {
        entry.error = QString::fromUtf8(e.what());
    } catch (...) {
        entry.error = u"it failed while being read"_s;
    }
    return entry;
}

// The scanner program's exit codes (src/scanner/main.cpp); anything else is
// the plugin crashing it.
constexpr int kScannerRead = 0;
constexpr int kScannerUsage = 2;
constexpr int kScannerCannotWrite = 3;

// Reads one plugin in a scanner process (Audacity 4's --register-audio-plugin):
// what it found comes back in `resultFile`.
CacheEntry readInScanner(const QString& scanner, const QString& bundle, const QString& resultFile)
{
    CacheEntry entry;
    entry.fingerprint = fingerprintOf(bundle);
    QProcess process;
    process.setProgram(scanner);
    process.setArguments({bundle, resultFile});
    process.setProcessChannelMode(QProcess::ForwardedErrorChannel); // its own log lines, if any
    process.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* arguments) { arguments->flags |= CREATE_NO_WINDOW; });
    process.start();
    if (!process.waitForStarted(PluginCatalog::kScanTimeoutMs)) {
        entry.error = u"the plugin scanner did not start (%1)"_s.arg(process.errorString());
        entry.retry = true;
        return entry;
    }
    if (!process.waitForFinished(PluginCatalog::kScanTimeoutMs)) {
        process.kill();
        process.waitForFinished();
        entry.error = u"it did not finish being read within %1 s (tried again next start)"_s.arg(PluginCatalog::kScanTimeoutMs / 1000);
        entry.retry = true;
        return entry;
    }
    const auto code = static_cast<uint32_t>(process.exitCode());
    if (process.exitStatus() == QProcess::NormalExit && code == kScannerRead) {
        QFile file(resultFile);
        const QJsonDocument doc = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()) : QJsonDocument();
        if (doc.isObject()) return fromJson(doc.object());
        entry.error = u"the plugin scanner's result was unreadable"_s;
        entry.retry = true;
        return entry;
    }
    if (process.exitStatus() == QProcess::NormalExit && (code == kScannerUsage || code == kScannerCannotWrite)) {
        entry.error = u"the plugin scanner failed (exit code %1; see its log lines)"_s.arg(code);
        entry.retry = true;
        return entry;
    }
    entry.error = u"it crashed while being read (exit code 0x%1)"_s.arg(code, 8, 16, QLatin1Char('0'));
    return entry;
}

// Reads `bundles` in scanner processes, as many at once as the machine has
// cores. `finished(i)` is called here (the calling thread), as each is read.
std::vector<CacheEntry> readAllInScanner(const QString& scanner, const QStringList& bundles,
                                         const std::function<void(int)>& finished)
{
    const auto count = static_cast<int>(bundles.size());
    std::vector<CacheEntry> results(static_cast<std::size_t>(count));
    if (count == 0) return results; // everything came from the cache
    const QTemporaryDir work;
    if (!work.isValid()) {
        for (int i = 0; i < count; ++i) {
            CacheEntry& entry = results.at(static_cast<std::size_t>(i));
            entry.fingerprint = fingerprintOf(bundles.at(i));
            entry.error = u"no folder for the plugin scanner's results (%1)"_s.arg(work.errorString());
            entry.retry = true;
            finished(i);
        }
        return results;
    }
    std::atomic<int> next{0};
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<int> done;
    const int workers = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, count);
    int running = workers;
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(workers));
    for (int w = 0; w < workers; ++w) {
        threads.emplace_back([&] {
            for (int i = next++; i < count; i = next++) {
                CacheEntry entry = readInScanner(scanner, bundles.at(i), work.filePath(QString::number(i) + u".json"_s));
                const std::scoped_lock lock(mutex);
                results.at(static_cast<std::size_t>(i)) = std::move(entry);
                done.push_back(i);
                changed.notify_one();
            }
            const std::scoped_lock lock(mutex);
            --running;
            changed.notify_one();
        });
    }
    for (int reported = 0; reported < count;) {
        std::vector<int> ready;
        {
            std::unique_lock lock(mutex);
            changed.wait(lock, [&] { return !done.empty() || running == 0; });
            ready.swap(done);
            if (ready.empty() && running == 0) break;
        }
        for (const int i : ready) {
            finished(i);
            ++reported;
        }
    }
    for (std::thread& thread : threads) thread.join();
    return results;
}

} // namespace

bool PluginCatalog::readToFile(const QString& bundle, const QString& resultFile)
{
    const QJsonDocument doc(toJson(bundle, openAndRead(bundle)));
    QSaveFile out(resultFile);
    if (!out.open(QIODevice::WriteOnly) || out.write(doc.toJson(QJsonDocument::Compact)) < 0 || !out.commit()) {
        qCWarning(lcEngine).noquote() << "Cannot write the scan result" << resultFile << ":" << out.errorString();
        return false;
    }
    return true;
}

std::vector<PluginInfo> PluginCatalog::scan(const QString& folder, const QString& cacheFile, ScanStats* stats,
                                            const Progress& progress, const PluginLoadGuard* guard, const QString& scanner)
{
    ScanStats local;
    ScanStats& counts = stats != nullptr ? *stats : local;
    counts = {};
    std::vector<PluginInfo> plugins;
    if (!QFileInfo(folder).isDir()) {
        qCInfo(lcEngine).noquote() << "No VST3 folder at" << folder;
        return plugins;
    }

    QElapsedTimer timer;
    timer.start();
    QStringList bundles;
    findBundles(folder, 0, bundles);

    const std::map<QString, CacheEntry> cached = readCache(cacheFile);
    const int total = static_cast<int>(bundles.size());
    int reported = 0;
    auto report = [&](const QString& bundle) {
        if (progress) progress(QFileInfo(bundle).completeBaseName(), reported, total);
        ++reported;
    };

    // Each plugin: known already, switched off, or to be read.
    struct Found
    {
        QString bundle;
        std::optional<CacheEntry> entry; // nullopt: switched off
        bool unchanged = false;
    };
    std::vector<Found> found;
    found.reserve(static_cast<std::size_t>(total));
    std::vector<Found*> toRead;
    for (const QString& bundle : std::as_const(bundles)) {
        Found& f = found.emplace_back(Found{.bundle = bundle, .entry = std::nullopt, .unchanged = false});
        const auto hit = cached.find(bundle);
        if (hit != cached.end() && hit->second.fingerprint == fingerprintOf(bundle)) {
            f.entry = hit->second;
            f.unchanged = true;
            ++counts.fromCache;
            report(bundle);
        } else if (guard != nullptr && guard->isBlocked(bundle)) {
            // It crashed the app while loading: not opened again until the user says so.
            qCWarning(lcEngine).noquote() << "Skipping plugin" << bundle << ": it crashed the app before (switched off)";
            ++counts.failed;
            report(bundle);
        } else {
            toRead.push_back(&f);
        }
    }
    counts.opened = static_cast<int>(toRead.size());

    // New and changed plugins: each in a scanner process, or here.
    const bool outOfProcess = !scanner.isEmpty() && QFileInfo(scanner).isFile();
    if (!scanner.isEmpty() && !outOfProcess && !toRead.empty()) {
        qCWarning(lcEngine).noquote() << "The plugin scanner" << scanner
                                      << "was not found: reading plugins in this process (one that crashes takes the app with it)";
    }
    if (outOfProcess) {
        QStringList paths;
        for (const Found* f : toRead) paths << f->bundle;
        std::vector<CacheEntry> read =
            readAllInScanner(scanner, paths, [&](int j) { report(toRead.at(static_cast<std::size_t>(j))->bundle); });
        for (std::size_t j = 0; j < read.size(); ++j) toRead.at(j)->entry = std::move(read.at(j));
    } else {
        for (Found* f : toRead) {
            report(f->bundle);
            const auto loading = guard != nullptr ? guard->loading(f->bundle) : PluginLoadGuard().loading(f->bundle);
            f->entry = openAndRead(f->bundle);
        }
    }

    std::map<QString, CacheEntry> fresh; // only plugins that exist now, and not those to try again
    for (Found& f : found) {
        if (!f.entry) continue; // switched off (said above)
        if (f.entry->info) {
            plugins.push_back(*f.entry->info);
        } else {
            ++counts.failed;
            qCWarning(lcEngine).noquote() << "Skipping plugin" << f.bundle << ":" << f.entry->error
                                          << (f.unchanged ? u"(failed before; retried when the file changes)"_s : QString());
        }
        if (!f.entry->retry) fresh.emplace(f.bundle, std::move(*f.entry));
    }
    if (counts.opened > 0 || fresh.size() != cached.size()) writeCache(cacheFile, fresh);

    std::ranges::sort(plugins, [](const PluginInfo& a, const PluginInfo& b) {
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });
    qCInfo(lcEngine).noquote() << "Found" << plugins.size() << "VST3 plugins in" << folder << "(" << counts.opened
                               << "opened," << counts.fromCache << "from cache," << counts.failed << "skipped ) in"
                               << timer.elapsed() << "ms";
    return plugins;
}

} // namespace gigchain::engine
