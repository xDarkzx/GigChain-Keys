#include "CrashReports.h"

#include "gigchain/core/Branding.h"
#include "gigchain/platform/CrashHandler.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSaveFile>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

constexpr int kKeepReports = 10; // dumps can be large: only the newest are kept
const QString kSeenFile = u"reported.txt"_s;

QStringList readSeen(const QDir& folder)
{
    QFile file(folder.filePath(kSeenFile));
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcUi).noquote() << "Could not read" << file.fileName() << ":" << file.errorString();
        return {};
    }
    // A list of report names: far below this unless damaged (then the tail is ignored).
    constexpr qint64 kMaxSeenBytes = 1024LL * 1024;
    return QString::fromUtf8(file.read(kMaxSeenBytes)).split(u'\n', Qt::SkipEmptyParts);
}

} // namespace

void CrashReports::install(const QString& folder)
{
    if (!QDir().mkpath(folder)) {
        qCWarning(lcUi).noquote() << "No crash reports: could not create" << folder;
        return;
    }
    // Only the newest reports are kept.
    const QDir dir(folder);
    const QFileInfoList reports = dir.entryInfoList({platform::crashReportPattern()}, QDir::Files, QDir::Time);
    for (qsizetype i = kKeepReports; i < reports.size(); ++i) {
        const QString report = reports.at(i).filePath();
        QFile::remove(report);
        QFile::remove(platform::crashNoteOf(report));
    }
    if (!platform::installCrashHandler(folder, branding::executable())) return; // logged, with the reason
    qCInfo(lcUi).noquote() << "Crash reports go to" << QDir::toNativeSeparators(QDir(folder).absolutePath());
}

void CrashReports::uninstall()
{
    platform::uninstallCrashHandler();
}

void CrashReports::setLastAction(const QString& action)
{
    platform::setCrashContext(action.toUtf8());
}

QStringList CrashReports::takeNewReports()
{
    const QString folder = platform::crashFolder();
    if (folder.isEmpty()) return {};
    const QDir dir(folder);
    QStringList seen = readSeen(dir);
    QStringList fresh;
    for (const QFileInfo& report :
         dir.entryInfoList({platform::crashReportPattern()}, QDir::Files, QDir::Time | QDir::Reversed)) {
        if (seen.contains(report.fileName())) continue;
        fresh << report.filePath();
        seen << report.fileName();
    }
    if (!fresh.isEmpty()) {
        seen = seen.mid(std::max<qsizetype>(0, seen.size() - 100)); // a short memory is enough
        QSaveFile out(dir.filePath(kSeenFile));
        if (!out.open(QIODevice::WriteOnly) || out.write(seen.join(u'\n').toUtf8()) < 0 || !out.commit()) {
            qCWarning(lcUi).noquote() << "Could not save" << out.fileName() << ":" << out.errorString();
        }
    }
    return fresh;
}

QString CrashReports::writeNow(const char* reason)
{
    const auto written = platform::writeCrashReportNow(reason);
    if (!written) {
        qCWarning(lcUi).noquote() << "Could not write a crash report:" << written.error().message;
        return {};
    }
    return *written;
}

QString CrashReports::noteOf(const QString& report)
{
    return platform::crashNoteOf(report);
}

} // namespace gigchain::ui
