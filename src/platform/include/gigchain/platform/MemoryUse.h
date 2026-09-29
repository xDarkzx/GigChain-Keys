#pragma once

#include "gigchain/core/Error.h"

#include <QtGlobal>

namespace gigchain::platform {

// The memory this process is using now (its working set, or resident set),
// in bytes; an error with the reason when the system would not say.
[[nodiscard]] core::Result<qint64> residentBytes();

} // namespace gigchain::platform
