#pragma once

#include "openstage/engine/EngineTypes.h"

#include <QHash>
#include <QString>

#include <functional>

namespace openstage::ui {

// Paths of a plugin's official images (empty when it has none).
struct PluginArtwork
{
    QString banner; // wide picture for the browser card
    QString icon;   // square product icon (mixer strip)
    QString logo;   // vendor/product logo
    QString source; // "VST3 snapshot", "NKS" or "Arturia"

    [[nodiscard]] bool isEmpty() const { return banner.isEmpty() && icon.isEmpty() && logo.isEmpty(); }
};

// Finds the artwork plugin makers publish for hosts — never screenshots.
// Sources, in order:
//  1. VST3 snapshot inside the bundle (Steinberg, VST SDK 3.6.10+):
//     Contents/Resources/Snapshots/<class id>_snapshot[_2.0x].png
//  2. NKS artwork (Native Instruments' standard, used by Komplete Kontrol /
//     Maschine): the product is matched through NI's Service Center XML
//     (BinName = plugin file name); images live in the product's registered
//     ContentDir under PAResources/image/<Company>/<Name>/ or in the shared
//     "NI Resources/image" folder: MST_Artwork, VB_Artwork, MST_Logo, OSO_Logo.
//  3. Arturia's own product images: <ProgramData>/Arturia/<Product>/
//     resources/images/banner_browser.png and desktop-icon.png.
// Only reads files; never loads a plugin.
class OfficialArtwork
{
public:
    struct Sources
    {
        QString arturiaRoot;     // C:/ProgramData/Arturia
        QString niServiceCenter; // .../Common Files/Native Instruments/Service Center
        QString niResources;     // C:/Users/Public/Documents/NI Resources
        // Registry lookup: HKLM/SOFTWARE/Native Instruments/<key>/ContentDir
        std::function<QString(const QString& regKey)> niContentDir;
    };

    static Sources defaultSources();
    explicit OfficialArtwork(Sources sources);

    [[nodiscard]] PluginArtwork find(const engine::PluginInfo& plugin) const;

private:
    struct NiProduct
    {
        QString name;
        QString company;
        QString regKey;
    };

    [[nodiscard]] PluginArtwork fromSnapshot(const engine::PluginInfo& plugin) const;
    [[nodiscard]] PluginArtwork fromNks(const engine::PluginInfo& plugin) const;
    [[nodiscard]] PluginArtwork fromArturia(const engine::PluginInfo& plugin) const;

    Sources m_sources;
    QHash<QString, NiProduct> m_niProducts; // by lower-case BinName
};

} // namespace openstage::ui
