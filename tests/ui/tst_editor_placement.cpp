#include "EditorPlacement.h"

#include <QtTest>

using namespace gigchain::ui;

class TestEditorPlacement : public QObject
{
    Q_OBJECT

private slots:
    void smallEditorIsCentred()
    {
        const auto p = placeEditor(QRectF(100, 50, 800, 600), QSizeF(400, 300), QPointF());
        QVERIFY(!p.scrollHorizontally && !p.scrollVertically);
        QCOMPARE(p.editor, QRectF(300, 200, 400, 300)); // centred: 100 + (800-400)/2, 50 + (600-300)/2
        // The clipping window covers only the editor: it paints nothing itself,
        // so any area it covered beyond the editor would show stale pixels.
        QCOMPARE(p.viewport, p.editor);
    }

    void tooWideEditorScrollsSideways()
    {
        // Analog Lab V is 1280 wide at 100 %; the area here is 900 wide.
        const auto p = placeEditor(QRectF(0, 0, 900, 700), QSizeF(1280, 600), QPointF(200, 0));
        QVERIFY(p.scrollHorizontally);
        QVERIFY(!p.scrollVertically);
        QCOMPARE(p.editor.x(), -200.0); // scrolled 200 px
        // clipped to the area's width, and to the (centred) editor's height
        QCOMPARE(p.viewport, QRectF(0, (700 - kScrollBarSize - 600) / 2.0, 900, 600));
        QCOMPARE(p.contentSize, QSizeF(1280, 600));
    }

    void tooBigEditorScrollsBothWays()
    {
        const auto p = placeEditor(QRectF(0, 0, 900, 500), QSizeF(1280, 886), QPointF());
        QVERIFY(p.scrollHorizontally && p.scrollVertically);
        QCOMPARE(p.viewport, QRectF(0, 0, 900 - kScrollBarSize, 500 - kScrollBarSize));
    }

    void scrollIsClampedToTheEdges()
    {
        const auto p = placeEditor(QRectF(0, 0, 900, 700), QSizeF(1280, 600), QPointF(5000, -40));
        const double maxX = 1280 - 900;
        QCOMPARE(p.scroll, QPointF(maxX, 0));
        QCOMPARE(p.editor.x(), -maxX);
    }
};

QTEST_GUILESS_MAIN(TestEditorPlacement)
#include "tst_editor_placement.moc"
