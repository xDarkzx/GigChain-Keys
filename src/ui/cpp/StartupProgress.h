#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace gigchain::ui {

// What the splash screen shows while OpenStage starts: the current step
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

public:
    using QObject::QObject;

    [[nodiscard]] QString step() const { return m_step; }
    [[nodiscard]] QString detail() const { return m_detail; }
    [[nodiscard]] double progress() const { return m_progress; }

    // Updates the splash and lets it repaint (startup work runs on this thread).
    void report(const QString& step, const QString& detail = {}, double progress = -1.0);

signals:
    void changed();

private:
    QString m_step;
    QString m_detail;
    double m_progress = -1.0;
};

} // namespace gigchain::ui
