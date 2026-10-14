#include "ArcadeTrayHost.h"
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimer>

namespace ArcadeLink {
void TrayHostStateMachine::own() {
  m_state = m_hasTray ? QStringLiteral("own") : QStringLiteral("none");
  m_ready = true;
  m_startup.reset();
  m_grace.reset();
}
void TrayHostStateMachine::begin(qint64 now, bool candidate, bool linkEnabled) {
  m_allowed = linkEnabled;
  if (!m_hasTray || !candidate || !linkEnabled)
    own();
  else {
    m_state = QStringLiteral("none");
    m_ready = false;
    m_startup = now + TrayStartupCapMs;
    m_grace.reset();
  }
}
void TrayHostStateMachine::host(qint64 now, bool hosted, bool restarting) {
  if (!m_allowed || !m_hasTray || !hosted) {
    own();
    return;
  }
  m_state = QStringLiteral("hosted");
  m_ready = true;
  m_startup.reset();
  if (restarting) {
    if (!m_grace)
      m_grace = now + TrayRestartGraceMs;
  } else
    m_grace.reset();
}
void TrayHostStateMachine::closed(qint64 now) {
  advance(now);
  if (!m_grace)
    own();
}
void TrayHostStateMachine::setAllowed(qint64 now, bool allowed) {
  m_allowed = allowed;
  if (!allowed)
    own();
  else
    advance(now);
}
void TrayHostStateMachine::advance(qint64 now) {
  if ((m_startup && now >= *m_startup) || (m_grace && now >= *m_grace))
    own();
}
std::optional<qint64> TrayHostStateMachine::deadline() const {
  if (m_startup && m_grace)
    return std::min(*m_startup, *m_grace);
  return m_startup ? m_startup : m_grace;
}

// Every registry read, QFileSystemWatcher and socket belongs to this worker's
// event loop. QLocalSocket waits through the OS event dispatcher when idle.
class TrayHostWorker : public QObject {
public:
  TrayHostWorker(ArcadeTrayHost *owner, Locations locations, QString id,
                 bool enabled, bool hasTray, quint64 generation)
      : m_owner(owner), m_locations(std::move(locations)), m_id(std::move(id)),
        m_link(enabled), m_hasTray(hasTray), m_generation(generation),
        m_policy(hasTray) {}
  void start() {
    m_clock.start();
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, [this] {
      m_policy.advance(m_clock.elapsed());
      if (m_phase < 2)
        closeSocket(); // Bound silent hello/subscription answers.
      publish();
    });
    m_registry = new Registry(m_locations, this);
    m_registry->refresh();
    m_registry->watch();
    connect(m_registry, &Registry::changed, this, [this] { reconcile(false); });
    reconcile(true);
  }
  void setLink(bool enabled) {
    if (m_link == enabled)
      return;
    m_link = enabled;
    m_attempted.clear();
    reconcile(false);
  }
  void shutdown() {
    m_stopped = true;
    if (m_timer)
      m_timer->stop();
    closeSocket();
    if (m_registry) {
      delete m_registry;
      m_registry = nullptr;
    }
    QThread::currentThread()->quit();
  }

private:
  void publish() {
    if (m_stopped)
      return;
    const auto state = m_policy.state();
    const bool ready = m_policy.ready();
    if (m_owner)
      QMetaObject::invokeMethod(
          m_owner,
          [owner = m_owner, state, ready, generation = m_generation] {
            if (owner)
              owner->publish(state, ready, generation);
          },
          Qt::QueuedConnection);
    m_timer->stop();
    auto deadline = m_policy.deadline();
    if (m_socket && m_phase < 2 && (!deadline || m_connectDeadline < *deadline))
      deadline = m_connectDeadline;
    if (deadline)
      m_timer->start(int(std::max<qint64>(0, *deadline - m_clock.elapsed())));
  }
  void closeSocket() {
    if (!m_socket)
      return;
    auto *socket = m_socket;
    m_socket = nullptr;
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
    m_buffer.clear();
  }
  void reconcile(bool initial) {
    if (m_stopped)
      return;
    const auto manifest = m_registry->app(Ids::Tools);
    const auto settings = manifest.value(QStringLiteral("settings")).toObject();
    const auto tray = settings.value(QStringLiteral("trayHost")).toObject();
    const bool explicitOff =
        !manifest.isEmpty() &&
        (!settings.value(QStringLiteral("linkEnabled")).toBool(true) ||
         !tray.value(QStringLiteral("enabled")).toBool() ||
         tray.value(QStringLiteral("excluded")).toArray().contains(m_id));
    const bool allowed = m_link && m_hasTray && !explicitOff;
    Endpoint endpoint;
    const bool candidate = allowed && !manifest.isEmpty() &&
                           tray.value(QStringLiteral("enabled")).toBool() &&
                           Endpoint::read(m_locations, Ids::Tools, &endpoint);
    if (initial)
      m_policy.begin(m_clock.elapsed(), candidate, m_link);
    else
      m_policy.setAllowed(m_clock.elapsed(), allowed);
    if (!allowed)
      closeSocket();
    // A changed endpoint may race the old socket's final restarting event.
    // Drain that socket before reconnecting, preserving notification order.
    const auto info = QFileInfo(m_locations.manifestPath(Ids::Tools));
    const auto revision =
        qMakePair(info.lastModified().toMSecsSinceEpoch(), info.size());
    if (candidate && !m_socket &&
        (endpoint.token != m_attempted || revision != m_attemptedRevision)) {
      m_attemptedRevision = revision;
      connectHost(endpoint);
    }
    publish();
  }
  void connectHost(const Endpoint &endpoint) {
    m_attempted = endpoint.token;
    m_connectDeadline = m_clock.elapsed() + TrayStartupCapMs;
    m_socket = new QLocalSocket(this);
    auto *socket = m_socket;
    m_phase = 0;
    connect(socket, &QLocalSocket::connected, this,
            [this, socket, token = endpoint.token] {
              if (socket != m_socket)
                return;
              socket->write(
                  requestLine(1, QStringLiteral("hello"),
                              {{QStringLiteral("token"), token},
                               {QStringLiteral("protocol"), QJsonArray{1}},
                               {QStringLiteral("client"),
                                QJsonObject{{QStringLiteral("id"), m_id},
                                            {QStringLiteral("version"),
                                             QStringLiteral("0.3.0")}}}}));
            });
    connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
      if (socket == m_socket)
        readable();
    });
    const auto ended = [this, socket] {
      if (socket != m_socket)
        return;
      // Read any buffered restarting notification before the EOF.
      readable();
      if (socket != m_socket)
        return;
      closeSocket();
      m_policy.closed(m_clock.elapsed());
      reconcile(false);
    };
    connect(socket, &QLocalSocket::disconnected, this, ended);
    connect(socket, &QLocalSocket::errorOccurred, this,
            [ended](QLocalSocket::LocalSocketError) { ended(); });
    socket->connectToServer(endpoint.address);
  }
  void readable() {
    if (!m_socket)
      return;
    m_buffer += m_socket->readAll();
    if (m_buffer.size() > MaxLineBytes && !m_buffer.contains('\n')) {
      closeSocket();
      m_policy.closed(m_clock.elapsed());
      publish();
      return;
    }
    while (m_socket && m_buffer.contains('\n')) {
      const auto index = m_buffer.indexOf('\n');
      const auto line = m_buffer.left(index + 1);
      m_buffer.remove(0, index + 1);
      QJsonObject message;
      const auto kind = classify(line, &message);
      if (kind == Kind::Invalid) {
        closeSocket();
        m_policy.closed(m_clock.elapsed());
        publish();
        return;
      }
      if (kind == Kind::Response && message.contains(QStringLiteral("error"))) {
        closeSocket();
        m_policy.closed(m_clock.elapsed());
        publish();
        return;
      }
      if (kind == Kind::Response &&
          message.value(QStringLiteral("id")).toInteger() == 1 &&
          m_phase == 0) {
        const auto result = message.value(QStringLiteral("result")).toObject();
        if (result.value(QStringLiteral("protocol")).toInt() !=
                ProtocolVersion ||
            result.value(QStringLiteral("server"))
                    .toObject()
                    .value(QStringLiteral("id"))
                    .toString() != Ids::Tools) {
          closeSocket();
          m_policy.closed(m_clock.elapsed());
          publish();
          return;
        }
        m_phase = 1;
        m_socket->write(
            requestLine(2, QStringLiteral("subscribe"),
                        {{QStringLiteral("topics"),
                          QJsonArray{QStringLiteral("tray.host")}}}));
      } else if (kind == Kind::Notification && m_phase >= 1 &&
                 message.value(QStringLiteral("method")).toString() ==
                     u"tray.host") {
        const auto params = message.value(QStringLiteral("params")).toObject();
        if (!params.value(QStringLiteral("hosted")).isBool())
          continue;
        m_phase = 2;
        m_policy.host(m_clock.elapsed(),
                      params.value(QStringLiteral("hosted")).toBool(),
                      params.value(QStringLiteral("restarting")).toBool());
        publish();
      }
    }
  }
  QPointer<ArcadeTrayHost> m_owner;
  Locations m_locations;
  QString m_id, m_attempted;
  QPair<qint64, qint64> m_attemptedRevision{-1, -1};
  bool m_link, m_hasTray, m_stopped = false;
  quint64 m_generation;
  qint64 m_connectDeadline = 0;
  TrayHostStateMachine m_policy;
  QElapsedTimer m_clock;
  QTimer *m_timer = nullptr;
  Registry *m_registry = nullptr;
  QLocalSocket *m_socket = nullptr;
  QByteArray m_buffer;
  int m_phase = 0;
};

