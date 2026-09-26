#pragma once

class QSettings;

namespace gigchain::ui {

// After a rename (branding.cmake), copies the settings saved under an
// earlier name into the current, still empty settings, so nothing has to be
// set up again. A remembered setlist whose file was renamed to the current
// extension is found again. Returns true when anything was copied (logged).
// Never overwrites existing settings.
bool carryOverSettings(const QSettings& previous, QSettings& current);

// Tries every earlier name listed in branding.cmake, newest first.
void carryOverPreviousSettings(QSettings& current);

} // namespace gigchain::ui
