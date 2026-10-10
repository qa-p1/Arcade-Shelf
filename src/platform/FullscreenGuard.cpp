#include "FullscreenGuard.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QPlatformSurfaceEvent>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QWindow>
#include <algorithm>
#include <functional>
#include <map>
#include <utility>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#endif

namespace Shelf {

bool FullscreenGuard::coversScreen(const QRect &window, const QRect &screen, int tolerance)
{
    if (window.width() <= 0 || window.height() <= 0 || screen.width() <= 0 || screen.height() <= 0) return false;
    const qint64 extra = qBound(0, tolerance, 8);
    return qint64(window.x()) <= qint64(screen.x()) + extra
        && qint64(window.y()) <= qint64(screen.y()) + extra
        && qint64(window.x()) + window.width() >= qint64(screen.x()) + screen.width() - extra
        && qint64(window.y()) + window.height() >= qint64(screen.y()) + screen.height() - extra;
}

bool FullscreenGuard::hyprlandFullscreen(const QJsonArray &monitors, const QJsonArray &clients,
                                       const QString &screenName, qint64 ownPid, bool *monitorMatched)
{
    if (monitorMatched) *monitorMatched = false;
    if (screenName.isEmpty() || monitors.size() > 256 || clients.size() > 10000) return false;
    for (const auto &value : monitors) {
        const auto monitor = value.toObject();
        if (monitor["name"].toString() != screenName || !monitor["id"].isDouble()) continue;
        if (monitorMatched) *monitorMatched = true;
        const int id = monitor["id"].toInt(-1);
        const auto active = monitor["activeWorkspace"].toObject();
        const auto special = monitor["specialWorkspace"].toObject();
        if (id < 0) return false;
        const auto sameWorkspace = [](const QJsonObject &workspace, const QJsonObject &target, bool isSpecial) {
            const QString address = target["address"].toString();
            if (!address.isEmpty() && workspace["address"].toString() == address) return true;
            return target["id"].isDouble() && workspace["id"].isDouble()
                && (!isSpecial || target["id"].toInt() != 0)
                && target["id"].toInt() == workspace["id"].toInt();
        };
        for (const auto &clientValue : clients) {
            const auto client = clientValue.toObject();
            // Hyprland fullscreen mode 1 is maximized; mode 2 is fullscreen.
            if (!client["fullscreen"].isDouble() || client["fullscreen"].toInt() != 2
                || !client["monitor"].isDouble() || client["monitor"].toInt(-1) != id
                || !client["pid"].isDouble() || client["pid"].toInteger() <= 0 || client["pid"].toInteger() == ownPid
                || (client.contains("mapped") && (!client["mapped"].isBool() || !client["mapped"].toBool()))
                || (client.contains("hidden") && (!client["hidden"].isBool() || client["hidden"].toBool()))) continue;
            // Current Hyprland supplies authoritative visibility and optional
            // numbered workspace IDs. Named workspaces use address strings.
            if (client.contains("visible")) {
                if (client["visible"].isBool() && client["visible"].toBool()) return true;
                continue;
            }
            const auto workspace = client["workspace"].toObject();
            if (sameWorkspace(workspace, active, false) || sameWorkspace(workspace, special, true)) return true;
        }
        return false;
    }
    return false;
}

struct FullscreenGuard::Native {
    explicit Native(FullscreenGuard *guard) : owner(guard) {}
    ~Native() { stop(); }
    FullscreenGuard *owner;
    bool watching = false;
    bool queued = false;
    quint64 generation = 0;

#ifdef Q_OS_WIN
    HWINEVENTHOOK foregroundHook = nullptr, locationHook = nullptr;
    HMONITOR shelfMonitor = nullptr;
    static std::map<HWINEVENTHOOK, Native *> &hooks()
    {
        static std::map<HWINEVENTHOOK, Native *> owners;
        return owners;
    }
    static void CALLBACK event(HWINEVENTHOOK hook, DWORD kind, HWND window,
                               LONG object, LONG child, DWORD, DWORD)
    {
        const auto it = hooks().find(hook);
        if (it == hooks().end()) return;
        if (kind == EVENT_SYSTEM_FOREGROUND
            || (kind == EVENT_OBJECT_LOCATIONCHANGE && object == OBJID_WINDOW
                && child == CHILDID_SELF && window == GetForegroundWindow())) it->second->schedule();
    }
    void start()
    {
        if (watching) { schedule(); return; }
        foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
            event, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        locationHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr,
            event, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (!foregroundHook || !locationHook) {
            stop(); owner->setState(false, QStringLiteral("Windows fullscreen observation unavailable")); return;
        }
        hooks()[foregroundHook] = hooks()[locationHook] = this;
        watching = true; schedule();
    }
    void evaluate()
    {
        QWindow *shelf = owner->m_window;
        if (!watching || !shelf) return;
        // A suppressed Qt surface can be unmapped or recreated. Retain the
        // last monitor so foreground changes can still restore the capsule.
        if (shelf->handle()) {
            const HWND handle = reinterpret_cast<HWND>(shelf->winId());
            if (IsWindow(handle)) shelfMonitor = MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST);
        }
        HWND foreground = GetForegroundWindow();
        if (foreground) foreground = GetAncestor(foreground, GA_ROOTOWNER);
        DWORD pid = 0; if (foreground) GetWindowThreadProcessId(foreground, &pid);
        wchar_t className[128]{};
        if (foreground) GetClassNameW(foreground, className, 128);
        const QString classString = QString::fromWCharArray(className);
        if (!foreground || pid == GetCurrentProcessId() || !IsWindowVisible(foreground)
            || IsIconic(foreground) || IsZoomed(foreground)
            || classString == QStringLiteral("Progman") || classString == QStringLiteral("WorkerW")
            || classString.startsWith(QStringLiteral("Shell_"))) {
            owner->setState(false, QStringLiteral("Windows fullscreen observation active")); return;
        }
        MONITORINFO info{}; info.cbSize = sizeof(info);
        RECT bounds{};
        if (!shelfMonitor || !GetMonitorInfoW(shelfMonitor, &info)) {
            owner->setState(false, QStringLiteral("Shelf monitor unavailable")); return;
        }
        if (FAILED(DwmGetWindowAttribute(foreground, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds)))
            && !GetWindowRect(foreground, &bounds)) {
            owner->setState(false, QStringLiteral("Foreground geometry unavailable")); return;
        }
        const QRect window(bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top);
        const QRect screen(info.rcMonitor.left, info.rcMonitor.top,
                           info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top);
        owner->setState(FullscreenGuard::coversScreen(window, screen), QStringLiteral("Windows fullscreen observation active"));
    }
    void stop()
    {
        ++generation; watching = false; queued = false;
        for (HWINEVENTHOOK hook : {foregroundHook, locationHook})
            if (hook) { hooks().erase(hook); UnhookWinEvent(hook); }
        foregroundHook = locationHook = nullptr;
        shelfMonitor = nullptr;
    }
