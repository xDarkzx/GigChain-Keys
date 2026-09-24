#pragma once

#include "gigchain/core/Error.h"
#include "gigchain/core/Model.h"

namespace gigchain::core {

// `path` names the value in error messages, e.g. "songs[2].name".
Result<void> validateName(const QString& name, const QString& path);
Result<void> validateChannel(const Channel& channel, const QString& path);

// Checks every limit, every range and that all ids are present and unique.
Result<void> validate(const Setlist& setlist);

} // namespace gigchain::core
