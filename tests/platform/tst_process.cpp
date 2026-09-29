// The process's own safeguards: no library loaded from the folder the app was
// started in (Windows: a double-clicked setlist's), and a plugin scanner that
// crashes ends with an exit code the app reads, never a dialog or a hang.
#include "gigchain/platform/Process.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace gigchain;
using namespace Qt::StringLiterals;

class TestProcess : public QObject
{
    Q_OBJECT

private slots:
#ifdef Q_OS_WIN
    // Double-clicking a setlist starts the app in the setlist's folder (often
    // Downloads). A DLL someone left there must not be loaded by name.
    void aDllInTheCurrentFolderIsNotLoaded()
    {
        // Tries to load "gigchain_planted.dll" by name, as a library asking
        // Windows for a DLL does; true when it loaded (then it is let go).
        const auto plantedLoads = [] {
            HMODULE module = LoadLibraryW(L"gigchain_planted.dll");
            if (module == nullptr) return false;
            FreeLibrary(module);
            return true;
        };
        QTemporaryDir downloads;
        // Any harmless DLL, under a name nothing else has.
        std::array<wchar_t, MAX_PATH> system{};
        const UINT length = GetSystemDirectoryW(system.data(), static_cast<UINT>(system.size()));
        QVERIFY(length > 0 && length < system.size());
        const QString source = QString::fromWCharArray(system.data(), static_cast<qsizetype>(length)) + u"\\version.dll"_s;
        QVERIFY(QFile::copy(source, downloads.filePath(u"gigchain_planted.dll"_s)));
        const QString before = QDir::currentPath();
        QVERIFY(QDir::setCurrent(downloads.path()));

        QVERIFY2(plantedLoads(), "the test's DLL did not load from the current folder: the test proves nothing");
        QVERIFY(platform::hardenLibrarySearch().has_value());
        QVERIFY2(!plantedLoads(), "a DLL in the current folder was loaded");

        QVERIFY(QDir::setCurrent(before));
    }
#else
    // Libraries load by full path here: nothing to harden, and no error.
    void hardeningHasNothingToDo() { QVERIFY(platform::hardenLibrarySearch().has_value()); }
#endif

    // The scanner's crash ending: a plugin that crashes it ends the process
    // with a code (not 0, 2 or 3, which mean something else), no dialog.
    void aCrashEndsQuietlyWithACode()
    {
        QProcess child;
        child.setProgram(QCoreApplication::applicationFilePath());
        child.setArguments({u"--crash-quietly"_s});
        child.start();
        QVERIFY(child.waitForFinished(20'000));
        const int code = child.exitCode();
        QVERIFY2(code != 0 && code != 2 && code != 3, qPrintable(QString::number(code)));
    }
};

int main(int argc, char** argv)
{
    const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
    if (std::ranges::any_of(arguments, [](const char* argument) { return std::string_view(argument) == "--crash-quietly"; })) {
        platform::endQuietlyOnCrash();
        volatile std::uintptr_t nowhere = 0;
        // cppcheck-suppress nullPointer ; the crash is the point
        *reinterpret_cast<volatile int*>(nowhere) = 1; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr): the crash is the point
        return 0;
    }
    QCoreApplication app(argc, argv);
    TestProcess test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_process.moc"
