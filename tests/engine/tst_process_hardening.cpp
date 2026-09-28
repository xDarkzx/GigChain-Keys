// Double-clicking a setlist starts the app in the setlist's folder (often
// Downloads). A DLL someone left there must not be loaded by name.
#include "gigchain/engine/ProcessHardening.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <array>

#include <windows.h>

using namespace gigchain::engine;
using namespace Qt::StringLiterals;

class TestProcessHardening : public QObject
{
    Q_OBJECT

    // Tries to load "gigchain_planted.dll" by name, as a library asking
    // Windows for a DLL does; true when it loaded (then it is let go).
    static bool plantedLoads()
    {
        HMODULE module = LoadLibraryW(L"gigchain_planted.dll");
        if (module == nullptr) return false;
        FreeLibrary(module);
        return true;
    }

private slots:
    void aDllInTheCurrentFolderIsNotLoaded()
    {
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
        QVERIFY(hardenDllSearch().has_value());
        QVERIFY2(!plantedLoads(), "a DLL in the current folder was loaded");

        QVERIFY(QDir::setCurrent(before));
    }
};

QTEST_GUILESS_MAIN(TestProcessHardening)
#include "tst_process_hardening.moc"
