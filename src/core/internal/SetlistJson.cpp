#include "openstage/core/SetlistJson.h"

#include "openstage/core/Limits.h"
#include "openstage/core/Validation.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>
#include <limits>
#include <optional>

using namespace Qt::StringLiterals;

namespace openstage::core {
namespace {

// Reads typed fields from JSON and remembers only the first error. After an
// error every read is a no-op returning a default, so callers read all the
// fields of an object and check failed() once.
class JsonReader
{
public:
    [[nodiscard]] bool failed() const { return m_error.has_value(); }
    [[nodiscard]] const Error& error() const { return *m_error; }

    QString string(const QJsonObject& obj, QLatin1StringView key, const QString& path, qsizetype maxLength)
    {
        const auto value = get(obj, key, path);
        if (!value) return {};
        const QString where = path + u'.' + key;
        if (!value->isString()) {
            setError(ErrorCode::InvalidData, u"%1 must be text"_s.arg(where));
            return {};
        }
        QString text = value->toString();
        if (text.size() > maxLength) {
            setError(ErrorCode::LimitExceeded, u"%1 is longer than %2 characters"_s.arg(where).arg(maxLength));
            return {};
        }
        return text;
    }

    int integer(const QJsonObject& obj, QLatin1StringView key, const QString& path, int min, int max)
    {
        const double number = checkedNumber(obj, key, path, min, max, true);
        return failed() ? 0 : static_cast<int>(number);
    }

    double number(const QJsonObject& obj, QLatin1StringView key, const QString& path, double min, double max)
    {
        const double number = checkedNumber(obj, key, path, min, max, false);
        return failed() ? 0.0 : number;
    }

    bool boolean(const QJsonObject& obj, QLatin1StringView key, const QString& path)
    {
        const auto value = get(obj, key, path);
        if (!value) return false;
        if (!value->isBool()) {
            setError(ErrorCode::InvalidData, u"%1 must be true or false"_s.arg(path + u'.' + key));
            return false;
        }
        return value->toBool();
    }

    QJsonArray array(const QJsonObject& obj, QLatin1StringView key, const QString& path, qsizetype maxCount)
    {
        const auto value = get(obj, key, path);
        if (!value) return {};
        const QString where = path + u'.' + key;
        if (!value->isArray()) {
            setError(ErrorCode::InvalidData, u"%1 must be a list"_s.arg(where));
            return {};
        }
        QJsonArray items = value->toArray();
        if (items.size() > maxCount) {
            setError(ErrorCode::LimitExceeded, u"%1 has more than %2 entries"_s.arg(where).arg(maxCount));
            return {};
        }
        return items;
    }

    QJsonObject object(const QJsonValue& value, const QString& path)
    {
        if (failed()) return {};
        if (!value.isObject()) {
            setError(ErrorCode::InvalidData, u"%1 must be an object"_s.arg(path));
            return {};
        }
        return value.toObject();
    }

    // A key that must be present and may be null.
    std::optional<QJsonObject> nullableObject(const QJsonObject& obj, QLatin1StringView key, const QString& path)
    {
        const auto value = get(obj, key, path);
        if (!value || value->isNull()) return std::nullopt;
        QJsonObject result = object(*value, path + u'.' + key);
        if (failed()) return std::nullopt;
        return result;
    }

private:
    std::optional<QJsonValue> get(const QJsonObject& obj, QLatin1StringView key, const QString& path)
    {
        if (failed()) return std::nullopt;
        const auto it = obj.constFind(key);
        if (it == obj.constEnd()) {
            setError(ErrorCode::InvalidData, u"%1 is missing \"%2\""_s.arg(path, QString(key)));
            return std::nullopt;
        }
        return QJsonValue(*it);
    }

