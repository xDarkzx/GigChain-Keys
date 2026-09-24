#pragma once

#include <QImage>
#include <QRect>

class QWindow;

namespace openstage::ui {

// A picture of part of a top-level window, taken by Windows itself
// (PrintWindow with full content rendering), so GPU-drawn plugin editors
// come out right even when other windows cover them. `region` is in the
// window's physical pixels; an empty region means the whole window.
// Returns a null image on failure (logged).
QImage captureWindow(QWindow* topLevel, QRect region = {});

} // namespace openstage::ui
