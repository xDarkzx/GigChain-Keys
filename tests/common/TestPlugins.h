#pragma once

// The real plugins the tests play and load, per system: tests that need one
// skip (saying so) where it is not installed.
//  - Windows: Arturia Piano V2 (instrument) and TDR Kotelnikov (a small
//    effect), as installed on the development machine.
//  - Linux: Surge XT and Surge XT Effects (free, open source; installed by
//    tools/setup-linux.sh).
//  - macOS: Surge XT and Surge XT Effects (brew install --cask surge-xt).

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QString>

namespace gigchain::test {

// Copies a plugin: a single .vst3 file (some Windows plugins) or a whole
// bundle folder (the VST3 standard, and every Linux plugin). True when done.
inline bool copyPlugin(const QString& from, const QString& to)
{
    if (QFileInfo(from).isFile()) return QFile::copy(from, to);
    QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString target = to + file.mid(from.size());
        if (!QDir().mkpath(QFileInfo(target).path()) || !QFile::copy(file, target)) return false;
    }
    return QFileInfo(to).isDir();
}

// Removes a plugin: a single file or a whole bundle folder.
inline bool removePlugin(const QString& path)
{
    return QFileInfo(path).isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
}

struct TestPlugin
{
    QString path;    // the .vst3 bundle
    QString name;    // the name the plugin gives itself
    QString website; // a word its maker's web address holds
};

#ifdef Q_OS_WIN
inline const QString kVst3Folder = QStringLiteral("C:/Program Files/Common Files/VST3");
inline const TestPlugin kInstrument{QStringLiteral("C:/Program Files/Common Files/VST3/Arturia/Piano V2.vst3"),
                                    QStringLiteral("Piano V2"), QStringLiteral("arturia")};
inline const TestPlugin kEffect{QStringLiteral("C:/Program Files/Common Files/VST3/TDR Kotelnikov.vst3"),
                                QStringLiteral("TDR Kotelnikov"), QStringLiteral("tokyodawn")};
#elif defined(Q_OS_MACOS)
inline const QString kVst3Folder = QStringLiteral("/Library/Audio/Plug-Ins/VST3");
inline const TestPlugin kInstrument{QStringLiteral("/Library/Audio/Plug-Ins/VST3/Surge XT.vst3"), QStringLiteral("Surge XT"),
                                    QStringLiteral("surge")};
inline const TestPlugin kEffect{QStringLiteral("/Library/Audio/Plug-Ins/VST3/Surge XT Effects.vst3"),
                                QStringLiteral("Surge XT Effects"), QStringLiteral("surge")};
#else
inline const QString kVst3Folder = QStringLiteral("/usr/lib/vst3");
inline const TestPlugin kInstrument{QStringLiteral("/usr/lib/vst3/Surge XT.vst3"), QStringLiteral("Surge XT"),
                                    QStringLiteral("surge")};
inline const TestPlugin kEffect{QStringLiteral("/usr/lib/vst3/Surge XT Effects.vst3"), QStringLiteral("Surge XT Effects"),
                                QStringLiteral("surge")};
#endif

} // namespace gigchain::test
