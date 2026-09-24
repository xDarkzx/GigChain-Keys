#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace gigchain::ui {

// What the splash screen shows while the app starts: the current step
// ("Scanning plugins"), a detail ("Piano V2") and progress 0-1 (or -1 when
// the step has no measurable progress).
class StartupProgress : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    Q_PROPERTY(QString step READ step NOTIFY changed)
    Q_PROPERTY(QString detail READ detail NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    // When > 0, the bar glides to its new value over this many ms.
    Q_PROPERTY(int glideMs READ glideMs NOTIFY changed)
    // Every plugin found, in scan order: the splash names each one.
    Q_PROPERTY(QStringList plugins READ plugins NOTIFY changed)
    // What the splash says while it names each plugin after loading.
    Q_PROPERTY(QString listingStep READ listingStep NOTIFY changed)

public:
    using QObject::QObject;

    [[nodiscard]] QString step() const { return m_step; }
    [[nodiscard]] QString detail() const { return m_detail; }
    [[nodiscard]] double progress() const { return m_progress; }
    [[nodiscard]] int glideMs() const { return m_glideMs; }
    [[nodiscard]] QStringList plugins() const { return m_plugins; }
    [[nodiscard]] QString listingStep() const { return m_listingStep; }
    void addPlugin(const QString& name) { m_plugins << name; }

    // Updates the splash and lets it repaint (startup work runs on this thread).
    void report(const QString& step, const QString& detail = {}, double progress = -1.0);
    // Everything is loaded. The splash still stays up for `remainingMs`: it
    // steps through every plugin found, the bar filling left to right, then
    // shows `readyStep`. `listingStep` is shown while it names them.
    void finish(int remainingMs, const QString& listingStep, const QString& readyStep);

signals:
    void changed();

private:
    QString m_step;
    QString m_detail;
    double m_progress = -1.0;
    int m_glideMs = 0;
    QStringList m_plugins;
    QString m_listingStep;
};

} // namespace gigchain::ui
