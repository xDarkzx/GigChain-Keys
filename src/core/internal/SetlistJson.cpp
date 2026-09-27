#include "gigchain/core/SetlistJson.h"

#include "gigchain/core/Branding.h"
#include "gigchain/core/Chart.h"

#include "gigchain/core/Limits.h"
#include "gigchain/core/Validation.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>
#include <limits>
#include <optional>

using namespace Qt::StringLiterals;

namespace gigchain::core {
namespace {

// Reads typed fields from JSON and remembers only the first error. After an
// error every read is a no-op returning a default, so callers read all the
// fields of an object and check failed() once.
class JsonReader
{
public:
    [[nodiscard]] bool failed() const { return m_error.has_value(); }
    // For values checked outside the reader (the first error is kept).
    void invalid(QString message) { setError(ErrorCode::InvalidData, std::move(message)); }
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
        const double value = checkedNumber(obj, key, path, min, max, true);
        return failed() ? 0 : static_cast<int>(value);
    }

    double number(const QJsonObject& obj, QLatin1StringView key, const QString& path, double min, double max)
    {
        const double value = checkedNumber(obj, key, path, min, max, false);
        return failed() ? 0.0 : value;
    }

    // Text that may be absent (fields added after format 1 shipped).
    QString optionalString(const QJsonObject& obj, QLatin1StringView key, const QString& path, qsizetype maxLength)
    {
        if (failed() || !obj.contains(key)) return {};
        return string(obj, key, path, maxLength);
    }

    // A list that may be absent (fields added after format 1 shipped).
    QJsonArray optionalArray(const QJsonObject& obj, QLatin1StringView key, const QString& path, qsizetype maxCount)
    {
        if (failed() || !obj.contains(key)) return {};
        return array(obj, key, path, maxCount);
    }

    // A whole number that may be absent (fields added after format 1 shipped).
    int optionalInteger(const QJsonObject& obj, QLatin1StringView key, const QString& path, int min, int max, int fallback)
    {
        if (failed() || !obj.contains(key)) return fallback;
        return integer(obj, key, path, min, max);
    }

    // A number that may be absent (fields added after format 1 shipped).
    double optionalNumber(const QJsonObject& obj, QLatin1StringView key, const QString& path, double min, double max,
                          double fallback)
    {
        if (failed() || !obj.contains(key)) return fallback;
        return number(obj, key, path, min, max);
    }