ArcadeTrayHost::ArcadeTrayHost(const Locations &locations, const QString &appId,
                               QObject *parent)
    : QObject(parent), m_locations(locations), m_appId(appId) {
  m_startupGate.setSingleShot(true);
  m_startupGate.setTimerType(Qt::PreciseTimer);
  connect(&m_startupGate, &QTimer::timeout, this, [this] {
    if (!m_ready)
      publish(m_hasTray ? QStringLiteral("own") : QStringLiteral("none"), true,
              m_generation);
  });
}
ArcadeTrayHost::~ArcadeTrayHost() { stop(); }
void ArcadeTrayHost::start(bool enabled, bool hasTray) {
  if (m_worker)
    return;
  m_state = QStringLiteral("none");
  m_ready = false;
  m_hasTray = hasTray;
  m_linkEnabled = enabled;
  ++m_generation;
  m_startupGate.start(TrayStartupCapMs);
  auto *thread = new QThread;
  auto *worker = new TrayHostWorker(this, m_locations, m_appId, enabled,
                                    hasTray, m_generation);
  m_thread = thread;
  m_worker = worker;
  worker->moveToThread(thread);
  connect(thread, &QThread::started, worker, [worker] { worker->start(); });
  connect(thread, &QThread::finished, worker, &QObject::deleteLater);
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  thread->start();
}
void ArcadeTrayHost::setLinkEnabled(bool enabled) {
  m_linkEnabled = enabled;
  if (!enabled)
    publish(m_hasTray ? QStringLiteral("own") : QStringLiteral("none"), true,
            m_generation);
  if (m_worker)
    QMetaObject::invokeMethod(
        m_worker,
        [worker = m_worker, enabled] {
          if (worker)
            static_cast<TrayHostWorker *>(worker.data())->setLink(enabled);
        },
        Qt::QueuedConnection);
}
void ArcadeTrayHost::stop() {
  if (!m_worker)
    return;
  m_startupGate.stop();
  publish(m_hasTray ? QStringLiteral("own") : QStringLiteral("none"), true,
          m_generation);
  QMetaObject::invokeMethod(
      m_worker,
      [worker = m_worker] {
        if (worker)
          static_cast<TrayHostWorker *>(worker.data())->shutdown();
      },
      Qt::QueuedConnection);
  m_worker = nullptr;
  m_thread = nullptr;
  ++m_generation;
}
void ArcadeTrayHost::publish(const QString &state, bool ready,
                             quint64 generation) {
  if (!m_worker || generation != m_generation)
    return;
  if (!m_linkEnabled && state == u"hosted")
    return;
  if (state != m_state) {
    m_state = state;
    emit stateChanged(state);
  }
  if (ready && !m_ready) {
    m_ready = true;
    m_startupGate.stop();
    emit startupReady(state);
  }
}
} // namespace ArcadeLink
