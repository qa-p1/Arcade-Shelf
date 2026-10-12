// App-side tray hosting. Creating/starting this object never blocks the GUI.
// Keep an icon constructed but hidden when state() is "hosted". At startup,
// wait for startupReady() before creating/showing it (300 ms maximum).
#pragma once
#include "ArcadeLink.h"
#include <QThread>
#include <QTimer>
#include <optional>

namespace ArcadeLink {
inline constexpr int TrayStartupCapMs = 300;
inline constexpr int TrayRestartGraceMs = 5000;

// Deterministic transition policy used by the worker and conformance vectors.
// Times are monotonic milliseconds, not wall-clock timestamps.
class TrayHostStateMachine {
public:
  explicit TrayHostStateMachine(bool hasTray = true) : m_hasTray(hasTray) {}
  void begin(qint64 now, bool candidate, bool linkEnabled = true);
  void host(qint64 now, bool hosted, bool restarting = false);
  void closed(qint64 now);
  void setAllowed(qint64 now, bool allowed);
  void advance(qint64 now);
  QString state() const { return m_state; }
  bool ready() const { return m_ready; }
  std::optional<qint64> deadline() const;

private:
  void own();
  bool m_hasTray, m_allowed = true, m_ready = false;
  QString m_state = QStringLiteral("none");
  std::optional<qint64> m_startup, m_grace;
};

class ArcadeTrayHost : public QObject {
  Q_OBJECT
public:
  ArcadeTrayHost(const Locations &locations, const QString &appId,
                 QObject *parent = nullptr);
  ~ArcadeTrayHost() override;
  void start(bool linkEnabled = true, bool hasTray = true);
  void setLinkEnabled(bool enabled);
  void stop();                              // asynchronous; no thread join
  QString state() const { return m_state; } // "own", "hosted", "none"
  bool ready() const { return m_ready; }
signals:
  // Always on the object's owning thread, normally the GUI thread. Connect
  // with a context object (or Qt::QueuedConnection for another thread).
  void stateChanged(const QString &state);
  void startupReady(const QString &state);

private:
  friend class TrayHostWorker;
  void publish(const QString &state, bool ready, quint64 generation);
  Locations m_locations;
  QString m_appId, m_state = QStringLiteral("none");
  bool m_ready = false;
  bool m_hasTray = true;
  bool m_linkEnabled = true;
  quint64 m_generation = 0;
  QTimer m_startupGate;
  QPointer<QObject> m_worker;
  QPointer<QThread> m_thread;
};
} // namespace ArcadeLink
