#pragma once

#include <QObject>
#include <QRect>
#include <QtQml/qqmlregistration.h>

class QSettings;

namespace gigchain::ui {

// Where the mixer is (remembered): along the bottom of the main window, down
// its left or right side, or in a window of its own (a second screen); its
// strips upright everywhere, as Logic's. Its window comes back where it was
// left, on the same screen, as the instrument's (InstrumentWindows::onScreens).
class MixerLayout : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

public:
    enum class Placement : int { Bottom = 0, Left = 1, Right = 2 };
    Q_ENUM(Placement)

    // Where it is in the main window (while not detached).
    Q_PROPERTY(Placement placement READ placement WRITE setPlacement NOTIFY placementChanged)
    // In a window of its own.
    Q_PROPERTY(bool detached READ detached WRITE setDetached NOTIFY detachedChanged)
    // How wide it is down a side of the main window (remembered).
    Q_PROPERTY(int sideWidth READ sideWidth WRITE setSideWidth NOTIFY sideWidthChanged)

    explicit MixerLayout(QSettings& settings, QObject* parent = nullptr);

    [[nodiscard]] Placement placement() const { return m_placement; }
    void setPlacement(Placement to);
    [[nodiscard]] bool detached() const { return m_detached; }
    void setDetached(bool to);
    [[nodiscard]] int sideWidth() const { return m_sideWidth; }
    void setSideWidth(int to);

    // Detached, or back in its place (a key).
    Q_INVOKABLE void toggleDetached() { setDetached(!m_detached); }
    // Where its window was last, when that is on a screen now; empty otherwise.
    Q_INVOKABLE [[nodiscard]] QRect restoredGeometry() const;
    Q_INVOKABLE [[nodiscard]] bool restoredMaximized() const;
    Q_INVOKABLE void rememberGeometry(const QRect& normal, bool maximized);

    static constexpr int kMinSideWidth = 340; // a channel strip, the aux and the master (Theme.sideMixerMinWidth)
    static constexpr int kMaxSideWidth = 1200;
    static constexpr int kDefaultSideWidth = 560; // three channels in sight, then the aux and the master

signals:
    void placementChanged();
    void detachedChanged();
    void sideWidthChanged();

private:
    QSettings& m_settings;
    Placement m_placement = Placement::Bottom;
    bool m_detached = false;
    int m_sideWidth = kDefaultSideWidth;
};

} // namespace gigchain::ui
