#pragma once

#include <QString>
#include <QStringList>
#include <Qt>

namespace gigchain::platform {

// The folders where this system keeps VST3 plugins, absolute, in the order
// they are searched (the VST3 standard for each system).
[[nodiscard]] QStringList standardVst3Folders();

// The file inside a .vst3 bundle folder holding this system's binary (what
// decides whether a plugin changed).
[[nodiscard]] QString vst3ModuleFile(const QString& bundle);

// The folders where this system keeps VST2 plugins, absolute (those that
// exist or not: the scan skips missing ones). Windows: the folder the
// registry names (HKLM\SOFTWARE\VST, VSTPluginsPath) and the usual ones.
[[nodiscard]] QStringList standardVst2Folders();
// Whether `path` is a VST2 plugin's file by its name on this system (a .dll
// on Windows, a .vst bundle on the Mac, a .so on Linux).
[[nodiscard]] bool isVst2PluginFile(const QString& path);
// The library to load for a VST2 plugin (the bundle's binary on the Mac;
// the file itself elsewhere).
[[nodiscard]] QString vst2LibraryFile(const QString& plugin);

// Whether two paths that differ only in case are the same file here.
[[nodiscard]] Qt::CaseSensitivity fileNameCase();

} // namespace gigchain::platform
