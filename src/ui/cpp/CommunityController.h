#pragma once

#include "gigchain/core/Error.h"

#include <QDate>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class QSettings;

namespace gigchain::ui {

// Inviting players in, never nagging (the README promises "no nag screens"):
// - a welcome, on the very first start only (someone who used an earlier
//   version is not welcomed again);
// - "What's new", once after each update, from the bundled CHANGELOG;
// - at most two asks for support in the app's whole life, at good moments:
//   after the first real use of Perform (kPerformMinutes in all), and after a
//   month; "Don't ask again" ends both. Main.qml never shows them while the
//   player performs or practises.
// Everything is remembered in the settings (community/...).
class CommunityController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    Q_PROPERTY(bool welcomeDue READ welcomeDue NOTIFY changed)
    Q_PROPERTY(bool whatsNewDue READ whatsNewDue NOTIFY changed)
    Q_PROPERTY(QString version READ version CONSTANT)
    // This version's changes, one line each (the CHANGELOG's bullets, plain text).
    Q_PROPERTY(QStringList whatsNew READ whatsNew CONSTANT)
    // "perform" or "month" when a support ask is due now, else "".
    Q_PROPERTY(QString supportAskDue READ supportAskDue NOTIFY changed)
    // "Don't ask again" was chosen: no support invitations anywhere (What's new too).
    Q_PROPERTY(bool asksOff READ asksOff NOTIFY changed)
    Q_PROPERTY(QString discordUrl READ discordUrl CONSTANT)
    Q_PROPERTY(QString sponsorUrl READ sponsorUrl CONSTANT)
    // A donation with no account (card, Apple Pay or Google Pay): the main way to give.
    Q_PROPERTY(QString donateUrl READ donateUrl CONSTANT)
    // The website's Support page: donate or sponsor, side by side.
    Q_PROPERTY(QString supportPageUrl READ supportPageUrl CONSTANT)
    Q_PROPERTY(QString videosUrl READ videosUrl CONSTANT)
    Q_PROPERTY(QString issuesUrl READ issuesUrl CONSTANT)

public:
    static constexpr int kPerformMinutes = 20; // "a real use": a rehearsal, a gig
    static constexpr int kMonthDays = 30;

    // `changelog`: the CHANGELOG's text, or why it could not be read (then
    // this version is not marked as seen, so What's new comes on a later start).
    CommunityController(QSettings& settings, QString version, const core::Result<QString>& changelog = bundledChangelog(),
                        QObject* parent = nullptr);

    // The CHANGELOG built into the app, or why it could not be read.
    [[nodiscard]] static core::Result<QString> bundledChangelog();

    // Once a run, at start: works out what is due. `today` is for tests: left
    // out, every check reads the clock, so the month ask comes due in a long run.
    void noteStart(const QDate& today = {});

    [[nodiscard]] bool welcomeDue() const { return m_welcomeDue; }
    [[nodiscard]] bool whatsNewDue() const { return m_whatsNewDue; }
    [[nodiscard]] QString version() const { return m_version; }
    [[nodiscard]] QStringList whatsNew() const { return m_whatsNew; }
    [[nodiscard]] QString supportAskDue() const;
    [[nodiscard]] bool asksOff() const;
    // Why the changelog could not be read ("" when it was).
    [[nodiscard]] QString changelogError() const { return m_changelogError; }
    [[nodiscard]] static QString discordUrl();
    [[nodiscard]] static QString sponsorUrl();
    [[nodiscard]] static QString donateUrl();
    [[nodiscard]] static QString supportPageUrl();
    [[nodiscard]] static QString videosUrl();
    [[nodiscard]] static QString issuesUrl();

    Q_INVOKABLE void welcomeSeen();
    Q_INVOKABLE void whatsNewSeen();
    // The ask for `reason` was shown (never again for it).
    Q_INVOKABLE void supportAsked(const QString& reason);
    // "Don't ask again": no more asks, ever. Returns why it could not be
    // saved ("" when it was), for the window to say.
    Q_INVOKABLE QString neverAskAgain();
    // Perform mode on or off (Main.qml): its minutes add up.
    Q_INVOKABLE void performing(bool on);
    // Time spent performing, added up to the millisecond (short visits count too).
    void addPerformTime(qint64 milliseconds);
    // (Tests: as if `minutes` more were spent performing.)
    void addPerformMinutes(int minutes) { addPerformTime(qint64{minutes} * 60000); }

    // A version's changes from a CHANGELOG (Keep a Changelog format): its
    // bullets as plain text, links and bold taken out. Empty if not there.
    [[nodiscard]] static QStringList changesFor(const QString& changelog, const QString& version);

signals:
    void changed();

private:
    // Writes the settings out now: `what` was saved, or why not (logged).
    [[nodiscard]] core::Result<void> persist(const QString& what);

    QSettings& m_settings;
    QString m_version;
    QStringList m_whatsNew;
    QString m_changelogError;
    QDate m_today; // (tests only; invalid: the clock)
    bool m_welcomeDue = false;
    bool m_whatsNewDue = false;
    QElapsedTimer m_performing; // valid while Perform is on
};

} // namespace gigchain::ui
