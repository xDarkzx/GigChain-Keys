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

    // What is visible of the area once the scroll bars take their room.
    const QRectF visible(area.x(), area.y(), area.width() - (vertical ? kScrollBarSize : 0.0),
                         area.height() - (horizontal ? kScrollBarSize : 0.0));

    const double maxX = std::max(0.0, editorSize.width() - visible.width());
    const double maxY = std::max(0.0, editorSize.height() - visible.height());
    p.scroll = QPointF(horizontal ? std::clamp(scroll.x(), 0.0, maxX) : 0.0,
                       vertical ? std::clamp(scroll.y(), 0.0, maxY) : 0.0);

    const double x = horizontal ? visible.x() - p.scroll.x()
                                : visible.x() + (visible.width() - editorSize.width()) / 2.0;
    const double y = vertical ? visible.y() - p.scroll.y()
                              : visible.y() + (visible.height() - editorSize.height()) / 2.0;
    p.editor = QRectF(QPointF(x, y), editorSize);
    // The clipping window paints nothing itself, so it covers only the part of
    // the editor that is visible; the rest of the area stays QML-painted.
    p.viewport = p.editor.intersected(visible);
    return p;
}

} // namespace openstage::ui
