// One app at a time: a second start hands its setlist to the running app.
#include "SingleInstance.h"

#include "gigchain/platform/InstanceLock.h"

#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>
#include <QtTest>

#include <array>
#include <chrono>
#include <cstdio>
#include <future>

#ifdef Q_OS_WIN
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#endif

using namespace gigchain::ui;
using namespace Qt::StringLiterals;

class TestSingleInstance : public QObject
{
    Q_OBJECT

    // A name of this test's own, so the app running meanwhile is not asked.
    static QString uniqueName() { return u"tst_single_instance-"_s + QUuid::createUuid().toString(QUuid::WithoutBraces); }

    // A later start is another process: here, another thread (the running
    // one reads in its event loop, which QTRY_ keeps turning).
    static std::future<bool> startAgain(const QString& name, const QString& path)
    {
        return std::async(std::launch::async, [name, path] { return SingleInstance(name).handOver(path); });
    }

private slots:
    void onlyTheFirstStartIsFirst()
    {
        const QString name = uniqueName();
        {
            SingleInstance running(name);
            QVERIFY(running.first());
            SingleInstance again(name);
            QVERIFY(!again.first());
        }
        SingleInstance afterItEnded(name); // the app was closed: the next start is the first
        QVERIFY(afterItEnded.first());
    }

    // A setlist double-clicked while the app runs: the running app opens it.
    void theRunningAppGetsTheSetlist()
    {
        const QString name = uniqueName();
        SingleInstance running(name);
        QVERIFY(running.first());
        QVERIFY2(running.listen().has_value(), "could not listen");
        QSignalSpy opened(&running, &SingleInstance::opened);

        auto handed = startAgain(name, u"C:/Gigs/Friday Night (late).gigchain"_s);
        QTRY_COMPARE(opened.size(), 1);
        QVERIFY(handed.get());
        QCOMPARE(opened.at(0).at(0).toString(), u"C:/Gigs/Friday Night (late).gigchain"_s);

        // Started again with no setlist: the running app only comes to the front.
        handed = startAgain(name, QString());
        QTRY_COMPARE(opened.size(), 2);
        QVERIFY(handed.get());
        QCOMPARE(opened.at(1).at(0).toString(), QString());
    }

    // Names outside Latin-1 arrive whole.
    void anyNameArrivesWhole()
    {
        const QString name = uniqueName();
        SingleInstance running(name);
        QVERIFY(running.first());
        QVERIFY(running.listen().has_value());
        QSignalSpy opened(&running, &SingleInstance::opened);
        const QString path = u"C:/Música/Répertoire — 日本.gigchain"_s;
        auto handed = startAgain(name, path);
        QTRY_COMPARE(opened.size(), 1);
        QVERIFY(handed.get());
        QCOMPARE(opened.at(0).at(0).toString(), path);
    }

