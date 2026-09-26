#include "PluginModules.h"

#include <QDir>
#include <QFileInfo>

#include <map>
#include <memory>
#include <mutex>

namespace gigchain::engine {
namespace {

// The libraries loaded now, one per plugin file. Weak: a library goes when
// its last instance goes. Built on first use (nothing thrown before main).
struct Loaded
{
    std::mutex mutex;
    std::map<QString, std::weak_ptr<VST3::Hosting::Module>> modules;
};

Loaded& loaded()
{
    static Loaded instance;
    return instance;
}

QString keyFor(const QString& bundlePath)
{
    return QDir::cleanPath(QFileInfo(bundlePath).absoluteFilePath()).toLower(); // Windows: case-insensitive
}

} // namespace

VST3::Hosting::Module::Ptr PluginModules::get(const QString& bundlePath, std::string& error)
{
    Loaded& all = loaded();
    const std::scoped_lock lock(all.mutex);
    const QString key = keyFor(bundlePath);
    if (const auto it = all.modules.find(key); it != all.modules.end()) {
        if (auto shared = it->second.lock()) return shared;
    }
    auto module = VST3::Hosting::Module::create(QFileInfo(bundlePath).absoluteFilePath().toStdString(), error);
    if (module) all.modules[key] = module;
    else all.modules.erase(key);
    return module;
}

std::size_t PluginModules::loadedCount()
{
    Loaded& all = loaded();
    const std::scoped_lock lock(all.mutex);
    std::size_t count = 0;
    for (const auto& [key, module] : all.modules) {
        if (!module.expired()) ++count;
    }
    return count;
}

} // namespace gigchain::engine
