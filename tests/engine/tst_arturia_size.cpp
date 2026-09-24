// Arturia's own window-size setting (the plugins refuse host zoom and resize).
#include "ArturiaWindowSize.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace openstage::engine;
using namespace Qt::StringLiterals;

namespace {

const QByteArray kPrefs = R"(<?xml version="1.0" encoding="utf-8"?>
<rootnode>
	<param name="DisableAnimations" value="0.000000"/>
	<param name="GUI Size" value="0.300000"/>
	<param name="MultiCore" value="0.000000"/>
</rootnode>
)";

} // namespace

class TestArturiaSize : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(QDir(m_root->path()).mkpath(u"Piano V2/tmp"_s));
        QFile file(m_root->filePath(u"Piano V2/tmp/plugin.pref.xml"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(kPrefs);
    }

    // Measured on Piano V2: 800 px wide at 0.0, 1280 at 0.3, 1600 at 0.5,
    // 1920 at 0.6, 2560 at 0.8, 3200 at 1.0.
    void stepsMatchArturiasMenu()
    {
        QCOMPARE(arturiaScale(0.0), 0.5);
        QCOMPARE(arturiaScale(0.3), 0.8);
        QCOMPARE(arturiaScale(0.5), 1.0);
        QCOMPARE(arturiaScale(0.6), 1.2);
        QCOMPARE(arturiaScale(1.0), 2.0);
    }

    void fitsTheLargestStepThatFits()
    {
        const QSizeF full(1600, 1258); // Piano V2 at 100 %
        QCOMPARE(fitArturiaGuiSize(full, QSizeF(2800, 1300)), 0.5);  // ultrawide: 100 %
        QCOMPARE(fitArturiaGuiSize(full, QSizeF(1280, 1006)), 0.3);  // exactly 80 %
        QCOMPARE(fitArturiaGuiSize(full, QSizeF(1100, 900)), 0.1);   // 60 % (70 % would be 1120 wide)
        QCOMPARE(fitArturiaGuiSize(full, QSizeF(3300, 2600)), 1.0);  // capped at 200 %
        QCOMPARE(fitArturiaGuiSize(full, QSizeF(300, 200)), 0.0);    // never below 50 %
    }

    void findsThePluginsSettingsFile()
    {
        const auto file = arturiaPrefsFile(u"C:/Program Files/Common Files/VST3/Arturia/Piano V2.vst3"_s, m_root->path());
        QVERIFY(file.has_value());
        QVERIFY(file->endsWith(u"Piano V2/tmp/plugin.pref.xml"_s));
        // Not an Arturia plugin, or an Arturia plugin without settings yet.
        QVERIFY(!arturiaPrefsFile(u"C:/Program Files/Common Files/VST3/FabFilter/FabFilter Pro-R.vst3"_s, m_root->path()));
        QVERIFY(!arturiaPrefsFile(u"C:/Program Files/Common Files/VST3/Arturia/Jup-8 V4.vst3"_s, m_root->path()));
    }

    void readsAndWritesOnlyTheSize()
    {
        const QString file = m_root->filePath(u"Piano V2/tmp/plugin.pref.xml"_s);
        const auto size = readArturiaGuiSize(file);
        QVERIFY(size.has_value());
        QCOMPARE(*size, 0.3);
        QVERIFY(writeArturiaGuiSize(file, 0.5).has_value());
        QCOMPARE(*readArturiaGuiSize(file), 0.5);
        QFile check(file);
        QVERIFY(check.open(QIODevice::ReadOnly));
        const QByteArray text = check.readAll();
        QVERIFY(text.contains(R"(<param name="DisableAnimations" value="0.000000"/>)")); // the rest untouched
        QVERIFY(text.contains(R"(<param name="GUI Size" value="0.500000"/>)"));
    }

    void aFileWithoutTheSettingIsAnError()
    {
        const QString file = m_root->filePath(u"other.xml"_s);
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write("<rootnode/>");
        out.close();
        const auto size = readArturiaGuiSize(file);
        QVERIFY(!size);
        QVERIFY(size.error().message.contains(u"GUI Size"_s));
        QVERIFY(!writeArturiaGuiSize(file, 0.5));
        QVERIFY(!readArturiaGuiSize(m_root->filePath(u"missing.xml"_s)));
    }

private:
    std::unique_ptr<QTemporaryDir> m_root;
};

QTEST_GUILESS_MAIN(TestArturiaSize)
#include "tst_arturia_size.moc"
