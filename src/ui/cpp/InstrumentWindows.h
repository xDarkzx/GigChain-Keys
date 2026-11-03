#pragma once

#include "gigchain/core/Ids.h"

#include <QList>
#include <QObject>
#include <QRect>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <vector>

class QSettings;

namespace gigchain::engine {
class IEngine;
}

namespace gigchain::ui {

class DocumentController;
class EditorService;

// Instruments' windows of their own, beside the main window (on a second
// screen, say), as MainStage's and Gig Performer's plug-in windows:
//  - the linked window follows the selected channel, as the Instrument tab
//    does (MainStage's Link); at most one, and it is remembered: open or not,
//    where and how big, on which screen;
//  - more windows can each be kept on one channel (Link off), for one
//    instrument per screen.
// A plugin draws in one place at a time (most cannot draw two windows), so
// where an instrument is in a window of its own, the Instrument tab and the
// linked window show nothing of it (EditorService::shownElsewhere): a window
// kept on it comes first, then the linked window, then the tab. Each window
// is an EditorService (its QML window, InstrumentWindow.qml, shows it).
class InstrumentWindows : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    // The open windows' EditorServices, oldest first.
    Q_PROPERTY(QList<QObject*> windows READ windows NOTIFY windowsChanged)
    Q_PROPERTY(bool linkedOpen READ linkedOpen NOTIFY windowsChanged)
    // Above every other window, or not (remembered).
    Q_PROPERTY(bool onTop READ onTop WRITE setOnTop NOTIFY onTopChanged)

public:
    InstrumentWindows(engine::IEngine& engine, DocumentController& document, EditorService& docked, QSettings& settings,
                      QObject* parent = nullptr);
    ~InstrumentWindows() override;
    InstrumentWindows(const InstrumentWindows&) = delete;
    InstrumentWindows& operator=(const InstrumentWindows&) = delete;
    InstrumentWindows(InstrumentWindows&&) = delete;
    InstrumentWindows& operator=(InstrumentWindows&&) = delete;

    [[nodiscard]] QList<QObject*> windows() const;
    [[nodiscard]] bool linkedOpen() const { return linkedWindow() != nullptr; }
    [[nodiscard]] bool onTop() const { return m_onTop; }
    void setOnTop(bool on);

    // The linked window: opened, or brought to the front.
    Q_INVOKABLE void popOut();
    // The linked window back into the Instrument tab, or out of it (a key).
    Q_INVOKABLE void toggle();
    // A window kept on channel `channel` of the current patch; the window
    // already showing it is brought to the front instead.
    Q_INVOKABLE void openFor(int channel);
    // Closes `window` (its X).
    Q_INVOKABLE void close(QObject* window);
    // Closes `window` and shows its channel in the Instrument tab again.
    Q_INVOKABLE void dock(QObject* window);
    // The window showing what `view` would show (the tab's channel, say) to the front.
    Q_INVOKABLE void showWindowOf(QObject* view);
    // Closes the window showing what `view` would show, so `view` shows it.
    Q_INVOKABLE void bringBack(QObject* view);
    // Link on (follows the selection) or off (stays on its channel). On only
    // while no other window is linked (canLink); false when refused.
    Q_INVOKABLE bool setLinked(QObject* window, bool linked);
    [[nodiscard]] bool canLink(const QObject* window) const;

    // Where the linked window was last (its frame's inside, in the desktop's
    // units), when that is on a screen now; empty otherwise (a screen gone:
    // the window opens on the main window instead).
    Q_INVOKABLE [[nodiscard]] QRect restoredGeometry() const;
    Q_INVOKABLE [[nodiscard]] bool restoredMaximized() const;
    Q_INVOKABLE void rememberGeometry(const QRect& normal, bool maximized);
    // `saved` when enough of it is on one of `screens` to grab and move
    // (shrunk to fit that screen), else an empty rect.
    [[nodiscard]] static QRect onScreens(const QRect& saved, const QList<QRect>& screens);

signals:
    void windowsChanged();
    void onTopChanged();
    void raiseRequested(QObject* window);
    // A window was docked: its channel is the selected one (show the tab).
    void docked();

private:
    struct Window
    {
        EditorService* service = nullptr; // a child of this
        bool closing = false;             // its view lets its plugin go first
    };
    // A new window, kept on `keep` (nullopt: linked).
    EditorService* create(const std::optional<core::ChannelId>& keep);
    [[nodiscard]] EditorService* linkedWindow() const;
    [[nodiscard]] Window* find(QObject* window);
    // The open window whose instrument is `view`'s, if any (not `view`).
    [[nodiscard]] EditorService* showing(const EditorService& view) const;
    [[nodiscard]] bool elsewhere(const EditorService& view) const;
    // Every view asks again: those losing their instrument first, so a
    // plugin is never open in two places, even for a moment.
    void recheck();
    void rememberOpen();

    engine::IEngine& m_engine;
    DocumentController& m_document;
    EditorService& m_docked;
    QSettings& m_settings;
    std::vector<Window> m_windows;
    bool m_onTop = true;
};

} // namespace gigchain::ui
