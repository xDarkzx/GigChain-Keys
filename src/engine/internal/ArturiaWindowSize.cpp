#include "ArturiaWindowSize.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace openstage::engine {
namespace {

// <param name="GUI Size" value="0.300000"/>
const QRegularExpression kGuiSize(uR"re(<param name="GUI Size" value="([0-9.]+)")re"_s);

constexpr int kSteps = 10; // 0.0, 0.1, ... 1.0

} // namespace

QString arturiaDataRoot()
{
    return u"C:/ProgramData/Arturia"_s;
}

std::optional<QString> arturiaPrefsFile(const QString& bundlePath, const QString& dataRoot)
{
    const QFileInfo bundle(bundlePath);
    if (bundle.dir().dirName().compare(u"Arturia"_s, Qt::CaseInsensitive) != 0) return std::nullopt;
    const QString file = QDir(dataRoot).filePath(bundle.completeBaseName() + u"/tmp/plugin.pref.xml"_s);
    if (!QFileInfo::exists(file)) return std::nullopt;
    return file;
}

double arturiaScale(double guiSize)
{
    const double v = std::clamp(guiSize, 0.0, 1.0);
    return v <= 0.5 ? 0.5 + v : 1.0 + 2.0 * (v - 0.5);
}

double fitArturiaGuiSize(QSizeF fullSize, QSizeF area)
{
    if (fullSize.isEmpty()) return 0.0;
    double best = 0.0;
    for (int step = 0; step <= kSteps; ++step) {
        const double v = step / static_cast<double>(kSteps);
        const double scale = arturiaScale(v);
        // A pixel of slack for rounding in the plugin's own size maths.
        if (fullSize.width() * scale <= area.width() + 1.0 && fullSize.height() * scale <= area.height() + 1.0) best = v;
    }
    return best;
}

core::Result<double> readArturiaGuiSize(const QString& prefsFile)
{
    QFile file(prefsFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return core::fail(core::ErrorCode::InvalidData, u"Cannot read %1: %2"_s.arg(prefsFile, file.errorString()));
    }
    const QString text = QString::fromUtf8(file.readAll());
    const auto match = kGuiSize.match(text);
    if (!match.hasMatch()) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 has no GUI Size setting"_s.arg(prefsFile));
    }
    bool ok = false;
    const double value = match.captured(1).toDouble(&ok);
    if (!ok || value < 0.0 || value > 1.0) {
        return core::fail(core::ErrorCode::InvalidData,
                          u"%1 has an unreadable GUI Size (%2)"_s.arg(prefsFile, match.captured(1)));
    }
    return std::round(value * kSteps) / kSteps;
}

core::Result<void> writeArturiaGuiSize(const QString& prefsFile, double guiSize)
{
    QFile in(prefsFile);
    if (!in.open(QIODevice::ReadOnly)) {
        return core::fail(core::ErrorCode::InvalidData, u"Cannot read %1: %2"_s.arg(prefsFile, in.errorString()));
    }
    QString text = QString::fromUtf8(in.readAll());
    in.close();
    const auto match = kGuiSize.match(text);
    if (!match.hasMatch()) {
        return core::fail(core::ErrorCode::InvalidData, u"%1 has no GUI Size setting"_s.arg(prefsFile));
    }
    const QString value = QString::number(std::clamp(guiSize, 0.0, 1.0), 'f', 6);
    text.replace(match.capturedStart(1), match.capturedLength(1), value);

    QSaveFile out(prefsFile);
    if (!out.open(QIODevice::WriteOnly) || out.write(text.toUtf8()) < 0 || !out.commit()) {
        return core::fail(core::ErrorCode::InvalidData, u"Cannot write %1: %2"_s.arg(prefsFile, out.errorString()));
    }
    return {};
}

} // namespace openstage::engine
