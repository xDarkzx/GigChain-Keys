#include "MixerLayout.h"

#include "InstrumentWindows.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QScreen>
#include <QSettings>

#include <algorithm>
#include <iterator>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

const QString kPlacementKey = u"mixer/placement"_s;
const QString kDetachedKey = u"mixer/detached"_s;
const QString kSideWidthKey = u"mixer/sideWidth"_s;
const QString kGeometryKey = u"mixer/windowGeometry"_s;
const QString kMaximizedKey = u"mixer/windowMaximized"_s;

// A place it knows, else along the bottom (a newer version's, an edited file).
MixerLayout::Placement placementOf(int saved)
{
    return saved >= 0 && saved <= static_cast<int>(MixerLayout::Placement::Right) ? static_cast<MixerLayout::Placement>(saved)
                                                                                  : MixerLayout::Placement::Bottom;
}

} // namespace

MixerLayout::MixerLayout(QSettings& settings, QObject* parent)
    : QObject(parent), m_settings(settings),
      m_placement(placementOf(settings.value(kPlacementKey, 0).toInt())),
      m_detached(settings.value(kDetachedKey, false).toBool()),
      m_sideWidth(std::clamp(settings.value(kSideWidthKey, kDefaultSideWidth).toInt(), kMinSideWidth, kMaxSideWidth))
{
}

void MixerLayout::setPlacement(Placement to)
{
    if (m_placement == to) return;
    m_placement = to;
    m_settings.setValue(kPlacementKey, static_cast<int>(to));
    qCInfo(lcUi) << "Mixer placed" << (to == Placement::Bottom ? "along the bottom" : to == Placement::Left ? "down the left" : "down the right");
    emit placementChanged();
}

void MixerLayout::setDetached(bool to)
{
    if (m_detached == to) return;
    m_detached = to;
    m_settings.setValue(kDetachedKey, to);
    qCInfo(lcUi) << (to ? "Mixer detached into a window of its own" : "Mixer reattached");
    emit detachedChanged();
}

void MixerLayout::setSideWidth(int to)
{
    const int clamped = std::clamp(to, kMinSideWidth, kMaxSideWidth);
    if (m_sideWidth == clamped) return;
    m_sideWidth = clamped;
    m_settings.setValue(kSideWidthKey, clamped);
    emit sideWidthChanged();
}

QRect MixerLayout::restoredGeometry() const
{
    QList<QRect> screens;
    std::ranges::transform(QGuiApplication::screens(), std::back_inserter(screens),
                           [](const QScreen* screen) { return screen->availableGeometry(); });
    const QRect saved = InstrumentWindows::onScreens(m_settings.value(kGeometryKey).toRect(), screens);
    if (saved.isNull() && m_settings.contains(kGeometryKey)) qCInfo(lcUi) << "The mixer window's screen is gone: it opens on the main window";
    return saved;
}

bool MixerLayout::restoredMaximized() const
{
    return !restoredGeometry().isNull() && m_settings.value(kMaximizedKey, false).toBool();
}

void MixerLayout::rememberGeometry(const QRect& normal, bool maximized)
{
    if (normal.isValid() && !normal.isEmpty()) m_settings.setValue(kGeometryKey, normal);
    m_settings.setValue(kMaximizedKey, maximized);
}

} // namespace gigchain::ui
