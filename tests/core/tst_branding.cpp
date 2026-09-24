// The product name comes from branding.cmake and nowhere else.
#include "gigchain/core/Branding.h"

#include <QtTest>

using namespace gigchain;
using namespace Qt::StringLiterals;

class TestBranding : public QObject
{
    Q_OBJECT

private slots:
    void nameIsBrandAndEdition()
    {
        QVERIFY(!branding::brand().isEmpty());
        QVERIFY(!branding::edition().isEmpty());
        QCOMPARE(branding::name(), branding::brand() + u" "_s + branding::edition());
    }

    void setlistsAreJsonFiles()
    {
        QVERIFY(branding::setlistSuffix().startsWith(u'.'));
        QVERIFY(branding::setlistSuffix().endsWith(u".json"_s));
    }

    void everythingIsFilledIn()
    {
        for (const QString& value : {branding::version(), branding::organization(), branding::executable(),
                                     branding::website()}) {
            QVERIFY(!value.isEmpty());
            QVERIFY2(!value.contains(u'@'), qPrintable(value)); // configure_file replaced every placeholder
        }
    }
};

QTEST_GUILESS_MAIN(TestBranding)
#include "tst_branding.moc"
