#pragma once

#include "gigchain/engine/EngineTypes.h"

#include <QQuickImageProvider>
#include <QString>

namespace gigchain::ui {

// Paths of a plugin's own images (empty when it has none).
struct PluginArtwork
{
    QString banner; // wide picture for the browser card
    QString icon;   // square icon (browser card, mixer strip)

    [[nodiscard]] bool isEmpty() const { return banner.isEmpty() && icon.isEmpty(); }
};

// Finds the images a plugin installs in its own folder, the same way for
// every plugin and every maker (never screenshots, never a maker's name):
//  - banner: the VST3 snapshot inside the bundle (VST SDK 3.6.10+),
//    Contents/Resources/Snapshots/<class id>_snapshot[_2.0x].png
//  - icon: PlugIn.ico, the VST3 bundle's Windows folder icon, in the bundle
//    itself, else in the folders around it up to the plugin folder (a maker's
//    folder, e.g. VST3/<Maker>/PlugIn.ico, shared by that maker's plugins).
// Only reads files; never loads a plugin.
class OfficialArtwork
{
public:
    // `pluginFolder`: the folder the plugins were found in; icons are looked
    // for up to it, never in it or above it. Empty: only inside the bundle.
    explicit OfficialArtwork(const QString& pluginFolder);

    [[nodiscard]] PluginArtwork find(const engine::PluginInfo& plugin) const;

private:
    [[nodiscard]] static QString snapshot(const engine::PluginInfo& plugin);
    [[nodiscard]] QString icon(const engine::PluginInfo& plugin) const;

    QString m_pluginFolder; // cleaned absolute path
};

// Serves a plugin's icon to QML at its sharpest: an .ico holds several sizes
// (16 to 256 px) and a plain Image would show the first, often the smallest.
// Registered on each QML engine as "plugin-icon"; see PluginIconProvider::url().
class PluginIconProvider : public QQuickImageProvider
{
public:
    static constexpr auto kName = "plugin-icon";
    PluginIconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    // Adds a provider to `engine` (which owns it).
    static void install(QQmlEngine& engine);
    // The QML source for an icon file (empty for no file).
    static QString url(const QString& iconPath);
    // The largest image in the file, scaled down to `requestedSize` if given.
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

} // namespace gigchain::ui
