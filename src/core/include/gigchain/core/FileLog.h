#pragma once

#include "gigchain/core/Error.h"

#include <QString>

namespace gigchain::core {

// Records every Qt log message (qDebug/qInfo/qWarning/qCritical, all
// categories) to a file, and still passes each one on to the previous handler
// (debugger output / console / Qt Test). Errors are both returned
// to the caller and logged, so this file is the record of everything that went
// wrong, even when no UI showed it.
//
// Never log from the audio thread: it takes a lock and does file IO.
class FileLog
{
public:
    // When the file is larger than this at install time it is moved to
    // `<path>.1` (replacing any older one) and a new file is started.
    static constexpr qint64 kMaxBytes = 5LL * 1024 * 1024;

    // Creates the folder if needed and appends to `path`.
    static Result<void> install(const QString& path);
    // Restores the previous handler and closes the file.
    static void uninstall();
};

} // namespace gigchain::core
