#pragma once

#include "openstage/core/Error.h"
#include "openstage/core/Model.h"

namespace openstage::core {

// `path` names the value in error messages, e.g. "songs[2].name".
Result<void> validateName(const QString& name, const QString& path);
Result<void> validateChannel(const Channel& channel, const QString& path);

// Checks every limit, every range and that all ids are present and unique.
Result<void> validate(const Setlist& setlist);

} // namespace openstage::core
