#include "SettingsMigration.h"

#include "gigchain/core/Branding.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QSettings>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {

bool carryOverSettings(QSettings& previous, QSettings& current)
{
    if (!current.allKeys().isEmpty()) return false; // the current name already has settings
    const QStringList keys = previous.allKeys();
    if (keys.isEmpty()) return false;
    for (const QString& key : keys) current.setValue(key, previous.value(key));

    // A remembered setlist may have been renamed to the current extension.
    const QString lastKey = u"session/lastFile"_s;
    const QString last = current.value(lastKey).toString();
    if (!last.isEmpty() && !QFileInfo::exists(last)) {
        for (const QString& extension : branding::previousFileExtensions()) {
            const QString oldSuffix = u"."_s + extension + u".json"_s;
            if (!last.endsWith(oldSuffix, Qt::CaseInsensitive)) continue;
            const QString renamed = last.left(last.size() - oldSuffix.size()) + branding::setlistSuffix();
            if (QFileInfo::exists(renamed)) current.setValue(lastKey, renamed);
        }
    }
    current.sync();
    if (current.status() != QSettings::NoError) {
        qCWarning(lcUi).noquote() << "Could not save the settings carried over from" << previous.fileName();
        return false;
    }
    qCInfo(lcUi).noquote() << "Carried over" << keys.size() << "settings from" << previous.fileName();
    return true;
}

void carryOverPreviousSettings(QSettings& current)
{
    for (const QString& pair : branding::previousSettings()) {
        const QStringList parts = pair.split(u'/');
        if (parts.size() != 2) {
            qCWarning(lcUi).noquote() << "branding.cmake: PRODUCT_PREVIOUS_SETTINGS entry" << pair
                                      << "is not organization/application";
            continue;
        }
        QSettings previous(parts[0], parts[1]);
        if (carryOverSettings(previous, current)) return;
    }
}

} // namespace gigchain::ui
