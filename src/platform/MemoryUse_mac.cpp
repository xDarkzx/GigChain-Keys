#include "gigchain/platform/MemoryUse.h"

#include <mach/mach.h>
#include <mach/mach_error.h>

using namespace Qt::StringLiterals;

namespace gigchain::platform {

core::Result<qint64> residentBytes()
{
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    const kern_return_t result =
        task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): Mach's own buffer type
    if (result != KERN_SUCCESS) {
        return core::fail(core::ErrorCode::SystemRefused,
                          u"task_info failed: %1 (%2)"_s.arg(QString::fromUtf8(mach_error_string(result))).arg(result));
    }
    return static_cast<qint64>(info.resident_size);
}

} // namespace gigchain::platform