#elif !defined(Q_OS_MACOS)
    QLocalSocket *events = nullptr;
    QByteArray eventBuffer;
    QString executable, signature, socketPath;
    std::vector<QPointer<QProcess>> requests;
    bool querying = false, queryAgain = false;
    struct Snapshot {
        QJsonArray monitors, clients;
        int pending = 2;
        bool valid = true;
    };

    void start()
    {
        const QString platform = QGuiApplication::platformName();
        if (!platform.contains(QStringLiteral("wayland"), Qt::CaseInsensitive)) {
            owner->setState(false, QStringLiteral("Fullscreen stacking is controlled by this window manager")); return;
        }
        signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
        if (signature.size() > 128 || !QRegularExpression(QStringLiteral("^[A-Za-z0-9_.-]+$")).match(signature).hasMatch()) {
            owner->setState(false, QStringLiteral("This Wayland compositor does not expose other apps' fullscreen state")); return;
        }
        executable = QStandardPaths::findExecutable(QStringLiteral("hyprctl"));
        if (executable.isEmpty()) { owner->setState(false, QStringLiteral("Hyprland fullscreen observation requires hyprctl")); return; }
        if (watching) {
            if (events && events->state() == QLocalSocket::UnconnectedState) events->connectToServer(socketPath);
            if (events && events->state() == QLocalSocket::ConnectedState) schedule();
            return;
        }
        QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
        if (runtime.isEmpty()) runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        socketPath = QDir(runtime).filePath(QStringLiteral("hypr/%1/.socket2.sock").arg(signature));
        watching = true;
        events = new QLocalSocket(owner);
        QObject::connect(events, &QLocalSocket::connected, owner, [this] { schedule(); });
        QObject::connect(events, &QLocalSocket::readyRead, owner, [this] {
            eventBuffer.append(events->readAll());
            if (eventBuffer.size() > 65536) { eventBuffer.clear(); schedule(); return; }
            bool relevant = false;
            for (qsizetype newline; (newline = eventBuffer.indexOf('\n')) >= 0;) {
                const QByteArray event = eventBuffer.left(newline);
                eventBuffer.remove(0, newline + 1);
                const QByteArray name = event.left(event.indexOf(">>"));
                relevant |= name == "fullscreen" || name.startsWith("activewindow")
                    || name.startsWith("workspace") || name.startsWith("focusedmon")
                    || name.startsWith("monitor") || name.startsWith("movewindow")
                    || name.startsWith("moveworkspace") || name == "closewindow"
                    || name == "openwindow" || name == "configreloaded";
            }
            if (relevant) schedule();
        });
        QObject::connect(events, &QLocalSocket::errorOccurred, owner, [this](QLocalSocket::LocalSocketError) {
            owner->setState(false, QStringLiteral("Hyprland fullscreen event stream unavailable; retry when Shelf is shown"));
        });
        QObject::connect(events, &QLocalSocket::disconnected, owner, [this] {
            eventBuffer.clear(); owner->setState(false, QStringLiteral("Hyprland fullscreen event stream disconnected"));
        });
        events->connectToServer(socketPath);
    }

    void runQuery(const QString &command, std::function<void(bool, QJsonArray)> completion)
    {
        const quint64 requestGeneration = generation;
        auto *process = new QProcess(owner);
        requests.erase(std::remove_if(requests.begin(), requests.end(), [](const auto &request) { return request.isNull(); }), requests.end());
        requests.emplace_back(process);
        auto *deadline = new QTimer(process); deadline->setSingleShot(true);
        auto output = std::make_shared<QByteArray>();
        auto finished = std::make_shared<bool>(false);
        const auto finish = [this, process, deadline, output, finished, requestGeneration, completion](bool ok) {
            if (std::exchange(*finished, true)) return;
            deadline->stop(); output->append(process->readAllStandardOutput());
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(*output, &error);
            ok &= output->size() <= 2 * 1024 * 1024 && error.error == QJsonParseError::NoError && document.isArray();
            if (watching && generation == requestGeneration) completion(ok, document.array());
            process->deleteLater();
        };
        QObject::connect(process, &QProcess::readyReadStandardOutput, owner, [process, output] {
            output->append(process->readAllStandardOutput());
            if (output->size() > 2 * 1024 * 1024) process->kill();
        });
        QObject::connect(process, &QProcess::readyReadStandardError, process, [process] { process->readAllStandardError(); });
        QObject::connect(process, &QProcess::finished, owner, [finish](int code, QProcess::ExitStatus status) {
            finish(code == 0 && status == QProcess::NormalExit);
        });
        QObject::connect(process, &QProcess::errorOccurred, owner, [finish](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) finish(false);
        });
        QObject::connect(deadline, &QTimer::timeout, process, [process, finish] { process->kill(); finish(false); });
        deadline->start(1800);
        process->start(executable, {QStringLiteral("-i"), signature, QStringLiteral("-j"), command});
    }

    void evaluate()
    {
        if (!watching || !owner->m_window || !owner->m_window->screen()) return;
        if (!events || events->state() != QLocalSocket::ConnectedState) {
            owner->setState(false, QStringLiteral("Hyprland fullscreen event stream unavailable")); return;
        }
        if (querying) { queryAgain = true; return; }
        querying = true;
        const auto snapshot = std::make_shared<Snapshot>();
        const auto complete = [this, snapshot] {
            if (--snapshot->pending) return;
            querying = false;
            if (!snapshot->valid || !owner->m_window || !owner->m_window->screen()
                || !events || events->state() != QLocalSocket::ConnectedState)
                owner->setState(false, QStringLiteral("Hyprland fullscreen snapshot unavailable"));
            else {
                bool matched = false;
                const bool fullscreen = FullscreenGuard::hyprlandFullscreen(snapshot->monitors, snapshot->clients,
                    owner->m_window->screen()->name(), QCoreApplication::applicationPid(), &matched);
                owner->setState(fullscreen, matched ? QStringLiteral("Hyprland fullscreen observation active")
                                                   : QStringLiteral("Hyprland monitor could not be matched to Shelf's screen"));
            }
            if (std::exchange(queryAgain, false)) schedule();
        };
        runQuery(QStringLiteral("monitors"), [snapshot, complete](bool ok, QJsonArray value) {
            snapshot->valid &= ok; snapshot->monitors = std::move(value); complete();
        });
        runQuery(QStringLiteral("clients"), [snapshot, complete](bool ok, QJsonArray value) {
            snapshot->valid &= ok; snapshot->clients = std::move(value); complete();
        });
    }

    void stop()
    {
        ++generation; watching = false; queued = querying = queryAgain = false;
        if (events) { QObject::disconnect(events, nullptr, owner, nullptr); events->abort(); events->deleteLater(); events = nullptr; }
        eventBuffer.clear();
        for (auto process : requests) if (process) {
            QObject::disconnect(process, nullptr, owner, nullptr);
            for (auto *deadline : process->findChildren<QTimer *>()) {
                deadline->stop(); QObject::disconnect(deadline, nullptr, process, nullptr);
            }
            process->kill(); process->deleteLater();
        }
        requests.clear();
    }
