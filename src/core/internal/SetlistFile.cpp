#include "gigchain/core/SetlistFile.h"

#include "gigchain/core/Limits.h"
#include "gigchain/core/SetlistJson.h"
#include "gigchain/core/Validation.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace gigchain::core {

Result<Setlist> loadSetlistFile(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        return fail(ErrorCode::FileNotFound, u"File not found: %1"_s.arg(path));
    }
    if (!info.isFile()) {
        return fail(ErrorCode::FileReadFailed, u"%1 is not a file"_s.arg(path));
    }
    if (info.size() > limits::kMaxFileBytes) {
        return fail(ErrorCode::FileTooLarge,
                    u"%1 is larger than %2 bytes and is not a setlist"_s.arg(path).arg(limits::kMaxFileBytes));
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(ErrorCode::FileReadFailed, u"Could not open %1: %2"_s.arg(path, file.errorString()));
    }
    // Read one byte past the limit so a file that grew after the size check
    // is still rejected by fromJson.
    const QByteArray bytes = file.read(limits::kMaxFileBytes + 1);
    if (file.error() != QFileDevice::NoError) {
        return fail(ErrorCode::FileReadFailed, u"Could not read %1: %2"_s.arg(path, file.errorString()));
    }
    return fromJson(bytes);
}

Result<void> saveSetlistFile(const Setlist& setlist, const QString& path)
{
    if (auto valid = validate(setlist); !valid) return valid;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return fail(ErrorCode::FileWriteFailed, u"Could not save %1: %2"_s.arg(path, file.errorString()));
    }
    const QByteArray bytes = toJson(setlist);
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return fail(ErrorCode::FileWriteFailed, u"Could not save %1: %2"_s.arg(path, file.errorString()));
    }
    if (!file.commit()) {
        return fail(ErrorCode::FileWriteFailed, u"Could not save %1: %2"_s.arg(path, file.errorString()));
    }
    return {};
}

} // namespace gigchain::core