    // Binary data stored as base64 text, absent when empty (added in format 2).
    QByteArray optionalBytes(const QJsonObject& obj, QLatin1StringView key, const QString& path, qsizetype maxBytes)
    {
        if (failed() || !obj.contains(key)) return {};
        const QString where = path + u'.' + key;
        const QJsonValue value = obj.value(key);
        if (!value.isString()) {
            setError(ErrorCode::InvalidData, u"%1 must be text"_s.arg(where));
            return {};
        }
        const QString text = value.toString();
        if (text.size() > (maxBytes / 3 + 1) * 4) {
            setError(ErrorCode::LimitExceeded, u"%1 is larger than %2 bytes"_s.arg(where).arg(maxBytes));
            return {};
        }
        auto decoded = QByteArray::fromBase64Encoding(text.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) {
            setError(ErrorCode::InvalidData, u"%1 is damaged (not base64)"_s.arg(where));
            return {};
        }
        return decoded.decoded;
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
        const double found = value->toDouble();
        if (!std::isfinite(found) || (wholeNumber && std::trunc(found) != found)) {
            setError(ErrorCode::InvalidData, u"%1 must be a whole number"_s.arg(where));
            return 0.0;
        }
        if (found < min || found > max) {
            setError(ErrorCode::OutOfRange, u"%1 must be between %2 and %3"_s.arg(where).arg(min).arg(max));
            return 0.0;
        }
        return found;
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
    slot.state = r.optionalBytes(obj, "state"_L1, path, limits::kMaxPluginStateBytes);
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
    channel.pan = r.optionalNumber(obj, "pan"_L1, path, limits::kMinPan, limits::kMaxPan, 0.0);
    channel.mute = r.boolean(obj, "mute"_L1, path);
    channel.solo = r.boolean(obj, "solo"_L1, path);
    channel.keyLow = r.integer(obj, "keyLow"_L1, path, limits::kMinMidiNote, limits::kMaxMidiNote);
    channel.keyHigh = r.integer(obj, "keyHigh"_L1, path, limits::kMinMidiNote, limits::kMaxMidiNote);
    channel.transpose = r.integer(obj, "transpose"_L1, path, limits::kMinTranspose, limits::kMaxTranspose);
    channel.midiChannel = r.integer(obj, "midiChannel"_L1, path, limits::kMinMidiChannel, limits::kMaxMidiChannel);
    // Added in format 3.
    channel.velocityLow = r.optionalInteger(obj, "velocityLow"_L1, path, limits::kMinVelocity, limits::kMaxVelocity,
                                            limits::kMinVelocity);
    channel.velocityHigh = r.optionalInteger(obj, "velocityHigh"_L1, path, limits::kMinVelocity, limits::kMaxVelocity,
                                             limits::kMaxVelocity);
    channel.inputLeft = r.optionalInteger(obj, "inputLeft"_L1, path, 0, limits::kMaxAudioInput, 0);
    channel.inputRight = r.optionalInteger(obj, "inputRight"_L1, path, 0, limits::kMaxAudioInput, 0);
    const QJsonArray mappings = r.optionalArray(obj, "mappings"_L1, path, limits::kMaxMappingsPerChannel);
    for (qsizetype i = 0; i < mappings.size() && !r.failed(); ++i) {
        const QString at = u"%1.mappings[%2]"_s.arg(path).arg(i);
        const QJsonObject m = r.object(mappings.at(i), at);
        channel.mappings.push_back(ControlMapping{
            .midiChannel = r.integer(m, "midiChannel"_L1, at, limits::kMinMidiChannel, limits::kMaxMidiChannel),
            .controller = r.integer(m, "controller"_L1, at, 0, limits::kMaxController),
            .target = r.integer(m, "target"_L1, at, -1, limits::kMaxEffectsPerChannel - 1),
            .parameter = static_cast<quint32>(r.number(m, "parameter"_L1, at, 0.0, 4294967295.0)),
            .parameterName = r.string(m, "parameterName"_L1, at, limits::kMaxNameLength),
            .minimum = r.number(m, "minimum"_L1, at, 0.0, 1.0),
            .maximum = r.number(m, "maximum"_L1, at, 0.0, 1.0)});
    }
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
    // Added in format 2; absent in format 1 files.
    song.chart = r.optionalString(obj, "chart"_L1, path, limits::kMaxChartLength);
    song.key = r.optionalString(obj, "key"_L1, path, limits::kMaxKeyLength);
    song.tempo = r.optionalNumber(obj, "tempo"_L1, path, 0.0, limits::kMaxTempo, 0.0);
    song.notes = r.optionalString(obj, "notes"_L1, path, limits::kMaxNotesLength);
    const QJsonArray links = r.optionalArray(obj, "links"_L1, path, limits::kMaxLinksPerSong);
    for (qsizetype i = 0; i < links.size() && !r.failed(); ++i) {
        const QString linkPath = u"%1.links[%2]"_s.arg(path).arg(i);
        const QJsonObject link = r.object(links.at(i), linkPath);
        song.links.push_back(SongLink{.title = r.string(link, "title"_L1, linkPath, limits::kMaxNameLength),
                                      .url = r.string(link, "url"_L1, linkPath, limits::kMaxUrlLength)});
    }
    const QJsonArray attachments = r.optionalArray(obj, "attachments"_L1, path, limits::kMaxAttachmentsPerSong);
    for (qsizetype i = 0; i < attachments.size() && !r.failed(); ++i) {
        const QString where = u"%1.attachments[%2]"_s.arg(path).arg(i);
        if (!attachments.at(i).isString()) {
            (void)r.string(QJsonObject{{u"x"_s, attachments.at(i)}}, "x"_L1, where, 0); // reports "must be text"
            break;
        }
        song.attachments.push_back(attachments.at(i).toString());
    }
    song.backingTrack = r.optionalString(obj, "backingTrack"_L1, path, limits::kMaxFileNameLength); // format 3

    // Format 4.
    if (const QString time = r.optionalString(obj, "timeSignature"_L1, path, 8); !time.isEmpty()) {
        const QStringList parts = time.split(u'/');
        bool numberOk = false;
        bool noteOk = false;
        const int numerator = parts.size() == 2 ? parts.at(0).toInt(&numberOk) : 0;
        const int denominator = parts.size() == 2 ? parts.at(1).toInt(&noteOk) : 0;
        if (!numberOk || !noteOk || !isTimeSignature(numerator, denominator)) {
            r.invalid(u"%1.timeSignature \"%2\" is not a time signature like 4/4 or 6/8"_s.arg(path, time));
            return song;
        }
        song.timeNumerator = numerator;
        song.timeDenominator = denominator;
    }
    song.switchEarly = obj.contains("switchEarly"_L1) && r.boolean(obj, "switchEarly"_L1, path);
    song.loopSync = !obj.contains("loopSync"_L1) || r.boolean(obj, "loopSync"_L1, path);
    const QJsonArray sections = r.optionalArray(obj, "sections"_L1, path, limits::kMaxSectionsPerSong);
    for (qsizetype i = 0; i < sections.size() && !r.failed(); ++i) {
        const QString where = u"%1.sections[%2]"_s.arg(path).arg(i);
        const QJsonObject item = r.object(sections.at(i), where);
        SectionSetup section;
        section.name = r.string(item, "name"_L1, where, limits::kMaxNameLength);
        section.occurrence = r.integer(item, "occurrence"_L1, where, 1, limits::kMaxSectionOccurrence);
        section.bars = r.integer(item, "bars"_L1, where, 0, limits::kMaxSectionBars);
        section.assigned = r.boolean(item, "assigned"_L1, where);
        const QJsonArray channels = r.array(item, "channels"_L1, where, limits::kMaxChannelsPerPatch);
        for (qsizetype c = 0; c < channels.size() && !r.failed(); ++c) {
            const QString channelPath = u"%1.channels[%2]"_s.arg(where).arg(c);
            if (!channels.at(c).isString()) {
                r.invalid(u"%1 must be text"_s.arg(channelPath));
                break;
            }
            section.channels.emplace_back(channels.at(c).toString().left(limits::kMaxIdLength + 1));
        }
        song.sections.push_back(section);
    }
    return song;
}

QJsonObject writeSlot(const PluginSlot& slot)
{
    QJsonObject obj{
        {u"pluginId"_s, slot.pluginId},
        {u"displayName"_s, slot.displayName},
        {u"bypass"_s, slot.bypass},
    };
    if (!slot.state.isEmpty()) obj.insert(u"state"_s, QString::fromLatin1(slot.state.toBase64()));
    return obj;
}

QJsonObject writeChannel(const Channel& channel)
{
    QJsonArray effects;
    for (const PluginSlot& effect : channel.effects) {
        effects.append(writeSlot(effect));
    }
    QJsonArray mappings;
    for (const ControlMapping& m : channel.mappings) {
        mappings.append(QJsonObject{{u"midiChannel"_s, m.midiChannel},
                                    {u"controller"_s, m.controller},
                                    {u"target"_s, m.target},
                                    {u"parameter"_s, static_cast<double>(m.parameter)},
                                    {u"parameterName"_s, m.parameterName},
                                    {u"minimum"_s, m.minimum},
                                    {u"maximum"_s, m.maximum}});
    }
    return QJsonObject{
        {u"id"_s, channel.id.value()},
        {u"name"_s, channel.name},
        {u"instrument"_s, channel.instrument ? QJsonValue(writeSlot(*channel.instrument)) : QJsonValue(QJsonValue::Null)},
        {u"effects"_s, effects},
        {u"volumeDb"_s, channel.volumeDb},
        {u"pan"_s, channel.pan},
        {u"mute"_s, channel.mute},
        {u"solo"_s, channel.solo},
        {u"keyLow"_s, channel.keyLow},
        {u"keyHigh"_s, channel.keyHigh},
        {u"transpose"_s, channel.transpose},
        {u"midiChannel"_s, channel.midiChannel},
        {u"velocityLow"_s, channel.velocityLow},
        {u"velocityHigh"_s, channel.velocityHigh},
        {u"inputLeft"_s, channel.inputLeft},
        {u"inputRight"_s, channel.inputRight},
        {u"mappings"_s, mappings},
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
    QJsonArray links;
    for (const SongLink& link : song.links) links.append(QJsonObject{{u"title"_s, link.title}, {u"url"_s, link.url}});
    QJsonArray attachments;
    for (const QString& name : song.attachments) attachments.append(name);
    QJsonArray sections;
    for (const SectionSetup& section : song.sections) {
        QJsonArray channels;
        for (const ChannelId& id : section.channels) channels.append(id.value());
        sections.append(QJsonObject{{u"name"_s, section.name},
                                    {u"occurrence"_s, section.occurrence},
                                    {u"bars"_s, section.bars},
                                    {u"assigned"_s, section.assigned},
                                    {u"channels"_s, channels}});
    }
    return QJsonObject{{u"id"_s, song.id.value()},
                       {u"name"_s, song.name},
                       {u"patches"_s, patches},
                       {u"chart"_s, song.chart},
                       {u"key"_s, song.key},
                       {u"tempo"_s, song.tempo},
                       {u"notes"_s, song.notes},
                       {u"links"_s, links},
                       {u"attachments"_s, attachments},
                       {u"backingTrack"_s, song.backingTrack},
                       {u"timeSignature"_s, u"%1/%2"_s.arg(song.timeNumerator).arg(song.timeDenominator)},
                       {u"switchEarly"_s, song.switchEarly},
                       {u"loopSync"_s, song.loopSync},
                       {u"sections"_s, sections}};
}

} // namespace

QByteArray toJson(const Setlist& setlist)
{
    QJsonArray songs;
    for (const Song& song : setlist.songs) {
        songs.append(writeSong(song));
    }
    const auto control = [](const LearnedControl& c) {
        return QJsonObject{{u"kind"_s, c.kind}, {u"channel"_s, c.channel}, {u"number"_s, c.number}};
    };
    QJsonArray buttons;
    for (const LearnedControl& button : setlist.loopControls.buttons) buttons.append(control(button));
    const QJsonObject loopControls{{u"buttons"_s, buttons},
                                   {u"selector"_s, control(setlist.loopControls.selector)},
                                   {u"selectorMode"_s, setlist.loopControls.selectorMode}};
    const QJsonObject root{{u"formatVersion"_s, kSetlistFormatVersion}, {u"songs"_s, songs}, {u"loopControls"_s, loopControls}};
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
    if (version > kSetlistFormatVersion) { // older formats still open
        return fail(ErrorCode::UnsupportedVersion,
                    u"This setlist uses file format %1, but this version of %2 reads format %3"_s.arg(version).arg(branding::name())
                        .arg(kSetlistFormatVersion));
    }

    Setlist setlist;
    const QJsonArray songs = reader.array(root, "songs"_L1, rootPath, limits::kMaxSongs);
    for (qsizetype i = 0; i < songs.size() && !reader.failed(); ++i) {
        const QString songPath = u"songs[%1]"_s.arg(i);
        setlist.songs.push_back(readSong(reader, reader.object(songs.at(i), songPath), songPath));
    }
    if (reader.failed()) return tl::unexpected(reader.error());
    // Format 4: the looper's keyboard controls (absent: none learned).
    if (root.contains("loopControls"_L1)) {
        const QString path = rootPath + u".loopControls"_s;
        const QJsonObject controls = reader.object(root.value("loopControls"_L1), path);
        const auto control = [&reader](const QJsonObject& obj, const QString& where) {
            return LearnedControl{.kind = reader.integer(obj, "kind"_L1, where, 0, 0xC0),
                                  .channel = reader.integer(obj, "channel"_L1, where, 0, 16),
                                  .number = reader.integer(obj, "number"_L1, where, 0, 127)};
        };
        const QJsonArray buttons = reader.array(controls, "buttons"_L1, path, LoopControls::ButtonCount);
        for (qsizetype i = 0; i < buttons.size() && !reader.failed(); ++i) {
            const QString where = u"%1.buttons[%2]"_s.arg(path).arg(i);
            setlist.loopControls.buttons.at(static_cast<std::size_t>(i)) = control(reader.object(buttons.at(i), where), where);
        }
        const QString selectorPath = path + u".selector"_s;
        setlist.loopControls.selector = control(reader.object(controls.value("selector"_L1), selectorPath), selectorPath);
        setlist.loopControls.selectorMode = reader.integer(controls, "selectorMode"_L1, path, 0, LoopControls::RelativeOffset);
        if (reader.failed()) return tl::unexpected(reader.error());
    }

    if (auto valid = validate(setlist); !valid) return tl::unexpected(valid.error());
    return setlist;
}

} // namespace gigchain::core
