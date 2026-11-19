#pragma once

#include "gigchain/core/Error.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <vector>

namespace gigchain::devcontrol {

// The live test control's wire format (docs/testing/live-test-control.md):
// one JSON object per line each way. Pure functions, no event loop.

// The longest request line taken; a longer one is refused and its connection closed.
inline constexpr qsizetype kMaxLine = qsizetype{64} * 1024;

struct DevRequest
{
    QString cmd;
    QJsonArray args;
    QString line; // as it came, for the log
};

// `{"cmd":"note","args":[60,100]}`; `args` may be left out. Each problem says what is wrong.
[[nodiscard]] core::Result<DevRequest> parseRequest(QByteArrayView line);

// A request's argument `at` as text (a number as its digits); empty when there is none.
[[nodiscard]] QString argText(const DevRequest& request, qsizetype at);

// The answers: `{"ok":true,...fields}` / `{"ok":false,"error":why}`, one line each, ending in '\n'.
[[nodiscard]] QByteArray okLine(const QJsonObject& fields = {});
[[nodiscard]] QByteArray errorLine(const QString& why);

// A timed MIDI sequence: `on 60 100; wait 50; off 60`. Steps: on <key> <velocity>,
// off <key>, cc <number> <value>, bend <-8192..8191>, pc <program>, wait <ms>.
struct PlayStep
{
    enum Kind { Message, Wait };
    Kind kind = Message;
    QByteArray bytes; // a Message's MIDI bytes
    int ms = 0;       // a Wait's length
};
// `channel` 1-16 goes on every message. An error names the step (counted from 1).
[[nodiscard]] core::Result<std::vector<PlayStep>> parsePlayScript(const QString& script, int channel);

// One MIDI message as hex bytes, "90 3C 64": a status byte first, then up to two
// data bytes; or a whole sysex message, F0 .. F7.
[[nodiscard]] core::Result<QByteArray> parseHexMidi(const QString& hex);

} // namespace gigchain::devcontrol
