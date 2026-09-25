#pragma once

#include <QThread>

// Safeguards, written the way Audacity 4 / Muse guards its code.
//
// GC_IF_FAILED(cond) { fallback } — for what must never happen (a broken
// invariant, a null that cannot be null). It is logged with the condition,
// file and line; a debug build stops right there, so the mistake is found in
// testing; a release build runs the block, a safe way out, and plays on.
// (Muse: IF_ASSERT_FAILED.)
//
//     GC_IF_FAILED(channel.instrument) { continue; }
//
// Input that can legitimately be wrong (a stale index from the UI, a file)
// is not a check: it is rejected with a message saying why.
#define GC_IF_FAILED(cond) \
    if (!static_cast<bool>(cond) && gigchain::core::checks::failed(#cond, __FILE__, __LINE__))

// Thread rules at the top of a function (Muse: ONLY_MAIN_THREAD /
// ONLY_AUDIO_THREAD). Debug builds (every test run) stop on a call from the
// wrong thread, so a race shows up in testing, not on stage. Release builds:
// nothing, and nothing costs time on the audio thread.
#ifdef QT_DEBUG
#define GC_ONLY_MAIN_THREAD() Q_ASSERT_X(QThread::isMainThread(), __func__, "must run on the main thread")
#define GC_ONLY_AUDIO_THREAD() Q_ASSERT_X(!QThread::isMainThread(), __func__, "must run on the audio thread")
#else
#define GC_ONLY_MAIN_THREAD() static_cast<void>(0)
#define GC_ONLY_AUDIO_THREAD() static_cast<void>(0)
#endif

namespace gigchain::core::checks {

// Logs a failed check; stops a debug build unless setFatal(false). Returns
// true (the fallback runs). Not for the audio thread (it logs).
bool failed(const char* condition, const char* file, int line);

// Tests of the fallback paths turn stopping off; the check is still logged.
void setFatal(bool fatal);

} // namespace gigchain::core::checks
