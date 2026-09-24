#pragma once

#include "openstage/engine/EngineTypes.h"

#include <QString>

namespace openstage::ui {

// The icon (a file in the module's icons/ folder, without ".svg") that best
// describes a plugin: from its VST3 sub-categories first, then its name,
// then a generic icon for its kind. Icons are Tabler Icons (MIT).
QString iconFor(const engine::PluginInfo& plugin);

// The qrc URL QML uses for an icon name.
QString iconUrl(const QString& icon);

} // namespace openstage::ui
