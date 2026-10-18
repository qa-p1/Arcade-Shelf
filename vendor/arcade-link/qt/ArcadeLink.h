// Arcade Link for Qt (C++20, Qt 6.8+): the same protocol as the Rust crate,
// for Arcade Wheel and Shelf. See SPEC.md. Passes spec/vectors/ (qt/tests).
//
// Vendor qt/ with tools/vendor_qt.py; pin the commit and file checksums.
//
// Threading: Server and Registry live on the thread that creates them and are
// driven by its event loop (no extra threads, no timers while idle).
// Client and invokeAction() block: call them from a worker thread only.
#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>

namespace ArcadeLink {

inline constexpr int ProtocolVersion = 1;
inline constexpr qsizetype MaxLineBytes = 1024 * 1024;
inline constexpr int HelloTimeoutMs = 150;
inline constexpr int LaunchTimeoutMs = 3000;
inline constexpr int SpinnerDelayMs = 150;
QList<int> supportedProtocols();

namespace Ids {
inline const QString Box = QStringLiteral("arcade.box");
inline const QString Lens = QStringLiteral("arcade.lens");
inline const QString Look = QStringLiteral("arcade.look");
inline const QString Wheel = QStringLiteral("arcade.wheel");
inline const QString Clipboard = QStringLiteral("arcade.clipboard");
inline const QString Tools = QStringLiteral("arcade.tools");
inline const QString Shelf = QStringLiteral("arcade.shelf");
inline const QString Find = QStringLiteral("arcade.find");
QStringList apps();
}

QString appName(const QString &id);
QString appPitch(const QString &id);
QString releasesUrl(const QString &id);

// ---- Locations (SPEC §2) ---------------------------------------------------

struct Locations {
    QString registry;
    QString runtime;
    QString handoff;
    static Locations discover();               // honors ARCADE_HOME
    static Locations under(const QString &root);
    QString manifestPath(const QString &appId) const;
    QString endpointPath(const QString &appId) const;
};

QString currentPlatform();                     // "linux", "windows", "macos"
bool ensurePrivateDir(const QString &dir);     // mkpath + 0700 on Unix

// ---- AppImage start (Rust: childenv::on_startup) ---------------------------
// The type-2 AppImage runtime gives the app an open handle on its mount (fd
// 1023) that isn't close-on-exec, so every program the app starts inherited
// it, and one that outlives the app (the installed copy, another Arcade app)
// kept the old mount and runtime alive. releaseMountHandles makes handles on
// $APPDIR or any .mount_* directory close-on-exec; the launch helpers call it.
// onStartup does that and also leaves the $APPDIR/usr working directory that
// linuxdeploy's AppRun picks for $OWD, where the user started the app, so
// relative command-line paths work. Call onStartup first thing in main().
void onStartup();
void releaseMountHandles();

// ---- Logs (Rust: logs::capture_stderr) --------------------------------------
// Started from the launcher, at login or by Tools, an app's stderr (and so
// Qt's warnings) often goes to /dev/null, or to NUL or nowhere on Windows.
// captureStderr sends it to
// <dir>/<name>.log instead, keeping one older <name>.log.1 once the log passes
// 1 MiB. A terminal, file, pipe or the journal keeps receiving it. Returns the
// log's path, or an empty string when stderr was left alone or on failure.
bool stderrGoesNowhere();
QString captureStderr(const QString &dir, const QString &name);
QString captureStderrTo(const QString &dir, const QString &name); // no check

// ---- Errors (SPEC §6) ------------------------------------------------------

struct Error {
    QString code;                              // empty: no error
    QString message;
    QString reason;
    qint64 limit = -1;
    bool isError() const { return !code.isEmpty(); }
    // Optional v1 methods may be absent on old servers.
    bool isUnsupported() const;
    QJsonObject toJson() const;
    static Error fromJson(const QJsonObject &o);
    static Error make(const QString &code, const QString &message, const QString &reason = {}, qint64 limit = -1);
    QString userMessage(const QString &appName) const;
};

QString formatLimit(qint64 bytes);
QString standardMessage(const QString &code, const QString &app, const QString &reason = {}, qint64 limit = -1);

// ---- Content (SPEC §5) -----------------------------------------------------

bool typeMatches(const QString &accept, const QString &offered);
bool contentMatches(const QString &accept, const QJsonObject &content);
bool acceptsContent(const QJsonArray &accepts, const QJsonObject &content);
bool acceptsType(const QJsonArray &accepts, const QString &offered);
QString fileKindForPath(const QString &path);
QJsonObject fileContent(const QString &path);  // file/<kind> or folder/reference
QJsonObject textContent(const QString &type, const QString &text, const QStringList &hints = {});

// ---- Wire (SPEC §4) --------------------------------------------------------

enum class Kind { Request, Notification, Response, Invalid };
// Parses and validates one line; `out` receives the object when valid.
Kind classify(const QByteArray &line, QJsonObject *out = nullptr);
int negotiate(const QList<int> &client, const QList<int> &server); // -1: none
QByteArray requestLine(qint64 id, const QString &method, const QJsonObject &params);
QByteArray notificationLine(const QString &method, const QJsonObject &params);
QByteArray responseLine(qint64 id, const QJsonValue &result);
QByteArray errorLine(qint64 id, const Error &error);

QString normalizeAccelerator(const QString &accelerator);
QString newToken();                            // 32 random bytes, hex

// ---- Manifests and registry (SPEC §3) --------------------------------------

// Validates schema >= 1 and a non-empty id; unknown fields are kept and ignored.
bool parseManifest(const QByteArray &json, QJsonObject *out, QString *error = nullptr);
// Atomic, only if changed (ignoring writtenAt). Returns true if rewritten.
bool writeManifest(const Locations &locations, QJsonObject manifest, QString *error = nullptr);
QString executablePath();                      // $APPIMAGE or the running binary
// Shown for this content: link enabled, available, on this OS, accepts it.
bool actionOffered(const QJsonObject &manifest, const QJsonObject &action, const QJsonObject &content);
bool actionUsable(const QJsonObject &manifest, const QJsonObject &action);

class Registry : public QObject {
    Q_OBJECT
public:
    explicit Registry(const Locations &locations, QObject *parent = nullptr);
    // Re-reads changed manifests (by mtime). Returns whether apps changed.
    bool refresh();
    // Watches the registry and runtime directories with QFileSystemWatcher.
    void watch();
    QList<QJsonObject> apps() const { return m_apps; }  // executable exists
    QJsonObject app(const QString &id) const;
    // "Used by Arcade Box" for an accelerator another app already uses.
    QString shortcutOwner(const QString &me, const QString &accelerator) const;
    const Locations &locations() const { return m_locations; }

signals:
    void changed();                            // apps or running state changed

private:
    struct Entry { qint64 mtime = 0; qint64 size = 0; QJsonObject manifest; bool valid = false; };
    Locations m_locations;
    QHash<QString, Entry> m_entries;
    QList<QJsonObject> m_apps;
    QFileSystemWatcher *m_watcher = nullptr;
};

// ---- Endpoint (SPEC §4.1) --------------------------------------------------

struct Endpoint {
    QList<int> protocol;
    QString transport;
    QString address;
    qint64 pid = 0;
    QString startedAt;
    QString token;
    static bool read(const Locations &locations, const QString &appId, Endpoint *out);
    bool write(const Locations &locations, const QString &appId) const;
};
QString socketAddress(const Locations &locations, const QString &appId);
QString nowRfc3339();

// ---- Server ----------------------------------------------------------------

class Server;

// One invoke's answer: done()/fail() once, or startJob() then progress()
// and finish()/finishError(). Copies share state; safe to keep and answer later.
class Responder {
public:
    void done(const QJsonObject &result) const;
    void fail(const Error &error) const;
    QString startJob() const;
    void progress(double fraction, const QString &message) const;
    void finish(const QJsonObject &result) const;
    void finishError(const Error &error) const;
    bool cancelled() const;
    QString peerId() const;

private:
    friend class Server;
    struct State;
    std::shared_ptr<State> d;
};

class Server : public QObject {
    Q_OBJECT
public:
    Server(const QString &appId, const QString &version, const Locations &locations, QObject *parent = nullptr);
    ~Server() override;

