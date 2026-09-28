// The app's own files about plugins (the scan cache, the list of plugins
// that crashed it) can be damaged: a full disk, a crash while writing, a
// hand edit. Whatever is in them: reading them never crashes; a damaged
// file means "no cache" / "nothing blocked", said in the log.
#include "PluginCatalog.h"
#include "PluginLoadGuard.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtLogging>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>

using namespace gigchain::engine;
using namespace Qt::StringLiterals;

namespace {

// What the readers log about damaged files is expected here, thousands of
// times a second: not printed.
void quiet(QtMsgType, const QMessageLogContext&, const QString&) {}

void write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size()) std::abort();
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    static const auto folder = [] {
        qInstallMessageHandler(&quiet);
        auto dir = std::make_unique<QTemporaryDir>();
        if (!dir->isValid() || !QDir(dir->path()).mkpath(u"plugins"_s) || !QDir(dir->path()).mkpath(u"guard"_s)) std::abort();
        return dir;
    }();
    if (size == 0) return 0;
    const QByteArray bytes(reinterpret_cast<const char*>(data) + 1, static_cast<qsizetype>(size - 1)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-bounds-pointer-arithmetic): libFuzzer's bytes after the choice
    if (data[0] % 2 == 0) {
        const QString cache = folder->filePath(u"cache.json"_s);
        write(cache, bytes);
        (void)PluginCatalog::scan(folder->filePath(u"plugins"_s), cache);
    } else {
        write(folder->filePath(u"guard/blocked.json"_s), bytes);
        PluginLoadGuard guard(folder->filePath(u"guard"_s));
        (void)guard.isBlocked(u"C:/Program Files/Common Files/VST3/Some Plugin.vst3"_s);
        (void)guard.takeCrashed();
    }
    return 0;
}
