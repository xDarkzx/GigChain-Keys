// gck-ctl: the live test control's command line (docs/testing/live-test-control.md).
//
//   gck-ctl [--socket <name>] <command> [args...]   one command; prints its answer
//   gck-ctl run <file>                               one command a line (# comments); stops at the first error
//   gck-ctl start [setlist]                          starts the dev app next to it with --dev-control
//   gck-ctl kill                                     ends the app at once (as a player's force quit)
//   gck-ctl kill-at <pattern> [ms]                   ends it the moment its log shows a matching line
//   gck-ctl restart [setlist]                        quits it (or kills it) and starts it again
//   gck-ctl sleep <ms>                               (in a command file: waits)
//
// Exit code: 0 done, 1 an error (said), 2 the app is not running with --dev-control.
#include "DevProtocol.h"

#include "gigchain/core/Branding.h"
#include "gigchain/platform/Process.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>

#include <optional>

using namespace Qt::StringLiterals;
using namespace gigchain;

namespace {

constexpr int kDone = 0;
constexpr int kFailed = 1;
constexpr int kNotRunning = 2;

QTextStream& out()
{
    static QTextStream stream(stdout);
    return stream;
}

// A word as JSON: a number when it is one, else text.
QJsonValue jsonArg(const QString& word)
{
    bool whole = false;
    const qlonglong integer = word.toLongLong(&whole);
    if (whole) return integer;
    bool real = false;
    const double number = word.toDouble(&real);
    if (real && word.contains(u'.')) return number;
    return word;
}

struct Answer
{
    int code = kFailed;
    QJsonObject json;
};

// Sends one command and waits for its answer (as long as the command takes).
Answer ask(const QString& socket, const QString& cmd, const QJsonArray& args = {})
{
    QLocalSocket connection;
    connection.connectToServer(socket);
    if (!connection.waitForConnected(2000)) {
        return Answer{kNotRunning, QJsonObject{{u"ok"_s, false}, {u"error"_s, u"The app is not running with --dev-control (nothing on %1)"_s.arg(socket)}}};
    }
    connection.write(QJsonDocument(QJsonObject{{u"cmd"_s, cmd}, {u"args"_s, args}}).toJson(QJsonDocument::Compact) + '\n');
    connection.flush();
    QElapsedTimer waited;
    waited.start();
    constexpr qint64 kLongest = qint64{15} * 60 * 1000; // (wait and cpu may run ten minutes)
    while (!connection.canReadLine()) {
        if (connection.state() != QLocalSocket::ConnectedState && !connection.waitForReadyRead(100)) {
            return Answer{kFailed, QJsonObject{{u"ok"_s, false}, {u"error"_s, u"The app closed the connection before answering %1"_s.arg(cmd)}}};
        }
        if (waited.elapsed() > kLongest) return Answer{kFailed, QJsonObject{{u"ok"_s, false}, {u"error"_s, u"No answer to %1"_s.arg(cmd)}}};
        connection.waitForReadyRead(1000);
    }
    const QJsonObject json = QJsonDocument::fromJson(connection.readLine()).object();
    return Answer{json.value(u"ok"_s).toBool() ? kDone : kFailed, json};
}

int print(const Answer& answer)
{
    out() << QString::fromUtf8(QJsonDocument(answer.json).toJson(QJsonDocument::Indented)) << Qt::flush;
    return answer.code;
}

int said(int code, const QString& text)
{
    return print(Answer{code, QJsonObject{{u"ok"_s, code == kDone}, {code == kDone ? u"note"_s : u"error"_s, text}}});
}

bool running(const QString& socket)
{
    QLocalSocket probe;
    probe.connectToServer(socket);
    return probe.waitForConnected(500);
}

bool killProcess(qint64 pid)
{
#ifdef Q_OS_WIN
    return QProcess::execute(u"taskkill"_s, {u"/F"_s, u"/PID"_s, QString::number(pid)}) == 0;
#else
    return QProcess::execute(u"kill"_s, {u"-9"_s, QString::number(pid)}) == 0;
#endif
}

bool waitUntil(const std::function<bool()>& done, int ms)
{
    QElapsedTimer clock;
    clock.start();
    while (!done()) {
        if (clock.elapsed() > ms) return false;
        QThread::msleep(100);
    }
    return true;
}

int start(const QString& socket, const QString& setlist)
{
    if (running(socket)) return said(kDone, u"Already running"_s);
    const QString exe = QCoreApplication::applicationDirPath() + u'/' + platform::programFileName(branding::executable());
    QStringList args{u"--dev-control"_s};
    if (!setlist.isEmpty()) args << setlist;
    qint64 pid = 0;
    if (!QProcess::startDetached(exe, args, QCoreApplication::applicationDirPath(), &pid)) {
        return said(kFailed, u"Could not start %1"_s.arg(exe));
    }
    if (!waitUntil([&socket] { return running(socket); }, 120'000)) {
        return said(kFailed, u"Started (pid %1), but the live test control never answered: a release build, or it crashed (see its log)"_s.arg(pid));
    }
    return print(Answer{kDone, QJsonObject{{u"ok"_s, true}, {u"pid"_s, pid}}});
}

int kill(const QString& socket)
{
    const Answer pid = ask(socket, u"pid"_s);
    if (pid.code != kDone) return print(pid);
    if (!killProcess(pid.json.value(u"pid"_s).toInteger())) return said(kFailed, u"Could not end process %1"_s.arg(pid.json.value(u"pid"_s).toInteger()));
    waitUntil([&socket] { return !running(socket); }, 10'000);
    return said(kDone, u"Ended process %1"_s.arg(pid.json.value(u"pid"_s).toInteger()));
}

// Ends the app the moment its log shows a line matching `pattern` (a plugin
// loading: the app is busy then, so this watches from outside).
int killAt(const QString& socket, const QString& pattern, int ms)
{
    const QRegularExpression match(pattern);
    if (pattern.isEmpty() || !match.isValid()) return said(kFailed, u"kill-at <pattern> [ms]"_s);
    const Answer paths = ask(socket, u"paths"_s);
    const Answer pid = ask(socket, u"pid"_s);
    if (paths.code != kDone || pid.code != kDone) return print(paths.code != kDone ? paths : pid);
    QFile log(paths.json.value(u"log"_s).toString());
    if (!log.open(QIODevice::ReadOnly)) return said(kFailed, u"Could not read the log %1"_s.arg(log.fileName()));
    log.seek(log.size());
    QByteArray pending;
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms) {
        pending += log.readAll();
        qsizetype end = 0;
        while ((end = pending.indexOf('\n')) >= 0) {
            const QString line = QString::fromUtf8(pending.left(end));
            pending.remove(0, end + 1);
            if (line.contains(u"Dev control: {"_s) || !match.match(line).hasMatch()) continue;
            if (!killProcess(pid.json.value(u"pid"_s).toInteger())) return said(kFailed, u"Saw \"%1\" but could not end the app"_s.arg(line));
            return print(Answer{kDone, QJsonObject{{u"ok"_s, true}, {u"killedAt"_s, line}}});
        }
        QThread::msleep(10);
    }
    return said(kFailed, u"\"%1\" never came in %2 ms: the app is still running"_s.arg(pattern).arg(ms));
}

int restart(const QString& socket, const QString& setlist)
{
    if (running(socket)) {
        (void)ask(socket, u"quit"_s);
        if (!waitUntil([&socket] { return !running(socket); }, 15'000)) {
            if (const int killed = kill(socket); killed != kDone) return killed;
        }
    }
    return start(socket, setlist);
}

// One command, the tool's own or the app's.
int perform(const QString& socket, const QStringList& words);

int runFile(const QString& socket, const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return said(kFailed, u"Could not read %1"_s.arg(path));
    int number = 0;
    while (!file.atEnd()) {
        ++number;
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(u'#')) continue;
        out() << u"> "_s << line << u'\n' << Qt::flush;
        if (const int code = perform(socket, QProcess::splitCommand(line)); code != kDone) {
            out() << u"Stopped at line %1 of %2\n"_s.arg(number).arg(path) << Qt::flush;
            return code;
        }
    }
    return kDone;
}

int perform(const QString& socket, const QStringList& words)
{
    if (words.isEmpty()) return said(kFailed, u"No command (gck-ctl help lists the app's)"_s);
    const QString cmd = words.front();
    const QString first = words.value(1);
    if (cmd == u"run"_s) return runFile(socket, first);
    if (cmd == u"start"_s) return start(socket, first);
    if (cmd == u"kill"_s) return kill(socket);
    if (cmd == u"kill-at"_s) return killAt(socket, first, words.size() > 2 ? words.at(2).toInt() : 60'000);
    if (cmd == u"restart"_s) return restart(socket, first);
    if (cmd == u"sleep"_s) {
        QThread::msleep(static_cast<unsigned long>(std::clamp(first.toInt(), 0, 600'000)));
        return kDone;
    }
    QJsonArray args;
    for (qsizetype i = 1; i < words.size(); ++i) args.append(jsonArg(words.at(i)));
    return print(ask(socket, cmd, args));
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QStringList words = QCoreApplication::arguments().mid(1);
    QString socket = devcontrol::socketName();
    if (words.size() >= 2 && words.front() == u"--socket"_s) {
        socket = words.at(1);
        words = words.mid(2);
    }
    if (words.isEmpty()) {
        out() << u"gck-ctl [--socket <name>] <command> [args...] | run <file> | start [setlist] | kill | kill-at <pattern> [ms] | restart\n"
                 u"The app's commands: gck-ctl help (with the dev app running: gck-ctl start)\n"_s
              << Qt::flush;
        return kFailed;
    }
    return perform(socket, words);
}
