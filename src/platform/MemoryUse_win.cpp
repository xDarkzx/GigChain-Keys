#include "gigchain/platform/MemoryUse.h"

#include <windows.h>
#include <psapi.h>

using namespace Qt::StringLiterals;

namespace gigchain::platform {

core::Result<qint64> residentBytes()
{
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0) {
        return core::fail(core::ErrorCode::SystemRefused, u"GetProcessMemoryInfo failed, error %1"_s.arg(GetLastError()));
    }
    return static_cast<qint64>(counters.WorkingSetSize);
}

} // namespace gigchain::platform
