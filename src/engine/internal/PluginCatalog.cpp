#include "PluginCatalog.h"

#include "EngineLog.h"
#include "LoaderErrors.h"

#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>

#include <algorithm>
#include <exception>

using namespace Qt::StringLiterals;

namespace openstage::engine {
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

std::vector<PluginInfo> PluginCatalog::scan(const QString& folder)
{
    std::vector<PluginInfo> plugins;
    if (!QFileInfo(folder).isDir()) {
        qCInfo(lcEngine).noquote() << "No VST3 folder at" << folder;
        return plugins;
    }

    QElapsedTimer timer;
    timer.start();
    QStringList bundles;
    findBundles(folder, 0, bundles);

    int failed = 0;
    for (const QString& bundle : bundles) {
        try {
            const SilentLoaderErrors silent;
            std::string error;
            const auto module = VST3::Hosting::Module::create(bundle.toStdString(), error);
            if (!module) {
                ++failed;
                qCWarning(lcEngine).noquote() << "Skipping plugin" << bundle << ":" << QString::fromStdString(error);
                continue;
            }
            const auto factory = module->getFactory();
            bool found = false;
            for (const auto& info : factory.classInfos()) {
                if (info.category() != kVstAudioEffectClass) continue;
                QString vendor = QString::fromStdString(info.vendor());
                if (vendor.isEmpty()) vendor = QString::fromStdString(factory.info().vendor());
                const bool instrument = QString::fromStdString(info.subCategoriesString()).contains(u"Instrument"_s);
                plugins.push_back(PluginInfo{bundle, QString::fromStdString(info.name()), vendor,
                                             instrument ? PluginKind::Instrument : PluginKind::Effect,
                                             QString::fromStdString(info.subCategoriesString()),
                                             QString::fromStdString(info.version()),
                                             QString::fromStdString(info.ID().toString()),
                                             QString::fromStdString(factory.info().url()),
                                             QString::fromStdString(factory.info().email()),
                                             QString::fromStdString(info.sdkVersion())});
                found = true;
                break; // v1: one plugin per bundle (the first audio class), matching Vst3Node::load
            }
            if (!found) {
                ++failed;
                qCWarning(lcEngine).noquote() << "Skipping plugin" << bundle << ": no audio processor class";
            }
        } catch (const std::exception& e) {
            ++failed;
            qCWarning(lcEngine).noquote() << "Skipping plugin" << bundle << ":" << QString::fromUtf8(e.what());
        }
    }

    std::sort(plugins.begin(), plugins.end(), [](const PluginInfo& a, const PluginInfo& b) {
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });
    qCInfo(lcEngine).noquote() << "Found" << plugins.size() << "VST3 plugins in" << folder << "(" << failed
                               << "skipped ) in" << timer.elapsed() << "ms";
    return plugins;
}

} // namespace openstage::engine
