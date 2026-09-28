#include "PluginIcons.h"

#include <QtTest>

using namespace gigchain;
using namespace gigchain::ui;
using namespace Qt::StringLiterals;

namespace {

engine::PluginInfo instrument(const QString& name, const QString& sub)
{
    return engine::PluginInfo{.id = u"x"_s, .name = name, .vendor = u"Vendor"_s, .kind = engine::PluginKind::Instrument,
                              .subCategories = sub, .version = u"1.0"_s, .classId = {}, .website = {}, .email = {},
                              .sdkVersion = {}};
}

engine::PluginInfo effect(const QString& name, const QString& sub)
{
    return engine::PluginInfo{.id = u"x"_s, .name = name, .vendor = u"Vendor"_s, .kind = engine::PluginKind::Effect,
                              .subCategories = sub, .version = u"1.0"_s, .classId = {}, .website = {}, .email = {},
                              .sdkVersion = {}};
}

} // namespace

class TestPluginIcons : public QObject
{
    Q_OBJECT

private slots:
    void subCategoryDecides()
    {
        QCOMPARE(iconFor(instrument(u"Anything"_s, u"Instrument|Piano"_s)), u"piano"_s);
        QCOMPARE(iconFor(instrument(u"Anything"_s, u"Instrument|Organ"_s)), u"keyboard"_s);
        QCOMPARE(iconFor(instrument(u"Anything"_s, u"Instrument|Drum"_s)), u"vinyl"_s);
        QCOMPARE(iconFor(instrument(u"Anything"_s, u"Instrument|Synth"_s)), u"wave-sine"_s);
        QCOMPARE(iconFor(effect(u"Anything"_s, u"Fx|EQ"_s)), u"adjustments-horizontal"_s);
    }

    void namesFillTheGaps()
    {
        // Real plugin names from this machine whose sub-category is generic.
        QCOMPARE(iconFor(instrument(u"Piano V2"_s, u"Instrument"_s)), u"piano"_s);
        QCOMPARE(iconFor(instrument(u"B-3 V2"_s, u"Instrument"_s)), u"keyboard"_s);
        QCOMPARE(iconFor(instrument(u"Stage-73 V2"_s, u"Instrument"_s)), u"keyboard"_s);
        QCOMPARE(iconFor(instrument(u"Solina V2"_s, u"Instrument"_s)), u"music"_s);
        QCOMPARE(iconFor(instrument(u"BBC Symphony Orchestra"_s, u"Instrument"_s)), u"music"_s);
        QCOMPARE(iconFor(instrument(u"Mini V3"_s, u"Instrument|Synth"_s)), u"wave-sine"_s);
        QCOMPARE(iconFor(effect(u"FabFilter Pro-R"_s, u"Fx"_s)), u"ripple"_s);
    }

    void specificNamesBeatGenericSynthCategory()
    {
        // Arturia tags every instrument "Synth" (seen on this machine).
        QCOMPARE(iconFor(instrument(u"Piano V2"_s, u"Instrument|Synth"_s)), u"piano"_s);
        QCOMPARE(iconFor(instrument(u"B-3 V2"_s, u"Instrument|Synth"_s)), u"keyboard"_s);
        QCOMPARE(iconFor(instrument(u"Jup-8 V4"_s, u"Instrument|Synth"_s)), u"wave-sine"_s);
    }

    void unknownInstrumentsGetAGenericIcon()
    {
        QCOMPARE(iconFor(instrument(u"Mystery Box"_s, u"Instrument"_s)), u"music"_s);
        QCOMPARE(iconFor(effect(u"Mystery Fx"_s, u"Fx"_s)), u"adjustments-horizontal"_s);
    }
};

QTEST_GUILESS_MAIN(TestPluginIcons)
#include "tst_plugin_icons.moc"
