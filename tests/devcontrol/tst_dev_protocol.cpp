#include "DevProtocol.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace gigchain::devcontrol;
using namespace Qt::StringLiterals;

class TestDevProtocol : public QObject
{
    Q_OBJECT

private slots:
    void aRequestIsParsed()
    {
        const auto request = parseRequest(R"({"cmd":"note","args":[60,100]})");
        QVERIFY2(request.has_value(), request ? "" : qPrintable(request.error().message));
        QCOMPARE(request->cmd, u"note"_s);
        QCOMPARE(request->args.size(), 2);
        QCOMPARE(request->args.at(0).toInt(), 60);
        const auto noArgs = parseRequest(R"({"cmd":"status"})");
        QVERIFY(noArgs.has_value());
        QVERIFY(noArgs->args.isEmpty());
    }

    void badRequestsSayWhy()
    {
        const auto says = [](QByteArrayView line, const QString& part) {
            const auto request = parseRequest(line);
            if (request.has_value()) return false;
            return request.error().message.contains(part, Qt::CaseInsensitive);
        };
        QVERIFY(says("not json", u"JSON"_s));
        QVERIFY(says("[1,2]", u"object"_s));
        QVERIFY(says(R"({"args":[]})", u"cmd"_s));
        QVERIFY(says(R"({"cmd":3})", u"cmd"_s));
        QVERIFY(says(R"({"cmd":"x","args":{}})", u"args"_s));
        QVERIFY(says(QByteArray(kMaxLine + 1, 'x'), u"long"_s));
    }

    void answersAreOneLine()
    {
        const QByteArray ok = okLine({{u"text"_s, u"two\nlines"_s}});
        QVERIFY(ok.endsWith('\n'));
        QCOMPARE(ok.count('\n'), 1);
        QVERIFY(QJsonDocument::fromJson(ok).object().value(u"ok"_s).toBool());
        const QByteArray error = errorLine(u"no such command: jump"_s);
        QCOMPARE(error.count('\n'), 1);
        const QJsonObject answer = QJsonDocument::fromJson(error).object();
        QVERIFY(!answer.value(u"ok"_s).toBool());
        QCOMPARE(answer.value(u"error"_s).toString(), u"no such command: jump"_s);
    }

    void aPlayScriptIsParsed()
    {
        const auto steps = parsePlayScript(u"on 60 100; wait 50; on 60 100; wait 50; off 60"_s, 1);
        QVERIFY2(steps.has_value(), steps ? "" : qPrintable(steps.error().message));
        QCOMPARE(steps->size(), std::size_t{5});
        QCOMPARE(steps->at(0).kind, PlayStep::Message);
        QCOMPARE(steps->at(0).bytes, QByteArray::fromHex("903c64"));
        QCOMPARE(steps->at(1).kind, PlayStep::Wait);
        QCOMPARE(steps->at(1).ms, 50);
        QCOMPARE(steps->at(4).bytes, QByteArray::fromHex("803c00"));
        const auto second = parsePlayScript(u"on 60 100; cc 64 127; bend 8191; pc 5"_s, 2);
        QVERIFY(second.has_value());
        QCOMPARE(second->at(0).bytes, QByteArray::fromHex("913c64"));
        QCOMPARE(second->at(1).bytes, QByteArray::fromHex("b1407f"));
        QCOMPARE(second->at(2).bytes, QByteArray::fromHex("e17f7f"));
        QCOMPARE(second->at(3).bytes, QByteArray::fromHex("c105"));
    }

    void badScriptsSayWhere()
    {
        const auto at = [](const QString& script) {
            const auto steps = parsePlayScript(script, 1);
            return steps.has_value() ? QString() : steps.error().message;
        };
        QVERIFY(at(u"on 200 100"_s).contains(u"step 1"_s));
        QVERIFY(at(u"on 60 100; wait -1"_s).contains(u"step 2"_s));
        QVERIFY(at(u"on 60 100; off 60; jump 3"_s).contains(u"step 3"_s));
        QVERIFY(!at(u""_s).isEmpty());
        QVERIFY(!parsePlayScript(u"on 60 100"_s, 17).has_value());
    }

    void hexMidiIsParsed()
    {
        QCOMPARE(*parseHexMidi(u"B0 40 7F"_s), QByteArray::fromHex("b0407f"));
        QCOMPARE(*parseHexMidi(u"F0 7E 7F 06 01 F7"_s), QByteArray::fromHex("f07e7f0601f7"));
        QCOMPARE(*parseHexMidi(u"c0 05"_s), QByteArray::fromHex("c005"));
        QVERIFY(!parseHexMidi(u"GG"_s).has_value());
        QVERIFY(!parseHexMidi(u""_s).has_value());
        QVERIFY(!parseHexMidi(u"90 3C 64 01"_s).has_value()); // four bytes, not sysex
        QVERIFY(!parseHexMidi(u"3C 64"_s).has_value());       // no status byte first
    }
};

QTEST_GUILESS_MAIN(TestDevProtocol)
#include "tst_dev_protocol.moc"
