#include "DevProtocol.h"

#include "gigchain/core/Branding.h"

#include <QJsonDocument>
#include <QStringList>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace gigchain::devcontrol {
namespace {

core::Error problem(const QString& message)
{
    return core::Error{core::ErrorCode::ParseFailed, message};
}

// A whole number in [low, high], or nothing.
std::optional<int> number(const QString& text, int low, int high)
{
    bool ok = false;
    const int value = text.toInt(&ok);
    if (!ok || value < low || value > high) return std::nullopt;
    return value;
}

QByteArray message(uint8_t status, std::initializer_list<int> data)
{
    QByteArray bytes(1, static_cast<char>(status));
    for (const int byte : data) bytes.append(static_cast<char>(byte));
    return bytes;
}

} // namespace

QString socketName()
{
    return branding::organization() + u'-' + branding::executable() + u"-dev-control"_s; // (as SingleInstance names the app)
}

core::Result<DevRequest> parseRequest(QByteArrayView line)
{
    if (line.size() > kMaxLine) {
        return tl::unexpected(problem(u"The request is too long (%1 bytes; at most %2)"_s.arg(line.size()).arg(kMaxLine)));
    }
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(line.toByteArray(), &error);
    if (error.error != QJsonParseError::NoError) {
        return tl::unexpected(problem(u"The request is not JSON: %1 at %2"_s.arg(error.errorString()).arg(error.offset)));
    }
    if (!doc.isObject()) return tl::unexpected(problem(u"The request must be a JSON object, as {\"cmd\": \"status\"}"_s));
    const QJsonObject object = doc.object();
    if (!object.value(u"cmd"_s).isString() || object.value(u"cmd"_s).toString().isEmpty()) {
        return tl::unexpected(problem(u"The request names no command: \"cmd\" must be a word, as {\"cmd\": \"status\"}"_s));
    }
    const QJsonValue args = object.value(u"args"_s);
    if (!args.isUndefined() && !args.isArray()) {
        return tl::unexpected(problem(u"The request's \"args\" must be a list, as [60, 100]"_s));
    }
    return DevRequest{.cmd = object.value(u"cmd"_s).toString(), .args = args.toArray(), .line = QString::fromUtf8(line.toByteArray())};
}

QString argText(const DevRequest& request, qsizetype at)
{
    if (at < 0 || at >= request.args.size()) return {};
    const QJsonValue value = request.args.at(at);
    return value.isString() ? value.toString() : value.toVariant().toString();
}

QByteArray okLine(const QJsonObject& fields)
{
    QJsonObject answer = fields;
    answer.insert(u"ok"_s, true);
    return QJsonDocument(answer).toJson(QJsonDocument::Compact) + '\n'; // (newlines inside strings are escaped)
}

QByteArray errorLine(const QString& why)
{
    return QJsonDocument(QJsonObject{{u"ok"_s, false}, {u"error"_s, why}}).toJson(QJsonDocument::Compact) + '\n';
}

