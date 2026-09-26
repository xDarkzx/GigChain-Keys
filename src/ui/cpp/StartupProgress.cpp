#include "StartupProgress.h"

#include <QCoreApplication>

namespace gigchain::ui {

void StartupProgress::report(const QString& title, const QString& text, double fraction)
{
    m_step = title;
    m_detail = text;
    m_progress = fraction;
    emit changed();
    // Startup work (plugin scan, loading sounds) runs on this thread, so the
    // splash can only repaint when events are processed. No user input yet.
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void StartupProgress::loading(const QString& title, const QString& what, int done, int total)
{
    m_active = done < total;
    report(title, what, total > 0 ? static_cast<double>(done) / total : -1.0);
}

void StartupProgress::finish(int remainingMs, const QString& listingTitle, const QString& readyStep)
{
    m_step = readyStep;
    m_listingStep = listingTitle;
    m_detail.clear();
    m_glideMs = remainingMs > 0 ? remainingMs : 0;
    m_progress = 1.0;
    emit changed();
}

} // namespace gigchain::ui
