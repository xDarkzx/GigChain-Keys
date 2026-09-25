#include "PluginModules.h"

#include <QDir>
#include <QFileInfo>

#include <map>
#include <memory>
#include <mutex>

namespace gigchain::engine {
namespace {

std::mutex g_mutex;
// Weak: the library goes when its last instance goes.
std::map<QString, std::weak_ptr<VST3::Hosting::Module>> g_modules;

QString keyFor(const QString& bundlePath)
{
    return QDir::cleanPath(QFileInfo(bundlePath).absoluteFilePath()).toLower(); // Windows: case-insensitive
}

} // namespace

VST3::Hosting::Module::Ptr PluginModules::get(const QString& bundlePath, std::string& error)
{
    const std::lock_guard lock(g_mutex);
    const QString key = keyFor(bundlePath);
    if (const auto it = g_modules.find(key); it != g_modules.end()) {
        if (auto shared = it->second.lock()) return shared;
    }
    auto module = VST3::Hosting::Module::create(QFileInfo(bundlePath).absoluteFilePath().toStdString(), error);
    if (module) g_modules[key] = module;
    else g_modules.erase(key);
    return module;
}

std::size_t PluginModules::loadedCount()
{
    const std::lock_guard lock(g_mutex);
    std::size_t count = 0;
    for (const auto& [key, module] : g_modules) {
        if (!module.expired()) ++count;
    }
    return count;
}

} // namespace gigchain::engine
