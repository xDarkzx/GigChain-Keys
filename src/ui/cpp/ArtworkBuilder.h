#pragma once

#include "ArtworkCache.h"

#include "openstage/engine/IPluginEditor.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace openstage::engine {
class IEngine;
}

namespace openstage::ui {

// "Build artwork": opens each plugin that has no picture yet, one at a time,
// in a window placed off-screen, waits for it to draw, captures it into the
// cache and closes it. Runs on the main thread in small steps so the UI stays
// usable between plugins. Instruments go first.
class ArtworkBuilder : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    Q_PROPERTY(bool running READ isRunning NOTIFY progressChanged)
    Q_PROPERTY(int done READ done NOTIFY progressChanged)
    Q_PROPERTY(int total READ total NOTIFY progressChanged)
    Q_PROPERTY(QString current READ current NOTIFY progressChanged)

public:
    static constexpr int kDrawWaitMs = 2500; // time a plugin gets to draw its editor

    ArtworkBuilder(engine::IEngine& engine, ArtworkCache& cache, QObject* parent = nullptr);
    ~ArtworkBuilder() override;
    ArtworkBuilder(const ArtworkBuilder&) = delete;
    ArtworkBuilder& operator=(const ArtworkBuilder&) = delete;
    ArtworkBuilder(ArtworkBuilder&&) = delete;
    ArtworkBuilder& operator=(ArtworkBuilder&&) = delete;

    [[nodiscard]] bool isRunning() const { return m_running; }
    [[nodiscard]] int done() const { return m_done; }
    [[nodiscard]] int total() const { return m_total; }
    [[nodiscard]] QString current() const { return m_current; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();

signals:
    void progressChanged();
    void finished();

private:
    void next();
    void capture();
    void closeCurrent();

    engine::IEngine& m_engine;
    ArtworkCache& m_cache;
    QStringList m_queue;         // plugin ids
    QStringList m_names;         // matching display names
    std::unique_ptr<engine::IPluginEditor> m_editor;
    QPointer<QWindow> m_window;  // Qt-owned off-screen window
    QString m_currentId;
    QString m_current;
    bool m_running = false;
    int m_done = 0;
    int m_total = 0;
};

} // namespace openstage::ui
