#include "openstage/core/Limits.h"
#include "openstage/core/Model.h"
#include "openstage/core/Validation.h"

#include <QtTest>

#include <cmath>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

using namespace openstage::core;

namespace {

Setlist sampleSetlist()
{
    Setlist setlist;
    Song song = makeSong(QStringLiteral("Wonderwall"));
    Channel piano = makeChannel(QStringLiteral("Piano"));
    piano.instrument = PluginSlot{QStringLiteral("fake.grand-piano"), QStringLiteral("Grand Piano"), false};
    piano.effects.push_back(PluginSlot{QStringLiteral("fake.reverb"), QStringLiteral("Reverb"), false});
    song.patches.front().channels.push_back(piano);
    setlist.songs.push_back(song);
    return setlist;
}

Channel& firstChannel(Setlist& s) { return s.songs.front().patches.front().channels.front(); }

} // namespace

class TestValidation : public QObject
{
    Q_OBJECT

private slots:
    void factoriesProduceValidObjects()
    {
        const Setlist setlist = sampleSetlist();
        QVERIFY(validate(setlist).has_value());
        QCOMPARE(setlist.songs.front().patches.size(), std::size_t{1});
        QCOMPARE(setlist.songs.front().patches.front().name, QStringLiteral("Patch 1"));
        QVERIFY(!setlist.songs.front().id.isNull());
    }

    void generatedIdsAreUnique()
    {
        QVERIFY(ChannelId::generate() != ChannelId::generate());
    }

    void freshIdsReplaceEveryId()
    {
        const Song original = sampleSetlist().songs.front();
        const Song copy = withFreshIds(original);
        QVERIFY(copy.id != original.id);
        QVERIFY(copy.patches.front().id != original.patches.front().id);
        QVERIFY(copy.patches.front().channels.front().id != original.patches.front().channels.front().id);
        QCOMPARE(copy.patches.front().channels.front().name, QStringLiteral("Piano"));
    }

    void rejectsInvalidSetlists()
    {
        using Mutation = std::function<void(Setlist&)>;
        const std::vector<std::pair<const char*, std::pair<ErrorCode, Mutation>>> cases = {
            {"keyLow above 127", {ErrorCode::OutOfRange, [](Setlist& s) { firstChannel(s).keyLow = 128; }}},
            {"keyHigh negative", {ErrorCode::OutOfRange, [](Setlist& s) { firstChannel(s).keyHigh = -1; }}},
            {"keyLow above keyHigh", {ErrorCode::OutOfRange, [](Setlist& s) {
                 firstChannel(s).keyLow = 80;
                 firstChannel(s).keyHigh = 20;
             }}},
            {"transpose too far", {ErrorCode::OutOfRange, [](Setlist& s) { firstChannel(s).transpose = 49; }}},
            {"midi channel 17", {ErrorCode::OutOfRange, [](Setlist& s) { firstChannel(s).midiChannel = 17; }}},
            {"volume NaN", {ErrorCode::OutOfRange, [](Setlist& s) {
                 firstChannel(s).volumeDb = std::numeric_limits<double>::quiet_NaN();
             }}},
            {"volume too loud", {ErrorCode::OutOfRange, [](Setlist& s) { firstChannel(s).volumeDb = 12.5; }}},
            {"empty song name", {ErrorCode::InvalidData, [](Setlist& s) { s.songs.front().name = QStringLiteral("  "); }}},
            {"name too long", {ErrorCode::LimitExceeded, [](Setlist& s) {
                 s.songs.front().name = QString(limits::kMaxNameLength + 1, QLatin1Char('x'));
             }}},
            {"empty plugin id", {ErrorCode::InvalidData, [](Setlist& s) { firstChannel(s).effects.front().pluginId.clear(); }}},
            {"too many effects", {ErrorCode::LimitExceeded, [](Setlist& s) {
                 auto& effects = firstChannel(s).effects;
                 const PluginSlot slot = effects.front();
                 effects.resize(limits::kMaxEffectsPerChannel + 1, slot);
             }}},
            {"song without patches", {ErrorCode::InvalidData, [](Setlist& s) { s.songs.front().patches.clear(); }}},
            {"duplicate ids", {ErrorCode::InvalidData, [](Setlist& s) {
                 Song copy = s.songs.front();
                 s.songs.push_back(copy);
             }}},
            {"empty id", {ErrorCode::InvalidData, [](Setlist& s) { firstChannel(s).id = ChannelId(); }}},
            {"too many songs", {ErrorCode::LimitExceeded, [](Setlist& s) {
                 for (int i = 0; i < limits::kMaxSongs; ++i) {
                     s.songs.push_back(makeSong(QStringLiteral("Song")));
                 }
             }}},
        };

        for (const auto& [label, expectation] : cases) {
            Setlist setlist = sampleSetlist();
            expectation.second(setlist);
            const auto result = validate(setlist);
            QVERIFY2(!result.has_value(), label);
            QVERIFY2(result.error().code == expectation.first, label);
            QVERIFY2(!result.error().message.isEmpty(), label);
        }
    }
};

QTEST_GUILESS_MAIN(TestValidation)
#include "tst_validation.moc"