#else
    void start()
    {
        // Ordinary Qt::Window remains in its desktop Space. Unlike Wheel's
        // transient overlay, Shelf never opts into FullScreenAuxiliary or
        // CanJoinAllSpaces; no screen-recording permission is needed.
        owner->setState(false, QStringLiteral("macOS Spaces control fullscreen stacking"));
    }
    void evaluate() {}
    void stop() { ++generation; watching = false; queued = false; }
#endif
    void schedule()
    {
        if (queued || !watching) return;
        queued = true;
        const quint64 requestGeneration = generation;
        QTimer::singleShot(0, owner, [this, requestGeneration] {
            if (generation != requestGeneration) return;
            queued = false;
            if (watching) evaluate();
        });
    }
};

FullscreenGuard::FullscreenGuard(QObject *parent) : QObject(parent), d(std::make_unique<Native>(this)) {}

FullscreenGuard::~FullscreenGuard()
{
    if (m_window) m_window->removeEventFilter(this);
    d->stop();
}

void FullscreenGuard::attachWindow(QWindow *window)
{
    if (m_window == window) { update(); return; }
    if (m_window) m_window->removeEventFilter(this);
    for (const auto &connection : m_connections) QObject::disconnect(connection);
    m_connections.clear(); d->stop(); m_window = window;
    if (window) {
        window->installEventFilter(this);
        m_connections.push_back(connect(window, &QWindow::screenChanged, this, [this] { update(); }));
        m_connections.push_back(connect(window, &QWindow::xChanged, this, [this] { d->schedule(); }));
        m_connections.push_back(connect(window, &QWindow::yChanged, this, [this] { d->schedule(); }));
        m_connections.push_back(connect(window, &QObject::destroyed, this, [this] {
            d->stop(); m_window = nullptr; setState(false, QStringLiteral("No desktop surface"));
        }));
    }
    update();
}

