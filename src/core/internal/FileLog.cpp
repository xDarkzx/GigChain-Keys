#include "openstage/core/FileLog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <memory>
#include <mutex>

using namespace Qt::StringLiterals;

namespace openstage::core {
namespace {

struct LogState
{
    std::mutex mutex;
    std::unique_ptr<QFile> file;
    QtMessageHandler previous = nullptr;
    bool installed = false;
};

LogState& state()
{
    static LogState s;
    return s;
}

const char* levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return "debug";
    case QtInfoMsg: return "info";
    case QtWarningMsg: return "warning";
    case QtCriticalMsg: return "critical";
    case QtFatalMsg: return "fatal";
    }
    return "unknown";
}

// A failed write cannot be logged (that would recurse into this handler);
// the message still reaches the previous handler (debugger/console) below.
void writeLine(QFile& file, const QString& line, bool flush)
{
    file.write(line.toUtf8());
    file.write("\n");
    if (flush) file.flush();
}

void handler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    QtMessageHandler previous = nullptr;
    {
        LogState& s = state();
        const std::lock_guard lock(s.mutex);
        previous = s.previous;
        if (s.file) {
            const QString line = u"%1 %2 %3: %4"_s.arg(
                QDateTime::currentDateTime().toString(Qt::ISODateWithMs), QString::fromLatin1(levelName(type)),
                QString::fromLatin1(context.category ? context.category : "default"), message);
            // Every line reaches the disk at once: readable while the app runs
            // and not lost in a crash.
            writeLine(*s.file, line, true);
        }
    }
    if (previous) previous(type, context, message);
}

} // namespace

Result<void> FileLog::install(const QString& path)
{
    uninstall();

    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        return fail(ErrorCode::FileWriteFailed, u"Could not create log folder %1"_s.arg(info.absolutePath()));
    }
    if (info.exists() && info.size() > kMaxBytes) {
        const QString older = path + u".1"_s;
        QFile::remove(older);
        if (!QFile::rename(path, older)) {
            return fail(ErrorCode::FileWriteFailed, u"Could not rotate log file %1"_s.arg(path));
        }
    }

    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return fail(ErrorCode::FileWriteFailed, u"Could not open log file %1: %2"_s.arg(path, file->errorString()));
    }
    writeLine(*file, u"--- OpenStage log started %1"_s.arg(QDateTime::currentDateTime().toString(Qt::ISODate)), true);

    LogState& s = state();
    {
        const std::lock_guard lock(s.mutex);
        s.file = std::move(file);
        s.installed = true;
    }
    s.previous = qInstallMessageHandler(&handler);
    return {};
}

void FileLog::uninstall()
{
    LogState& s = state();
    if (!s.installed) return;
    qInstallMessageHandler(s.previous);
    const std::lock_guard lock(s.mutex);
    s.file.reset();
    s.previous = nullptr;
    s.installed = false;
}

} // namespace openstage::core
