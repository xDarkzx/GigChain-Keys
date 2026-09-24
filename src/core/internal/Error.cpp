#include "gigchain/core/Error.h"

using namespace Qt::StringLiterals;

namespace gigchain::core {

QString toString(ErrorCode code)
{
    switch (code) {
    case ErrorCode::FileNotFound: return u"File not found"_s;
    case ErrorCode::FileReadFailed: return u"Could not read file"_s;
    case ErrorCode::FileWriteFailed: return u"Could not write file"_s;
    case ErrorCode::FileTooLarge: return u"File too large"_s;
    case ErrorCode::ParseFailed: return u"Could not parse file"_s;
    case ErrorCode::UnsupportedVersion: return u"Unsupported file version"_s;
    case ErrorCode::InvalidData: return u"Invalid data"_s;
    case ErrorCode::LimitExceeded: return u"Limit exceeded"_s;
    case ErrorCode::OutOfRange: return u"Value out of range"_s;
    case ErrorCode::DeviceUnavailable: return u"Audio or MIDI device unavailable"_s;
    }
    return u"Unknown error"_s;
}

} // namespace gigchain::core