core::Result<std::vector<PlayStep>> parsePlayScript(const QString& script, int channel)
{
    if (channel < 1 || channel > 16) return tl::unexpected(problem(u"MIDI channel %1 is not 1-16"_s.arg(channel)));
    const QStringList parts = script.split(u';', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return tl::unexpected(problem(u"The script is empty: say what to play, as \"on 60 100; wait 50; off 60\""_s));
    const auto ch = static_cast<uint8_t>(channel - 1);
    std::vector<PlayStep> steps;
    for (qsizetype i = 0; i < parts.size(); ++i) {
        const QStringList words = parts.at(i).split(u' ', Qt::SkipEmptyParts);
        const auto wrong = [&](const QString& why) {
            return tl::unexpected(problem(u"In step %1 (\"%2\"): %3"_s.arg(i + 1).arg(parts.at(i).trimmed(), why)));
        };
        const QString kind = words.value(0).toLower();
        const auto arg = [&words](int at, int low, int high) { return number(words.value(at), low, high); };
        if (kind == u"wait"_s && words.size() == 2) {
            const auto ms = arg(1, 0, 60'000);
            if (!ms) return wrong(u"wait takes 0-60000 ms"_s);
            steps.push_back(PlayStep{.kind = PlayStep::Wait, .bytes = {}, .ms = *ms});
        } else if (kind == u"on"_s && words.size() == 3) {
            const auto key = arg(1, 0, 127);
            const auto velocity = arg(2, 1, 127);
            if (!key || !velocity) return wrong(u"on takes a key 0-127 and a velocity 1-127"_s);
            steps.push_back(PlayStep{.kind = PlayStep::Message, .bytes = message(0x90 | ch, {*key, *velocity}), .ms = 0});
        } else if (kind == u"off"_s && words.size() == 2) {
            const auto key = arg(1, 0, 127);
            if (!key) return wrong(u"off takes a key 0-127"_s);
            steps.push_back(PlayStep{.kind = PlayStep::Message, .bytes = message(0x80 | ch, {*key, 0}), .ms = 0});
        } else if (kind == u"cc"_s && words.size() == 3) {
            const auto controller = arg(1, 0, 127);
            const auto value = arg(2, 0, 127);
            if (!controller || !value) return wrong(u"cc takes a controller 0-127 and a value 0-127"_s);
            steps.push_back(PlayStep{.kind = PlayStep::Message, .bytes = message(0xB0 | ch, {*controller, *value}), .ms = 0});
        } else if (kind == u"bend"_s && words.size() == 2) {
            const auto bend = arg(1, -8192, 8191);
            if (!bend) return wrong(u"bend takes -8192 to 8191"_s);
            const int raw = *bend + 8192;
            steps.push_back(PlayStep{.kind = PlayStep::Message, .bytes = message(0xE0 | ch, {raw & 0x7F, raw >> 7}), .ms = 0});
        } else if (kind == u"pc"_s && words.size() == 2) {
            const auto program = arg(1, 0, 127);
            if (!program) return wrong(u"pc takes a program 0-127"_s);
            steps.push_back(PlayStep{.kind = PlayStep::Message, .bytes = message(0xC0 | ch, {*program}), .ms = 0});
        } else {
            return wrong(u"not a step: on <key> <velocity>, off <key>, cc <n> <value>, bend <amount>, pc <program>, wait <ms>"_s);
        }
    }
    return steps;
}

core::Result<QByteArray> parseHexMidi(const QString& hex)
{
    const QStringList words = hex.split(u' ', Qt::SkipEmptyParts);
    if (words.isEmpty()) return tl::unexpected(problem(u"No MIDI bytes given, as \"90 3C 64\""_s));
    QByteArray bytes;
    for (const QString& word : words) {
        bool ok = false;
        const int value = word.toInt(&ok, 16);
        if (!ok || value < 0 || value > 0xFF || word.size() > 2) {
            return tl::unexpected(problem(u"\"%1\" is not a hex byte (00-FF)"_s.arg(word)));
        }
        bytes.append(static_cast<char>(value));
    }
    const auto first = static_cast<uint8_t>(bytes.front());
    if (first < 0x80) return tl::unexpected(problem(u"A MIDI message starts with a status byte (80-FF), not %1"_s.arg(words.front())));
    if (first == 0xF0) {
        if (static_cast<uint8_t>(bytes.back()) != 0xF7) return tl::unexpected(problem(u"A sysex message ends with F7"_s));
        return bytes;
    }
    if (bytes.size() > 3) return tl::unexpected(problem(u"One message has at most 3 bytes (sysex aside): %1 given"_s.arg(bytes.size())));
    if (std::any_of(bytes.begin() + 1, bytes.end(), [](char b) { return static_cast<uint8_t>(b) >= 0x80; })) {
        return tl::unexpected(problem(u"Data bytes are 00-7F"_s));
    }
    return bytes;
}

} // namespace gigchain::devcontrol
