#include "EditorPlacement.h"

#include <algorithm>

namespace openstage::ui {

EditorPlacement placeEditor(const QRectF& area, const QSizeF& editorSize, const QPointF& scroll)
{
    EditorPlacement p;
    p.contentSize = editorSize;

    // A bar on one axis takes room from the other, which can make that one
    // need a bar too.
    bool horizontal = editorSize.width() > area.width();
    bool vertical = editorSize.height() > area.height();
    if (horizontal && !vertical) vertical = editorSize.height() > area.height() - kScrollBarSize;
    if (vertical && !horizontal) horizontal = editorSize.width() > area.width() - kScrollBarSize;
    p.scrollHorizontally = horizontal;
    p.scrollVertically = vertical;

    p.viewport = QRectF(area.x(), area.y(), area.width() - (vertical ? kScrollBarSize : 0.0),
                        area.height() - (horizontal ? kScrollBarSize : 0.0));

    const double maxX = std::max(0.0, editorSize.width() - p.viewport.width());
    const double maxY = std::max(0.0, editorSize.height() - p.viewport.height());
    p.scroll = QPointF(horizontal ? std::clamp(scroll.x(), 0.0, maxX) : 0.0,
                       vertical ? std::clamp(scroll.y(), 0.0, maxY) : 0.0);

    const double x = horizontal ? p.viewport.x() - p.scroll.x()
                                : p.viewport.x() + (p.viewport.width() - editorSize.width()) / 2.0;
    const double y = vertical ? p.viewport.y() - p.scroll.y()
                              : p.viewport.y() + (p.viewport.height() - editorSize.height()) / 2.0;
    p.editor = QRectF(QPointF(x, y), editorSize);
    return p;
}

} // namespace openstage::ui
