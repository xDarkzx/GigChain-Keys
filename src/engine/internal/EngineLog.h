#pragma once

#include <QLoggingCategory>

// Log category for the engine module. Main thread only: the audio thread
// never logs (it counts problems in atomics that the main thread reports).
Q_DECLARE_LOGGING_CATEGORY(lcEngine)
