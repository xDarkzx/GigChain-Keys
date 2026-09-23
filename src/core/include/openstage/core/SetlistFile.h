#pragma once

#include "openstage/core/Error.h"
#include "openstage/core/Model.h"

#include <QString>

namespace openstage::core {

// Reads and validates a setlist file. Files over limits::kMaxFileBytes are
// rejected before being read.
Result<Setlist> loadSetlistFile(const QString& path);

// Validates, then writes atomically (temp file + rename): if anything fails,
// an existing file at `path` is left exactly as it was.
Result<void> saveSetlistFile(const Setlist& setlist, const QString& path);

} // namespace openstage::core