void FullscreenGuard::setCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed) return;
    m_collapsed = collapsed; update();
}

void FullscreenGuard::setRequestedVisible(bool visible)
{
    if (m_requestedVisible == visible) return;
    m_requestedVisible = visible; update();
}

void FullscreenGuard::setState(bool suppressed, const QString &status)
{
    suppressed &= m_collapsed && m_requestedVisible;
    if (m_suppressed == suppressed && m_status == status) return;
    m_suppressed = suppressed; m_status = status; emit changed();
}

void FullscreenGuard::update()
{
    const QString platform = QGuiApplication::platformName();
    const bool isolated = platform == QStringLiteral("offscreen") || platform == QStringLiteral("minimal")
        || qEnvironmentVariableIntValue("ARCADE_SHELF_TESTING") == 1
        || qEnvironmentVariableIntValue("ARCADE_SHELF_DISABLE_FULLSCREEN_GUARD") == 1
        || (!qEnvironmentVariableIsEmpty("ARCADE_HOME")
            && qEnvironmentVariableIntValue("ARCADE_SHELF_ALLOW_DESKTOP_IN_TESTS") != 1);
    if (!m_window || !m_requestedVisible || !m_collapsed || isolated) {
        d->stop(); setState(false, isolated ? QStringLiteral("Fullscreen observation disabled for this isolated session")
                                          : QStringLiteral("Fullscreen observation paused")); return;
    }
    d->start();
}

bool FullscreenGuard::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window && event->type() == QEvent::PlatformSurface) {
        const auto *surface = static_cast<QPlatformSurfaceEvent *>(event);
        if (surface->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
            // Wayland may destroy its wl_surface when QML hides the capsule.
            // Clearing suppression here would remap it immediately, detect
            // the same fullscreen app and enter a hide/show loop. Keep the
            // event stream alive until fullscreen exit; detach stops it.
            if (!(m_suppressed && m_requestedVisible && m_collapsed)) {
                d->stop(); setState(false, QStringLiteral("No desktop surface"));
            }
        } else QTimer::singleShot(0, this, [this] { update(); });
    }
    return QObject::eventFilter(watched, event);
}

} // namespace Shelf
