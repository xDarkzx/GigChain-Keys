#pragma once

#include "openstage/core/Error.h"

#include <QSizeF>
#include <QString>

#include <optional>

namespace openstage::engine {

// Arturia plugins refuse VST3 host zoom and resize. Their window size is
// their own "GUI Size" setting (the "Resize Window" menu), kept per plugin in
// <ProgramData>/Arturia/<plugin>/tmp/plugin.pref.xml and read when the plugin
// loads. Values are 0.0-1.0 in 0.1 steps (measured, Piano V2 2026-09-24).

// Where Arturia's own data lives.
[[nodiscard]] QString arturiaDataRoot(); // C:/ProgramData/Arturia

// The plugin's settings file, if `bundlePath` is an Arturia plugin (in an
// "Arturia" VST3 folder) that has one.
[[nodiscard]] std::optional<QString> arturiaPrefsFile(const QString& bundlePath, const QString& dataRoot);

// Window scale for a GUI Size value: 50-100 % in 10 % steps up to 0.5, then
// 120-200 % in 20 % steps.
[[nodiscard]] double arturiaScale(double guiSize);

// The largest GUI Size whose window fits `area`, for a plugin whose window
// is `fullSize` at 100 %. Never below 0.0 (50 %) or above 1.0 (200 %).
[[nodiscard]] double fitArturiaGuiSize(QSizeF fullSize, QSizeF area);

[[nodiscard]] core::Result<double> readArturiaGuiSize(const QString& prefsFile);
// Changes only the GUI Size value; the file is replaced atomically.
core::Result<void> writeArturiaGuiSize(const QString& prefsFile, double guiSize);

} // namespace openstage::engine
