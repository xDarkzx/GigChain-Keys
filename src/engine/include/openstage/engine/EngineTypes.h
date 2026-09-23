#pragma once

#include <QString>

namespace openstage::engine {

enum class PluginKind
{
    Instrument,
    Effect,
};

struct PluginInfo
{
    QString id;
    QString name;
    QString vendor;
    PluginKind kind = PluginKind::Instrument;
};

// Linear signal level, 0 (silence) to 1 (full scale).
struct LevelReading
{
    float peak = 0.0F;
    float rms = 0.0F;
};

} // namespace openstage::engine
