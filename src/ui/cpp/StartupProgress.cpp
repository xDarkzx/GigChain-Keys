#include "StartupProgress.h"

#include <QCoreApplication>

namespace gigchain::ui {

void StartupProgress::report(const QString& step, const QString& detail, double progress)
{
    m_step = step;
    m_detail = detail;
    m_progress = progress;
    emit changed();
    // Startup work (plugin scan, loading sounds) runs on this thread, so the
    // splash can only repaint when events are processed. No user input yet.
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void StartupProgress::finish(int remainingMs, const QString& listingStep, const QString& readyStep)
{
    m_step = readyStep;
    m_listingStep = listingStep;
    m_detail.clear();
    m_glideMs = remainingMs > 0 ? remainingMs : 0;
    m_progress = 1.0;
    emit changed();
}

} // namespace gigchain::ui
