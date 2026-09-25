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
#include <QSaveFile>

#include <algorithm>
#include <exception>
#include <map>
#include <optional>

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
    return Fingerprint{file.isFile() ? file.size() : -1, file.lastModified().toMSecsSinceEpoch()};
}

struct CacheEntry
{
    Fingerprint fingerprint;
    std::optional<PluginInfo> info; // nullopt: it failed to load
    QString error;
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
    entry.fingerprint = Fingerprint{o.value(u"size"_s).toInteger(-1), o.value(u"modified"_s).toInteger(0)};
    if (o.contains(u"error"_s)) {
        entry.error = o.value(u"error"_s).toString();
        return entry;
    }
    entry.info = PluginInfo{o.value(u"path"_s).toString(),
                            o.value(u"name"_s).toString(),
                            o.value(u"vendor"_s).toString(),
                            o.value(u"instrument"_s).toBool() ? PluginKind::Instrument : PluginKind::Effect,
                            o.value(u"subCategories"_s).toString(),
                            o.value(u"version"_s).toString(),
                            o.value(u"classId"_s).toString(),
                            o.value(u"website"_s).toString(),
                            o.value(u"email"_s).toString(),
                            o.value(u"sdkVersion"_s).toString()};
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
    for (const QJsonValue& value : doc.object().value(u"plugins"_s).toArray()) {
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
            entry.info = PluginInfo{bundle,
                                    QString::fromStdString(info.name()),
                                    vendor,
                                    instrument ? PluginKind::Instrument : PluginKind::Effect,
                                    QString::fromStdString(info.subCategoriesString()),
                                    QString::fromStdString(info.version()),
                                    QString::fromStdString(info.ID().toString()),
                                    QString::fromStdString(factory.info().url()),
                                    QString::fromStdString(factory.info().email()),
                                    QString::fromStdString(info.sdkVersion())};
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

} // namespace

std::vector<PluginInfo> PluginCatalog::scan(const QString& folder, const QString& cacheFile, ScanStats* stats,
                                            const Progress& progress, PluginLoadGuard* guard)
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
    std::map<QString, CacheEntry> fresh; // only plugins that exist now
    const int total = static_cast<int>(bundles.size());
    for (int done = 0; done < total; ++done) {
        const QString& bundle = bundles[done];
        if (progress) progress(QFileInfo(bundle).completeBaseName(), done, total);
        const auto hit = cached.find(bundle);
        const bool unchanged = hit != cached.end() && hit->second.fingerprint == fingerprintOf(bundle);
        if (!unchanged && guard != nullptr && guard->isBlocked(bundle)) {
            // It crashed the app while loading: not opened again until the user says so.
            ++counts.failed;
            qCWarning(lcEngine).noquote() << "Skipping plugin" << bundle << ": it crashed the app before (switched off)";
            continue;
        }
        CacheEntry entry;
        if (unchanged) {
            entry = hit->second;
        } else {
            const auto loading = guard != nullptr ? guard->loading(bundle) : PluginLoadGuard().loading(bundle);
            entry = openAndRead(bundle);
        }
        if (unchanged) ++counts.fromCache;
        else ++counts.opened;

        if (entry.info) {
            plugins.push_back(*entry.info);
        } else {
            ++counts.failed;
            qCWarning(lcEngine).noquote() << "Skipping plugin" << bundle << ":" << entry.error
                                          << (unchanged ? u"(failed before; retried when the file changes)"_s : QString());
        }
        fresh.emplace(bundle, std::move(entry));
    }
    if (counts.opened > 0 || fresh.size() != cached.size()) writeCache(cacheFile, fresh);

    std::sort(plugins.begin(), plugins.end(), [](const PluginInfo& a, const PluginInfo& b) {
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });
    qCInfo(lcEngine).noquote() << "Found" << plugins.size() << "VST3 plugins in" << folder << "(" << counts.opened
                               << "opened," << counts.fromCache << "from cache," << counts.failed << "skipped ) in"
                               << timer.elapsed() << "ms";
    return plugins;
}

} // namespace gigchain::engine
