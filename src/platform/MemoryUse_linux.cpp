#include "gigchain/platform/MemoryUse.h"

#include <QFile>

using namespace Qt::StringLiterals;

namespace gigchain::platform {

core::Result<qint64> residentBytes()
{
    QFile status(u"/proc/self/status"_s);
    if (!status.open(QIODevice::ReadOnly)) {
        return core::fail(core::ErrorCode::SystemRefused, u"cannot read /proc/self/status: %1"_s.arg(status.errorString()));
    }
    // "VmRSS:    123456 kB"
    for (const QByteArray& line : status.readAll().split('\n')) {
        if (!line.startsWith("VmRSS:")) continue;
        bool ok = false;
        const qint64 kb = line.mid(6).trimmed().split(' ').value(0).toLongLong(&ok);
        if (ok) return kb * 1024;
    }
    return core::fail(core::ErrorCode::ParseFailed, u"no VmRSS line in /proc/self/status"_s);
}

} // namespace gigchain::platform
