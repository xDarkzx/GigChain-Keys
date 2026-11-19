#pragma once

#include <QString>
#include <QStringList>

namespace gigchain::engine {

// Remembers plugins that crashed the app while loading, so they are not
// loaded again (a crashing plugin would otherwise take the app down at
// every start). As in Audacity 4's audio plugins load guard: a marker file
// is written before a plugin loads and removed when loading ends, however
// it ends (an error or exception included). A marker still there at the
// next start means the app died loading that plugin: it is blocked until
// the user asks to try it again.
//
// Files live in `folder` (markers in folder/loading, the list in
// folder/blocked.json). No folder: the guard does nothing. Main thread.
// Problems with the files are logged; loading goes ahead regardless.
class PluginLoadGuard
{
public:
    explicit PluginLoadGuard(QString folder = {});

    // Marks `pluginPath` as loading until this goes.
    class Loading
    {
    public:
        Loading(const Loading&) = delete;
        Loading& operator=(const Loading&) = delete;
        Loading(Loading&& other) noexcept : m_marker(std::move(other.m_marker)) { other.m_marker.clear(); }
        Loading& operator=(Loading&&) = delete;
        ~Loading();

    private:
        friend class PluginLoadGuard;
        explicit Loading(QString marker) : m_marker(std::move(marker)) {}
        QString m_marker; // empty: nothing to remove
    };
    [[nodiscard]] Loading loading(const QString& pluginPath) const;

    // At start-up: the plugins whose loading never ended last time, now
    // blocked (their markers cleared). `lastRunCrashed`: the crash reporter
    // found the last run's crash; without one the app was closed or killed
    // (#55), so a plugin is blocked only the second time in a row its loading
    // is cut short (one that kills the app before a report is written is still
    // caught, and never takes the app down at every start).
    QStringList takeCrashed(bool lastRunCrashed);

    [[nodiscard]] bool isBlocked(const QString& pluginPath) const;
    [[nodiscard]] QStringList blocked() const { return m_blocked; }
    void unblock(const QString& pluginPath);

private:
    [[nodiscard]] QString markerFor(const QString& pluginPath) const;
    void readBlocked();
    void writeBlocked() const;

    QString m_folder;
    QStringList m_blocked;
};

} // namespace gigchain::engine
