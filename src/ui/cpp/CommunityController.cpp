#include "CommunityController.h"

#include "gigchain/core/Branding.h"

#include <QFile>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSettings>

#include <utility>

Q_DECLARE_LOGGING_CATEGORY(lcUi)

using namespace Qt::StringLiterals;

namespace gigchain::ui {
namespace {

const QString kLastVersionKey = u"community/lastVersion"_s;
const QString kWelcomedKey = u"community/welcomed"_s;
const QString kFirstStartKey = u"community/firstStart"_s; // ISO date
const QString kPerformMinutesKey = u"community/performMinutes"_s;
const QString kAskedKey = u"community/asked"_s;       // the asks shown: "perform", "month"
const QString kNeverAskKey = u"community/neverAsk"_s;
const QString kOldStarAskedKey = u"session/starAsked"_s; // 0.11's one star ask: counts as an ask
const QString kChangelogResource = u":/changelog/CHANGELOG.md"_s;
constexpr qsizetype kMaxChanges = 12;

QString readChangelog()
{
    QFile file(kChangelogResource);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcUi).noquote() << "What's new: the changelog could not be read (" << kChangelogResource << ":" << file.errorString() << ")";
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

// One bullet as plain text: "**Bold**" and "[text](link)" read as their words.
QString plain(QString line)
{
    static const QRegularExpression kLink(uR"(\[([^\]]+)\]\([^)]*\))"_s);
    line.replace(kLink, u"\\1"_s);
    line.remove(u"**"_s);
    line.remove(u'`');
    return line.simplified();
}

} // namespace

CommunityController::CommunityController(QSettings& settings, QString version, const QString& changelog, QObject* parent)
    : QObject(parent), m_settings(settings), m_version(std::move(version))
{
    m_whatsNew = changesFor(changelog.isEmpty() ? readChangelog() : changelog, m_version);
}

void CommunityController::noteStart(const QDate& today)
{
    m_today = today;
    if (!m_settings.contains(kFirstStartKey)) m_settings.setValue(kFirstStartKey, today.toString(Qt::ISODate));
    // 0.11's star ask was one of the two.
    if (m_settings.value(kOldStarAskedKey, false).toBool()) {
        QStringList asked = m_settings.value(kAskedKey).toStringList();
        if (!asked.contains(u"month"_s)) m_settings.setValue(kAskedKey, asked << u"month"_s);
        m_settings.remove(kOldStarAskedKey);
    }
    const QString last = m_settings.value(kLastVersionKey).toString();
    if (last.isEmpty()) {
        // Never recorded: a first start, or someone updating from before 0.12
        // (their settings say so): welcomed, or told what's new.
        const bool usedBefore = m_settings.contains(u"session/starts"_s) || m_settings.contains(u"session/lastFile"_s)
                                || m_settings.contains(u"audio/device"_s) || m_settings.contains(u"audio/driver"_s);
        m_welcomeDue = !usedBefore && !m_settings.value(kWelcomedKey, false).toBool();
        m_whatsNewDue = usedBefore && !m_whatsNew.isEmpty();
        if (usedBefore) m_settings.setValue(kWelcomedKey, true); // (they know the app)
    } else {
        m_welcomeDue = false;
        m_whatsNewDue = last != m_version && !m_whatsNew.isEmpty();
    }
    if (!m_welcomeDue && !m_whatsNewDue) m_settings.setValue(kLastVersionKey, m_version);
    qCInfo(lcUi).noquote() << "Community: welcome" << m_welcomeDue << "| what's new" << m_whatsNewDue << "| support ask"
                           << (supportAskDue().isEmpty() ? u"none"_s : supportAskDue());
    emit changed();
}

QString CommunityController::supportAskDue() const
{
    if (m_settings.value(kNeverAskKey, false).toBool()) return {};
    const QStringList asked = m_settings.value(kAskedKey).toStringList();
    if (asked.size() >= 2) return {};
    if (!asked.contains(u"perform"_s) && m_settings.value(kPerformMinutesKey, 0).toInt() >= kPerformMinutes) return u"perform"_s;
    const QDate first = QDate::fromString(m_settings.value(kFirstStartKey).toString(), Qt::ISODate);
    const QDate today = m_today.isValid() ? m_today : QDate::currentDate();
    if (!asked.contains(u"month"_s) && first.isValid() && first.daysTo(today) >= kMonthDays) return u"month"_s;
    return {};
}

QString CommunityController::discordUrl()
{
    return u"https://discord.gg/BGn8Ujh37m"_s;
}

QString CommunityController::sponsorUrl()
{
    return u"https://github.com/sponsors/xDarkzx"_s;
}

QString CommunityController::videosUrl()
{
    return u"https://www.youtube.com/playlist?list=PLQp8ULNqWrb4"_s;
}

QString CommunityController::issuesUrl()
{
    return branding::website() + u"/issues/new/choose"_s; // (the product's name only in branding.cmake)
}

void CommunityController::welcomeSeen()
{
    m_settings.setValue(kWelcomedKey, true);
    m_settings.setValue(kLastVersionKey, m_version);
    if (std::exchange(m_welcomeDue, false)) emit changed();
}

void CommunityController::whatsNewSeen()
{
    m_settings.setValue(kLastVersionKey, m_version);
    if (std::exchange(m_whatsNewDue, false)) emit changed();
}

void CommunityController::supportAsked(const QString& reason)
{
    if (reason != u"perform"_s && reason != u"month"_s) {
        qCWarning(lcUi).noquote() << "Community: no support ask" << reason;
        return;
    }
    QStringList asked = m_settings.value(kAskedKey).toStringList();
    if (asked.contains(reason)) return;
    m_settings.setValue(kAskedKey, asked << reason);
    qCInfo(lcUi).noquote() << "Community: asked for support (" << reason << ")";
    emit changed();
}

void CommunityController::neverAskAgain()
{
    m_settings.setValue(kNeverAskKey, true);
    qCInfo(lcUi) << "Community: never asked for support again";
    emit changed();
}

void CommunityController::performing(bool on)
{
    if (on) {
        if (!m_performing.isValid()) m_performing.start();
        return;
    }
    if (!m_performing.isValid()) return;
    addPerformMinutes(static_cast<int>(m_performing.elapsed() / 60000));
    m_performing.invalidate();
}

void CommunityController::addPerformMinutes(int minutes)
{
    if (minutes <= 0) return;
    const QString before = supportAskDue();
    m_settings.setValue(kPerformMinutesKey, m_settings.value(kPerformMinutesKey, 0).toInt() + minutes);
    if (supportAskDue() != before) emit changed();
}

QStringList CommunityController::changesFor(const QString& changelog, const QString& version)
{
    QStringList changes;
    bool inside = false;
    QString item;
    const auto close = [&] {
        if (!item.isEmpty() && changes.size() < kMaxChanges) changes << plain(item);
        item.clear();
    };
    const QString heading = u"## ["_s + version + u']';
    for (const QString& line : changelog.split(u'\n')) {
        if (line.startsWith(u"## ["_s)) {
            if (inside) break;
            inside = line.startsWith(heading);
            continue;
        }
        if (!inside) continue;
        if (line.startsWith(u"- "_s)) {
            close();
            item = line.mid(2);
        } else if (line.startsWith(u"  "_s) && !item.isEmpty()) {
            item += u' ' + line.trimmed();
        } else {
            close();
        }
    }
    close();
    return changes;
}

} // namespace gigchain::ui
