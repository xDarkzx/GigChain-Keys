// Built into every test program: tests run unattended, so a C runtime
// assertion or error (debug builds) and a crash are written to the test's
// output and end it, never shown as a dialog box that waits for a click.
#include <windows.h>

#include <crtdbg.h>
#include <cstdlib>
#include <initializer_list>

namespace {

void noErrorDialogs() noexcept
{
    for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
}

} // namespace

// Run by the C runtime before main (its initializer section): nothing to throw.
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) void (*gigchainNoErrorDialogs)() = &noErrorDialogs; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): the runtime's own mechanism