    std::function<QJsonArray()> describe;
    std::function<void(const QJsonObject &request, const Responder &responder)> invoke;
    std::function<QJsonObject()> status;
    std::function<bool()> activate;
    std::function<bool()> quit;

    // Optional methods are advertised only when their callback is assigned.
    // Callbacks run on the Server's event-loop thread; queue slow work to a
    // worker and queue the Responder answer back to this thread.
    using MethodHandler = std::function<void(const QJsonObject &, const Responder &)>;
    MethodHandler settings, menu, menuInvoke, shortcutsSet, settingsExport, settingsImport;
    // restartReady validates before the acknowledgement; restart runs after
    // it is flushed. The app owns successor launch and waiting for this PID.
    std::function<Error(const QString &mode)> restartReady;
    std::function<void(const QString &mode)> restart;
    std::function<QString()> trayState;         // ArcadeTrayHost::state(), if used
    QStringList methods() const;

    // Binds the endpoint. False (with `error`) if a live instance answers or binding fails.
    bool start(QString *error = nullptr);
    void stop();
    bool listening() const { return m_server.isListening(); }
    bool busy() const { return !m_jobs.isEmpty(); }
    void notifyChanged();
    void notifyMenuChanged();

private:
    void onConnection();
    void onReadable(QLocalSocket *socket);
    void handle(QLocalSocket *socket, const QJsonObject &message);
    friend class Responder;
    QString m_appId, m_version, m_token;
    Locations m_locations;
    QLocalServer m_server;
    QHash<QLocalSocket *, bool> m_authed;
    QHash<QLocalSocket *, QStringList> m_topics;
    QHash<QString, std::shared_ptr<std::atomic_bool>> m_jobs;
    QHash<QLocalSocket *, QStringList> m_socketJobs;
    qint64 m_nextJob = 0;
};

// ---- Client (blocking; worker threads only) --------------------------------

class Client {
public:
    ~Client();
    static std::unique_ptr<Client> connect(const Locations &locations, const QString &appId, const QString &myId,
                                           const QString &myVersion, int timeoutMs = HelloTimeoutMs, Error *error = nullptr);
    QJsonValue call(const QString &method, const QJsonObject &params, Error *error, int timeoutMs = 30000);
    bool invoke(const QJsonObject &request, QJsonObject *result, Error *error,
                const std::function<void(double, const QString &)> &progress = {},
                const std::atomic_bool *cancel = nullptr);
    QJsonObject describe(Error *error = nullptr);
    bool settings(Error *error = nullptr);
    bool restart(const QString &mode = {}, Error *error = nullptr);
    QJsonArray menu(Error *error = nullptr);
    bool menuInvoke(const QString &id, Error *error = nullptr);
    QJsonObject shortcutsSet(const QString &id, const QString &accelerator, Error *error = nullptr);
    QJsonArray settingsExport(Error *error = nullptr);
    QJsonObject settingsImport(const QJsonArray &inputs, Error *error = nullptr);
    // Optional subscriptions, read on a worker thread; no polling timer.
    bool subscribe(const QStringList &topics, Error *error = nullptr);
    bool nextNotification(QJsonObject *message, Error *error, int timeoutMs = -1);
    QString serverId() const { return m_serverId; }
    QString serverVersion() const { return m_serverVersion; }

private:
    Client() = default;
    bool readMessage(QJsonObject *out, int timeoutMs, Error *error);
    std::unique_ptr<QLocalSocket> m_socket;
    QByteArray m_buffer;
    qint64 m_nextId = 0;
    QString m_serverId, m_serverVersion;
    QList<QJsonObject> m_notifications;
};

// Lifecycle (SPEC §7): running instance, else one-shot for headless actions,
// else launch in the background and wait. `onLaunching` runs before a launch.
bool invokeAction(const Locations &locations, const QString &myId, const QString &myVersion,
                  const QJsonObject &manifest, const QJsonObject &request, QJsonObject *result, Error *error,
                  const std::function<void(double, const QString &)> &progress = {},
                  const std::atomic_bool *cancel = nullptr, const std::function<void()> &onLaunching = {});
// "Running", "Installed" or "NotInstalled"; `version` receives the running/installed version.
QString appState(const Locations &locations, const Registry &registry, const QString &appId,
                 const QString &myId, QString *version = nullptr);

} // namespace ArcadeLink