    // The app crashed (or was killed) while it was the first: its lock is
    // left behind, and the next start must still become the first.
    void aLockLeftByACrashDoesNotBlock()
    {
        const QString name = uniqueName();
        QProcess crashed;
        crashed.setProgram(QCoreApplication::applicationFilePath());
        crashed.setArguments({u"--hold-lock"_s, name});
        crashed.start();
        QVERIFY(crashed.waitForReadyRead(20'000));
        QCOMPARE(crashed.readAllStandardOutput().trimmed(), QByteArray("held"));
        {
            SingleInstance meanwhile(name);
            QVERIFY(!meanwhile.first()); // it is running: not first
        }
        crashed.kill();
        QVERIFY(crashed.waitForFinished(20'000));
        SingleInstance next(name);
        QVERIFY(next.first());
    }

    // Each user of the computer has an app of their own: the socket a later
    // start hands its setlist to is this user's.
    void theSocketIsPerUser()
    {
        const QString socket = gigchain::platform::instanceSocketName(u"GigChain-test"_s);
#ifdef Q_OS_WIN
        QCOMPARE(socket, u"GigChain-test-"_s + qEnvironmentVariable("USERNAME"));
#else
        const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        QVERIFY(!runtime.isEmpty());
        QCOMPARE(socket, runtime + u"/GigChain-test.sock"_s);
        QVERIFY(!(QFileInfo(runtime).permissions() & (QFile::ReadOther | QFile::WriteOther | QFile::ReadGroup | QFile::WriteGroup)));
#endif
    }

#ifdef Q_OS_WIN
    // Only this Windows user reaches the running app's pipe: no "Everyone",
    // no anonymous, no other user of the computer.
    void onlyThisUserReachesThePipe()
    {
        const QString name = uniqueName();
        SingleInstance running(name);
        QVERIFY(running.first());
        QVERIFY(running.listen().has_value());
        const std::wstring pipe = (u"\\\\.\\pipe\\"_s + name + u'-' + qEnvironmentVariable("USERNAME")).toStdWString();
        HANDLE client = CreateFileW(pipe.c_str(), READ_CONTROL, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        QVERIFY2(client != INVALID_HANDLE_VALUE, qPrintable(u"could not open the pipe (error %1)"_s.arg(GetLastError())));
        PACL dacl = nullptr;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        const DWORD got = GetSecurityInfo(client, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &descriptor);
        CloseHandle(client);
        QCOMPARE(got, DWORD{ERROR_SUCCESS});
        QVERIFY(dacl != nullptr); // no list at all would let everyone in

        // Everyone the list lets in: this user, and the system itself.
        HANDLE token = nullptr;
        QVERIFY(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
        std::array<unsigned char, 256> userBuffer{};
        DWORD size = 0;
        QVERIFY(GetTokenInformation(token, TokenUser, userBuffer.data(), static_cast<DWORD>(userBuffer.size()), &size));
        CloseHandle(token);
        const PSID user = reinterpret_cast<TOKEN_USER*>(userBuffer.data())->User.Sid; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): the API's own struct
        QStringList others;
        for (DWORD i = 0; i < dacl->AceCount; ++i) {
            void* ace = nullptr;
            QVERIFY(GetAce(dacl, i, &ace));
            const auto* allowed = static_cast<const ACCESS_ALLOWED_ACE*>(ace);
            if (allowed->Header.AceType != ACCESS_ALLOWED_ACE_TYPE) continue;
            const PSID sid = const_cast<DWORD*>(&allowed->SidStart); // NOLINT(cppcoreguidelines-pro-type-const-cast): the API's layout
            if (EqualSid(sid, user) || IsWellKnownSid(sid, WinLocalSystemSid) || IsWellKnownSid(sid, WinBuiltinAdministratorsSid)) continue;
            LPWSTR text = nullptr;
            ConvertSidToStringSidW(sid, &text);
            others << QString::fromWCharArray(text);
            LocalFree(text);
        }
        LocalFree(descriptor);
        QVERIFY2(others.isEmpty(), qPrintable(u"the pipe also lets in: "_s + others.join(u", "_s)));
    }
#endif

    // The running app not listening (it could not): said, not hidden.
    void aRunningAppThatDoesNotListenIsReported()
    {
        const QString name = uniqueName();
        SingleInstance running(name);
        QVERIFY(running.first());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Could not reach the running app"_s));
        QVERIFY(!SingleInstance(name).handOver(u"C:/Gigs/x.gigchain"_s));
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = QCoreApplication::arguments();
    // A stand-in for a running app: takes the lock, says so, waits to be killed.
    if (arguments.size() == 3 && arguments.at(1) == u"--hold-lock"_s) {
        SingleInstance running(arguments.at(2));
        if (!running.first()) return 1;
        std::fputs("held\n", stdout);
        std::fflush(stdout);
        QThread::sleep(std::chrono::seconds(60));
        return 0;
    }
    TestSingleInstance test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_single_instance.moc"
