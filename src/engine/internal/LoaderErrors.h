#pragma once

#include <windows.h>

namespace openstage::engine {

// While alive, the Windows loader reports bad or missing DLLs (corrupt,
// 32-bit, missing dependency) to the caller as errors instead of showing a
// modal "Bad Image" / "missing DLL" dialog that would block the app. The
// caller still receives the failure and must return and log it. Restores the
// previous mode on exit.
class SilentLoaderErrors
{
public:
    SilentLoaderErrors() { SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &m_previous); }
    ~SilentLoaderErrors() { SetThreadErrorMode(m_previous, nullptr); }
    SilentLoaderErrors(const SilentLoaderErrors&) = delete;
    SilentLoaderErrors& operator=(const SilentLoaderErrors&) = delete;
    SilentLoaderErrors(SilentLoaderErrors&&) = delete;
    SilentLoaderErrors& operator=(SilentLoaderErrors&&) = delete;

private:
    DWORD m_previous = 0;
};

} // namespace openstage::engine
