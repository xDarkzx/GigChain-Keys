#pragma once

#include <QString>

#include <tl/expected.hpp>

#include <utility>

namespace openstage::core {

enum class ErrorCode
{
    FileNotFound,
    FileReadFailed,
    FileWriteFailed,
    FileTooLarge,
    ParseFailed,
    UnsupportedVersion,
    InvalidData,
    LimitExceeded,
    OutOfRange,
    DeviceUnavailable,
};

// A failure the user can be told about: `message` is ready to show in the UI.
struct Error
{
    ErrorCode code;
    QString message;
};

// Return type for anything that can fail for an expected reason (IO, parsing,
// validation). Exceptions are not used for these.
template <typename T>
using Result = tl::expected<T, Error>;

// Shorthand for the failure branch: `return fail(ErrorCode::ParseFailed, msg);`
inline tl::unexpected<Error> fail(ErrorCode code, QString message)
{
    return tl::unexpected<Error>(Error{code, std::move(message)});
}

QString toString(ErrorCode code);

} // namespace openstage::core
