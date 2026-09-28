#include "gigchain/engine/ProcessHardening.h"

#include "EngineLog.h"

#include <windows.h>

using namespace Qt::StringLiterals;

namespace gigchain::engine {

core::Result<void> hardenDllSearch()
{
    // An empty DLL directory removes the current folder from the search
    // (unlike SetDefaultDllDirectories, plugins keep finding DLLs of their
    // own through their usual search).
    if (SetDllDirectoryW(L"") == FALSE) {
        const QString why = u"Could not take the current folder out of the DLL search (error %1)"_s.arg(GetLastError());
        qCWarning(lcEngine).noquote() << why;
        return core::fail(core::ErrorCode::SystemRefused, why);
    }
    return {};
}

} // namespace gigchain::engine
