#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>

namespace openstage::ui {

// Width of the scroll bars shown beside a plugin editor that is bigger than
// the space it has (logical pixels).
inline constexpr double kScrollBarSize = 12.0;

struct EditorPlacement
{
    QRectF viewport;         // the visible window area (scene coordinates)
    QRectF editor;           // the plugin editor (scene coordinates; may extend past the viewport)
    QSizeF contentSize;      // the editor's full size
    QPointF scroll;          // scroll offset actually applied (clamped)
    bool scrollHorizontally = false;
    bool scrollVertically = false;
};

// Where a plugin editor of `editorSize` goes inside `area`: centred when it
// fits; otherwise the viewport leaves room for scroll bars and the editor is
// offset by `scroll` (clamped to the editor's edges). Pure: no windows.
EditorPlacement placeEditor(const QRectF& area, const QSizeF& editorSize, const QPointF& scroll);

} // namespace openstage::ui
