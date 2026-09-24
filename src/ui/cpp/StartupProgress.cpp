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

} // namespace gigchain::ui
