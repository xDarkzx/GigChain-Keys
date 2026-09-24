#pragma once

#include "gigchain/core/Error.h"
#include "gigchain/core/Model.h"

#include <QByteArray>

namespace gigchain::core {

// 2: songs gained chart, key, tempo, notes, links and attachments. Files in
// format 1 still open (those fields empty).
inline constexpr int kSetlistFormatVersion = 2;

// Serialises a setlist as indented JSON with "formatVersion".
QByteArray toJson(const Setlist& setlist);

// Parses and fully validates untrusted JSON. Every field is required and
// type-checked; sizes and counts are checked before any work is done on them.
Result<Setlist> fromJson(const QByteArray& bytes);

} // namespace gigchain::core
