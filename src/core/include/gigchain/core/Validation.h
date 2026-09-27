#pragma once

#include "gigchain/core/Error.h"
#include "gigchain/core/Model.h"

namespace gigchain::core {

// `path` names the value in error messages, e.g. "songs[2].name".
Result<void> validateName(const QString& name, const QString& path);
Result<void> validateChannel(const Channel& channel, const QString& path);
// A plain file name inside the setlist's folder (no folders, drives or "..").
Result<void> validateFileName(const QString& name, const QString& path);
// The song's time signature and section setups.
Result<void> validateSections(const Song& song, const QString& path);
// The looper's learned keyboard controls.
Result<void> validateLoopControls(const LoopControls& controls);

// Checks every limit, every range and that all ids are present and unique.
Result<void> validate(const Setlist& setlist);

} // namespace gigchain::core
