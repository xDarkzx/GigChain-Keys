#pragma once

#include "openstage/core/Error.h"

#include <QImage>
#include <QObject>
#include <QString>

namespace openstage::ui {

// Pictures of plugins (captured from their own editors), stored as PNG files
// in `folder`. A picture belongs to one version of a plugin: when the plugin's
// file changes (updated), its old picture no longer counts.
class ArtworkCache : public QObject
{
    Q_OBJECT

public:
    static constexpr int kMaxWidth = 640;

    explicit ArtworkCache(QString folder, QObject* parent = nullptr);

    // Default location: %LOCALAPPDATA%/OpenStage/OpenStage/artwork
    static QString defaultFolder();

    [[nodiscard]] bool has(const QString& pluginId) const;
    // file: URL of the picture, or empty when there is none for this version.
    [[nodiscard]] QString urlFor(const QString& pluginId) const;
    // Scales down to kMaxWidth and saves. Failures are returned and logged.
    core::Result<void> store(const QString& pluginId, const QImage& image);

signals:
    void artworkChanged(const QString& pluginId);

private:
    [[nodiscard]] QString pathFor(const QString& pluginId) const;

    QString m_folder;
};

} // namespace openstage::ui