    double checkedNumber(const QJsonObject& obj, QLatin1StringView key, const QString& path, double min,
                         double max, bool wholeNumber)
    {
        const auto value = get(obj, key, path);
        if (!value) return 0.0;
        const QString where = path + u'.' + key;
        if (!value->isDouble()) {
            setError(ErrorCode::InvalidData, u"%1 must be a number"_s.arg(where));
            return 0.0;
        }
        const double number = value->toDouble();
        if (!std::isfinite(number) || (wholeNumber && std::trunc(number) != number)) {
            setError(ErrorCode::InvalidData, u"%1 must be a whole number"_s.arg(where));
            return 0.0;
        }
        if (number < min || number > max) {
            setError(ErrorCode::OutOfRange, u"%1 must be between %2 and %3"_s.arg(where).arg(min).arg(max));
            return 0.0;
        }
        return number;
    }

    void setError(ErrorCode code, QString message)
    {
        if (!m_error) m_error = Error{code, std::move(message)};
    }

    std::optional<Error> m_error;
};

PluginSlot readSlot(JsonReader& r, const QJsonObject& obj, const QString& path)
{
    PluginSlot slot;
    slot.pluginId = r.string(obj, "pluginId"_L1, path, limits::kMaxPluginIdLength);
    slot.displayName = r.string(obj, "displayName"_L1, path, limits::kMaxNameLength);
    slot.bypass = r.boolean(obj, "bypass"_L1, path);
    return slot;
}

Channel readChannel(JsonReader& r, const QJsonObject& obj, const QString& path)
{
    Channel channel;
    channel.id = ChannelId(r.string(obj, "id"_L1, path, limits::kMaxIdLength));
    channel.name = r.string(obj, "name"_L1, path, limits::kMaxNameLength);
    if (const auto instrument = r.nullableObject(obj, "instrument"_L1, path)) {
        channel.instrument = readSlot(r, *instrument, path + ".instrument"_L1);
    }
    const QJsonArray effects = r.array(obj, "effects"_L1, path, limits::kMaxEffectsPerChannel);
    for (qsizetype i = 0; i < effects.size() && !r.failed(); ++i) {
        const QString effectPath = u"%1.effects[%2]"_s.arg(path).arg(i);
        channel.effects.push_back(readSlot(r, r.object(effects.at(i), effectPath), effectPath));
    }
    channel.volumeDb = r.number(obj, "volumeDb"_L1, path, limits::kMinVolumeDb, limits::kMaxVolumeDb);
    channel.mute = r.boolean(obj, "mute"_L1, path);
    channel.solo = r.boolean(obj, "solo"_L1, path);
    channel.keyLow = r.integer(obj, "keyLow"_L1, path, limits::kMinMidiNote, limits::kMaxMidiNote);
    channel.keyHigh = r.integer(obj, "keyHigh"_L1, path, limits::kMinMidiNote, limits::kMaxMidiNote);
    channel.transpose = r.integer(obj, "transpose"_L1, path, limits::kMinTranspose, limits::kMaxTranspose);
    channel.midiChannel = r.integer(obj, "midiChannel"_L1, path, limits::kMinMidiChannel, limits::kMaxMidiChannel);
    return channel;
}

Patch readPatch(JsonReader& r, const QJsonObject& obj, const QString& path)
{
    Patch patch;
    patch.id = PatchId(r.string(obj, "id"_L1, path, limits::kMaxIdLength));
    patch.name = r.string(obj, "name"_L1, path, limits::kMaxNameLength);
    const QJsonArray channels = r.array(obj, "channels"_L1, path, limits::kMaxChannelsPerPatch);
    for (qsizetype i = 0; i < channels.size() && !r.failed(); ++i) {
        const QString channelPath = u"%1.channels[%2]"_s.arg(path).arg(i);
        patch.channels.push_back(readChannel(r, r.object(channels.at(i), channelPath), channelPath));
    }
    return patch;
}

Song readSong(JsonReader& r, const QJsonObject& obj, const QString& path)
{
    Song song;
    song.id = SongId(r.string(obj, "id"_L1, path, limits::kMaxIdLength));
    song.name = r.string(obj, "name"_L1, path, limits::kMaxNameLength);
    const QJsonArray patches = r.array(obj, "patches"_L1, path, limits::kMaxPatchesPerSong);
    for (qsizetype i = 0; i < patches.size() && !r.failed(); ++i) {
        const QString patchPath = u"%1.patches[%2]"_s.arg(path).arg(i);
        song.patches.push_back(readPatch(r, r.object(patches.at(i), patchPath), patchPath));
    }
    return song;
}

QJsonObject writeSlot(const PluginSlot& slot)
{
    return QJsonObject{
        {u"pluginId"_s, slot.pluginId},
        {u"displayName"_s, slot.displayName},
        {u"bypass"_s, slot.bypass},
    };
}

QJsonObject writeChannel(const Channel& channel)
{
    QJsonArray effects;
    for (const PluginSlot& effect : channel.effects) {
        effects.append(writeSlot(effect));
    }
    return QJsonObject{
        {u"id"_s, channel.id.value()},
        {u"name"_s, channel.name},
        {u"instrument"_s, channel.instrument ? QJsonValue(writeSlot(*channel.instrument)) : QJsonValue(QJsonValue::Null)},
        {u"effects"_s, effects},
        {u"volumeDb"_s, channel.volumeDb},
        {u"mute"_s, channel.mute},
        {u"solo"_s, channel.solo},
        {u"keyLow"_s, channel.keyLow},
        {u"keyHigh"_s, channel.keyHigh},
        {u"transpose"_s, channel.transpose},
        {u"midiChannel"_s, channel.midiChannel},
    };
}

QJsonObject writePatch(const Patch& patch)
{
    QJsonArray channels;
    for (const Channel& channel : patch.channels) {
        channels.append(writeChannel(channel));
    }
    return QJsonObject{{u"id"_s, patch.id.value()}, {u"name"_s, patch.name}, {u"channels"_s, channels}};
}

QJsonObject writeSong(const Song& song)
{
    QJsonArray patches;
    for (const Patch& patch : song.patches) {
        patches.append(writePatch(patch));
    }
    return QJsonObject{{u"id"_s, song.id.value()}, {u"name"_s, song.name}, {u"patches"_s, patches}};
}

} // namespace

QByteArray toJson(const Setlist& setlist)
{
    QJsonArray songs;
    for (const Song& song : setlist.songs) {
        songs.append(writeSong(song));
    }
    const QJsonObject root{{u"formatVersion"_s, kSetlistFormatVersion}, {u"songs"_s, songs}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

Result<Setlist> fromJson(const QByteArray& bytes)
{
    if (bytes.size() > limits::kMaxFileBytes) {
        return fail(ErrorCode::FileTooLarge, u"Setlist is larger than %1 bytes"_s.arg(limits::kMaxFileBytes));
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return fail(ErrorCode::ParseFailed,
                    u"Not a valid setlist file: %1 (at byte %2)"_s.arg(parseError.errorString()).arg(parseError.offset));
    }
    if (!document.isObject()) {
        return fail(ErrorCode::ParseFailed, u"Not a valid setlist file: expected a JSON object"_s);
    }

    const QJsonObject root = document.object();
    const QString rootPath = u"setlist"_s;
    JsonReader reader;

    const int version = reader.integer(root, "formatVersion"_L1, rootPath, 1, std::numeric_limits<int>::max());
    if (reader.failed()) return tl::unexpected(reader.error());
    if (version != kSetlistFormatVersion) {
        return fail(ErrorCode::UnsupportedVersion,
                    u"This setlist uses file format %1, but this version of OpenStage reads format %2"_s.arg(version)
                        .arg(kSetlistFormatVersion));
    }

    Setlist setlist;
    const QJsonArray songs = reader.array(root, "songs"_L1, rootPath, limits::kMaxSongs);
    for (qsizetype i = 0; i < songs.size() && !reader.failed(); ++i) {
        const QString songPath = u"songs[%1]"_s.arg(i);
        setlist.songs.push_back(readSong(reader, reader.object(songs.at(i), songPath), songPath));
    }
    if (reader.failed()) return tl::unexpected(reader.error());

    if (auto valid = validate(setlist); !valid) return tl::unexpected(valid.error());
    return setlist;
}

} // namespace openstage::core
