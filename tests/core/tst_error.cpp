#include "LeakCheck.h"
#include "gigchain/core/Error.h"

#include <QtTest>

#include <memory>
#include <vector>

using namespace gigchain::core;

class TestError : public QObject
{
    Q_OBJECT

private slots:
    void failCarriesCodeAndMessage()
    {
        const Result<int> result = fail(ErrorCode::ParseFailed, QStringLiteral("bad input"));
        QVERIFY(!result.has_value());
        QVERIFY(result.error().code == ErrorCode::ParseFailed);
        QCOMPARE(result.error().message, QStringLiteral("bad input"));
    }

    void valueRoundTrips()
    {
        const Result<int> result = 42;
        QVERIFY(result.has_value());
        QCOMPARE(*result, 42);
    }

    void everyCodeHasText()
    {
        const ErrorCode codes[] = {ErrorCode::FileNotFound,    ErrorCode::FileReadFailed,
                                   ErrorCode::FileWriteFailed, ErrorCode::FileTooLarge,
                                   ErrorCode::ParseFailed,     ErrorCode::UnsupportedVersion,
                                   ErrorCode::InvalidData,     ErrorCode::LimitExceeded,
                                   ErrorCode::OutOfRange,      ErrorCode::DeviceUnavailable,
                                   ErrorCode::SystemRefused};
        for (const ErrorCode code : codes) {
            QVERIFY(!toString(code).isEmpty());
        }
    }

    void leakCheckerDetectsLiveBlocks()
    {
        if (!gigchain::test::leakCheckAvailable()) {
            QSKIP("CRT debug heap only available in the debug preset");
        }
        std::vector<std::unique_ptr<int>> kept;
        const auto grown = gigchain::test::leakedBlocks([&kept] { kept.push_back(std::make_unique<int>(1)); });
        QVERIFY(grown > 0);
        kept.clear();
        QCOMPARE(gigchain::test::leakedBlocks([] { auto temp = std::make_unique<int>(2); }), 0LL);
    }
};

QTEST_GUILESS_MAIN(TestError)
#include "tst_error.moc"
