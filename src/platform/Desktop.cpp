#include "Desktop.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLocalSocket>
#include <QMimeData>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QWindow>
#include <QXmlStreamWriter>
#include <functional>
#include <utility>

#ifdef ARCADE_SHELF_HAVE_DBUS
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#endif

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#ifdef Q_OS_MACOS
#include <Carbon/Carbon.h>
#endif
#if defined(ARCADE_SHELF_HAVE_X11) && __has_include(<xcb/xcb.h>)
#define ARCADE_SHELF_NATIVE_XCB_EVENTS 1
#include <xcb/xcb.h>
#endif

namespace {

using ProgramReply = std::function<void(bool, const QByteArray &)>;

// Only user actions and pending requests create a process/deadline. No timers
// run while idle. Arguments are passed separately, never through a shell.
void runProgram(QObject *context, const QString &program, const QStringList &arguments,
                ProgramReply reply, int timeoutMs = 1800)
{
    if (program.isEmpty()) { reply(false, {}); return; }
    auto *process = new QProcess(context);
    auto *deadline = new QTimer(process);
    deadline->setSingleShot(true);
    auto output = std::make_shared<QByteArray>();
    auto finished = std::make_shared<bool>(false);
    const auto finish = [process, deadline, output, finished, reply](bool ok) {
        if (std::exchange(*finished, true)) return;
        deadline->stop();
        output->append(process->readAllStandardOutput());
        if (output->size() > 2 * 1024 * 1024) { output->clear(); ok = false; }
        reply(ok, *output);
        process->deleteLater();
    };
    QObject::connect(process, &QProcess::readyReadStandardOutput, context, [process, output] {
        output->append(process->readAllStandardOutput());
        if (output->size() > 2 * 1024 * 1024) process->kill();
    });
    QObject::connect(process, &QProcess::readyReadStandardError, process,
                     [process] { process->readAllStandardError(); });
    QObject::connect(process, &QProcess::finished, context,
                     [finish](int code, QProcess::ExitStatus status) {
                         finish(code == 0 && status == QProcess::NormalExit);
                     });
    QObject::connect(process, &QProcess::errorOccurred, context,
                     [finish](QProcess::ProcessError error) {
                         if (error == QProcess::FailedToStart) finish(false);
                     });
    QObject::connect(deadline, &QTimer::timeout, process, [process, finish] {
        process->kill();
        finish(false);
    });
    deadline->start(timeoutMs);
    process->start(program, arguments);
}

bool isolatedDesktop()
{
    const QString platform = QGuiApplication::platformName();
    return platform == QStringLiteral("offscreen") || platform == QStringLiteral("minimal")
        || qEnvironmentVariableIntValue("ARCADE_SHELF_DISABLE_GLOBAL_SHORTCUT") == 1
        || qEnvironmentVariableIntValue("ARCADE_SHELF_TESTING") == 1
        || (!qEnvironmentVariableIsEmpty("ARCADE_HOME")
            && qEnvironmentVariableIntValue("ARCADE_SHELF_ALLOW_DESKTOP_IN_TESTS") != 1);
}

bool isWayland()
{
    return QGuiApplication::platformName().contains(QStringLiteral("wayland"), Qt::CaseInsensitive);
}

bool availablePath(const QString &path)
{
    if (path.isEmpty() || path.contains(QChar::Null) || !QDir::isAbsolutePath(path)) return false;
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}

QScreen *screenFor(QWindow *window, const QString &name = {}, const QRect &geometry = {})
{
    for (QScreen *screen : QGuiApplication::screens())
        if (!name.isEmpty() && screen->name() == name) return screen;
    if (geometry.isValid())
        if (QScreen *screen = QGuiApplication::screenAt(geometry.center())) return screen;
    if (window && window->screen()) return window->screen();
    return QGuiApplication::primaryScreen();
}

QString startupPath()
{
#ifdef Q_OS_WIN
    return QDir(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation))
        .filePath(QStringLiteral("Startup/Arcade Shelf.vbs"));
#elif defined(Q_OS_MACOS)
    return QDir::home().filePath(QStringLiteral("Library/LaunchAgents/arcade.shelf.plist"));
#else
    return QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
        .filePath(QStringLiteral("autostart/arcade.shelf.desktop"));
#endif
}

bool stableExecutable(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isAbsolute() || !info.isFile() || !info.isExecutable()
        || path.contains(QChar::Null) || path.contains(QLatin1Char('\n'))
        || path.contains(QLatin1Char('\r'))) return false;
    const QString resolved = info.canonicalFilePath();
    if (resolved.isEmpty()) return false;
    for (const QString &temporary : {QDir::tempPath(), QStringLiteral("/tmp"),
                                    QStringLiteral("/var/tmp"), QStringLiteral("/workspace")}) {
        if (!temporary.isEmpty() && (resolved == temporary || resolved.startsWith(temporary + QLatin1Char('/'))))
            return false;
    }
    const QStringList components = QDir::fromNativeSeparators(resolved).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &entry : components) {
        const QString component = entry.toLower();
        if (component == QStringLiteral("build") || component.startsWith(QStringLiteral("build-"))
            || component.startsWith(QStringLiteral("cmake-build-"))
            || component.startsWith(QStringLiteral(".mount_"))) return false;
    }
    return true;
}

QString quotedDesktopExec(QString text)
{
    text.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    text.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    text.replace(QStringLiteral("`"), QStringLiteral("\\`"));
    text.replace(QStringLiteral("$"), QStringLiteral("\\$"));
    text.replace(QStringLiteral("%"), QStringLiteral("%%"));
    return QLatin1Char('"') + text + QLatin1Char('"');
}

struct Shortcut {
    Qt::KeyboardModifiers modifiers;
    Qt::Key key = Qt::Key_unknown;
    QString nativeKey;
    QStringList nativeModifiers;
    int hyprMask = 0;
};

Shortcut parseShortcut(const QString &text)
{
    Shortcut result;
    QStringList components = text.split(QLatin1Char('+'));
    for (QString &component : components) {
        const QString token = component.trimmed().toLower();
        if (token == QStringLiteral("control")) component = QStringLiteral("Ctrl");
        else if (token == QStringLiteral("option")) component = QStringLiteral("Alt");
        else if (token == QStringLiteral("super") || token == QStringLiteral("win")) component = QStringLiteral("Meta");
        else if (token == QStringLiteral("cmd") || token == QStringLiteral("command")) {
#ifdef Q_OS_MACOS
            component = QStringLiteral("Ctrl"); // Qt's portable Control means Command on macOS.
#else
            component = QStringLiteral("Meta");
#endif
        }
    }
    const QKeySequence sequence = QKeySequence::fromString(components.join(QLatin1Char('+')), QKeySequence::PortableText);
    if (sequence.count() != 1 || sequence.isEmpty()) return result;
    result.key = sequence[0].key();
    result.modifiers = sequence[0].keyboardModifiers();
    if (result.modifiers.testFlag(Qt::ShiftModifier)) { result.nativeModifiers << QStringLiteral("SHIFT"); result.hyprMask |= 1; }
    if (result.modifiers.testFlag(Qt::ControlModifier)) { result.nativeModifiers << QStringLiteral("CTRL"); result.hyprMask |= 4; }
    if (result.modifiers.testFlag(Qt::AltModifier)) { result.nativeModifiers << QStringLiteral("ALT"); result.hyprMask |= 8; }
    if (result.modifiers.testFlag(Qt::MetaModifier)) { result.nativeModifiers << QStringLiteral("SUPER"); result.hyprMask |= 64; }
    if (result.key >= Qt::Key_A && result.key <= Qt::Key_Z) result.nativeKey = QChar(int(result.key));
    else if (result.key >= Qt::Key_0 && result.key <= Qt::Key_9) result.nativeKey = QChar(int(result.key));
    else if (result.key >= Qt::Key_F1 && result.key <= Qt::Key_F35)
        result.nativeKey = QStringLiteral("F%1").arg(int(result.key) - int(Qt::Key_F1) + 1);
    else {
        const QHash<int, QString> keys{{Qt::Key_Space, "space"}, {Qt::Key_Tab, "Tab"},
            {Qt::Key_Return, "Return"}, {Qt::Key_Enter, "Return"}, {Qt::Key_Escape, "Escape"},
            {Qt::Key_Home, "Home"}, {Qt::Key_End, "End"}, {Qt::Key_Left, "Left"},
            {Qt::Key_Right, "Right"}, {Qt::Key_Up, "Up"}, {Qt::Key_Down, "Down"},
            {Qt::Key_PageUp, "Prior"}, {Qt::Key_PageDown, "Next"}, {Qt::Key_Insert, "Insert"}};
        result.nativeKey = keys.value(int(result.key));
    }
    return result;
}

#ifdef ARCADE_SHELF_HAVE_DBUS
struct PortalShortcut {
    QString id;
    QVariantMap properties;
};
QDBusArgument &operator<<(QDBusArgument &argument, const PortalShortcut &shortcut)
{
    argument.beginStructure(); argument << shortcut.id << shortcut.properties; argument.endStructure();
    return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalShortcut &shortcut)
{
    argument.beginStructure(); argument >> shortcut.id >> shortcut.properties; argument.endStructure();
    return argument;
}
#endif
} // namespace

#ifdef ARCADE_SHELF_HAVE_DBUS
Q_DECLARE_METATYPE(PortalShortcut)
Q_DECLARE_METATYPE(QList<PortalShortcut>)

// Subscribe before starting the portal method: its Response may arrive before
// the method reply. Each request owns a bounded deadline and closes on cancel.
class ShelfPortalRequest final : public QObject {
    Q_OBJECT
public:
    using Completion = std::function<void(uint, const QVariantMap &)>;
    ShelfPortalRequest(QString path, QObject *parent, Completion completion)
        : QObject(parent), m_path(std::move(path)), m_completion(std::move(completion))
    {
        connectSignal();
        m_deadline.setSingleShot(true);
        QObject::connect(&m_deadline, &QTimer::timeout, this,
                         [this] { cancel(); finish(2, {{"timeout", true}}); });
        m_deadline.start(30000);
    }
    ~ShelfPortalRequest() override { disconnectSignal(); }
    void setPath(const QString &path)
    {
        if (path == m_path || !path.startsWith(QStringLiteral("/org/freedesktop/portal/desktop/request/"))) return;
        disconnectSignal(); m_path = path; connectSignal();
    }
    void cancel()
    {
        m_deadline.stop();
        QDBusMessage close = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),
            m_path, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Close"));
        QDBusConnection::sessionBus().asyncCall(close, 1000);
    }
    void fail() { cancel(); finish(2, {}); }
private slots:
    void response(uint code, const QVariantMap &results) { finish(code, results); }
private:
    void connectSignal()
    {
        QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.portal.Desktop"), m_path,
            QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"), this,
            SLOT(response(uint,QVariantMap)));
    }
    void disconnectSignal()
    {
        QDBusConnection::sessionBus().disconnect(QStringLiteral("org.freedesktop.portal.Desktop"), m_path,
            QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"), this,
            SLOT(response(uint,QVariantMap)));
    }
    void finish(uint code, const QVariantMap &results)
    {
        if (!m_completion) return;
        m_deadline.stop(); disconnectSignal();
        auto callback = std::exchange(m_completion, {});
        callback(code, results);
        deleteLater();
    }
    QString m_path;
    Completion m_completion;
    QTimer m_deadline;
};

class ShelfPortalSignals final : public QObject {
    Q_OBJECT
public:
    std::function<void(const QString &, const QString &)> activation;
    std::function<void(const QString &)> closed;
private slots:
    void activated(const QDBusObjectPath &session, const QString &shortcut,
                   qulonglong timestamp, const QVariantMap &options)
    { Q_UNUSED(timestamp) Q_UNUSED(options) if (activation) activation(session.path(), shortcut); }
    void sessionClosed(const QVariantMap &details, const QDBusMessage &message)
    { Q_UNUSED(details) if (closed) closed(message.path()); }
};
#endif

namespace Shelf {

struct Desktop::Native {
    QString error;
    QString shortcutStatus = QStringLiteral("No global shortcut");
    QString accelerator;
    QString executable;
    quint64 generation = 0;
    bool isolated = isolatedDesktop();
    QString hyprctl = isolated ? QString() : QStandardPaths::findExecutable(QStringLiteral("hyprctl"));
    QString hyprSignature = isolated ? QString() : qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    QString bindingToken = QStringLiteral("arcadeShelf_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    bool hyprBound = false;
    QString boundKey;
    QString boundModifiers;
    QString boundTarget;
    QLocalSocket *hyprEvents = nullptr;
    QByteArray hyprEventBuffer;
    QPointer<QWindow> movingWindow;
    QMetaObject::Connection movingDestroyed;
    quint64 moveGeneration = 0;
    WId movingNativeId = 0;
    bool moveLeftSurface = false;
#ifdef ARCADE_SHELF_HAVE_DBUS
    QString portalSession;
    bool portalRegistered = false;
    ShelfPortalSignals *portalSignals = nullptr;
#endif
#ifdef Q_OS_WIN
    bool hotkeyRegistered = false;
    int hotkeyId = 0x4153;
#endif
#ifdef Q_OS_MACOS
    EventHotKeyRef hotkey = nullptr;
    EventHandlerRef eventHandler = nullptr;
#endif
    bool hyprland() const { return !hyprctl.isEmpty() && !hyprSignature.isEmpty(); }
    QStringList hyprArguments(const QStringList &arguments) const
    { return QStringList{QStringLiteral("-i"), hyprSignature} + arguments; }
};

Desktop::Desktop(QObject *parent) : QObject(parent), d(std::make_unique<Native>())
{
    if (qGuiApp) qGuiApp->installNativeEventFilter(this);
#ifdef ARCADE_SHELF_HAVE_DBUS
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<QList<PortalShortcut>>();
    d->portalSignals = new ShelfPortalSignals;
    d->portalSignals->setParent(this);
    d->portalSignals->activation = [this](const QString &session, const QString &id) {
        if (session == d->portalSession && id == QStringLiteral("arcade-shelf-show")) emit activated();
    };
    d->portalSignals->closed = [this](const QString &session) {
        if (session != d->portalSession) return;
        d->portalSession.clear();
        setShortcutStatus(QStringLiteral("Desktop shortcut session closed; reapply the shortcut to reconnect"));
    };
    QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"), QStringLiteral("org.freedesktop.portal.GlobalShortcuts"),
        QStringLiteral("Activated"), d->portalSignals,
        SLOT(activated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
    auto *watcher = new QDBusServiceWatcher(QStringLiteral("org.freedesktop.portal.Desktop"),
        QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForOwnerChange, this);
    QObject::connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
        [this](const QString &, const QString &, const QString &newOwner) {
            d->portalSession.clear();
            d->portalRegistered = false;
            if (!newOwner.isEmpty() && !d->accelerator.isEmpty() && !d->isolated)
                startPortalShortcut(++d->generation);
            else if (!d->accelerator.isEmpty())
                setShortcutStatus(QStringLiteral("Desktop shortcut portal disconnected"));
        });
#endif
    if (d->hyprland()) {
        const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
        const QString socket = QDir(runtime).filePath(QStringLiteral("hypr/%1/.socket2.sock").arg(d->hyprSignature));
        d->hyprEvents = new QLocalSocket(this);
        QObject::connect(d->hyprEvents, &QLocalSocket::readyRead, this, [this] {
            d->hyprEventBuffer.append(d->hyprEvents->readAll());
            if (d->hyprEventBuffer.size() > 65536) { d->hyprEventBuffer.clear(); return; }
            qsizetype newline;
            while ((newline = d->hyprEventBuffer.indexOf('\n')) >= 0) {
                const QByteArray event = d->hyprEventBuffer.left(newline);
                d->hyprEventBuffer.remove(0, newline + 1);
                if (event.startsWith("configreloaded>>") && !d->accelerator.isEmpty()) {
                    d->hyprBound = false;
                    installHyprlandShortcut(d->generation);
                }
            }
        });
        d->hyprEvents->connectToServer(socket);
    }
}

Desktop::~Desktop()
{
    clearMove();
    clearShortcut();
    if (qGuiApp) qGuiApp->removeNativeEventFilter(this);
#ifdef Q_OS_MACOS
    if (d->eventHandler) RemoveEventHandler(d->eventHandler);
#endif
}

QString Desktop::shortcutStatus() const { return d->shortcutStatus; }
QString Desktop::lastError() const { return d->error; }
void Desktop::setError(const QString &message)
{
    if (d->error == message) return;
    d->error = message;
    emit errorChanged();
    if (!message.isEmpty()) emit errorOccurred(message);
}
void Desktop::setShortcutStatus(const QString &message)
{
    if (d->shortcutStatus == message) return;
    d->shortcutStatus = message;
    emit shortcutStatusChanged();
}

bool Desktop::move(QWindow *window)
{
    if (!window) return false;
    clearMove();
    d->movingWindow = window;
    d->movingNativeId = window->winId();
    const quint64 generation = d->moveGeneration;
    window->installEventFilter(this);
    d->movingDestroyed = QObject::connect(window, &QObject::destroyed, this,
                                          [this] { clearMove(); });
    // Call from the original pointer press/move; Wayland requires its serial.
    const bool moved = window->startSystemMove();
    if (!moved) {
        if (generation == d->moveGeneration) clearMove();
        setError(QStringLiteral("The desktop did not accept moving this window"));
    }
#ifdef Q_OS_MACOS
    // Cocoa's performWindowDragWithEvent runs the native gesture synchronously.
    // Observe the final frame on the next event turn, after Qt synchronizes it.
    else QTimer::singleShot(0, this, [this, generation] { completeMove(generation); });
#else
    Q_UNUSED(generation)
#endif
    return moved;
}

void Desktop::clearMove()
{
    ++d->moveGeneration;
    if (d->movingWindow) d->movingWindow->removeEventFilter(this);
    QObject::disconnect(d->movingDestroyed);
    d->movingDestroyed = {};
    d->movingWindow.clear();
    d->movingNativeId = 0;
    d->moveLeftSurface = false;
}

bool Desktop::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != d->movingWindow || !event) return false;
    switch (event->type()) {
    case QEvent::Hide:
    case QEvent::Close:
        clearMove();
        break;
    case QEvent::KeyPress:
        if (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) clearMove();
        break;
    case QEvent::MouseButtonPress:
        // A new gesture means any old compositor query can no longer describe
        // the original release. Never snap from a stale or canceled move.
        clearMove();
        break;
    case QEvent::Leave:
        if (isWayland() && d->hyprland()) d->moveLeftSurface = true;
        break;
    case QEvent::Enter:
        if (isWayland() && d->hyprland() && d->moveLeftSurface) {
            const quint64 generation = d->moveGeneration;
            d->moveLeftSurface = false;
            QTimer::singleShot(0, this, [this, generation] { completeMove(generation); });
        }
        break;
    default:
        break;
    }
    // QtWayland synthesizes MouseButtonRelease immediately when xdg move
    // starts. Do not use it as evidence that the native gesture has ended.
    return false;
}

bool Desktop::resize(QWindow *window, int edges)
{
    const auto flags = Qt::Edges(edges);
    if (!window || flags == Qt::Edges() || (edges & ~int(Qt::TopEdge | Qt::BottomEdge | Qt::LeftEdge | Qt::RightEdge))
        || (flags.testFlag(Qt::TopEdge) && flags.testFlag(Qt::BottomEdge))
        || (flags.testFlag(Qt::LeftEdge) && flags.testFlag(Qt::RightEdge))) return false;
    clearMove();
    const bool resized = window->startSystemResize(flags);
    if (!resized) setError(QStringLiteral("The desktop did not accept resizing this window"));
    return resized;
}

QRect Desktop::clampGeometry(const QRect &geometry, const QRect &available, const QSize &minimum)
{
    if (!available.isValid()) return geometry;
    const QSize size(qBound(qMin(minimum.width(), available.width()), geometry.width(), available.width()),
                     qBound(qMin(minimum.height(), available.height()), geometry.height(), available.height()));
    const QPoint position(qBound(available.left(), geometry.x(), available.right() - size.width() + 1),
                          qBound(available.top(), geometry.y(), available.bottom() - size.height() + 1));
    return QRect(position, size);
}

QString Desktop::nearestEdge(const QRect &geometry, const QRect &available, int distance)
{
    if (!geometry.isValid() || !available.isValid() || distance < 0) return {};
    const std::pair<QString, qint64> candidates[] = {
        {QStringLiteral("left"), qAbs(qint64(geometry.left()) - available.left())},
        {QStringLiteral("right"), qAbs(qint64(geometry.right()) - available.right())},
        {QStringLiteral("top"), qAbs(qint64(geometry.top()) - available.top())},
        {QStringLiteral("bottom"), qAbs(qint64(geometry.bottom()) - available.bottom())}};
    QString edge;
    qint64 closest = qint64(distance) + 1;
    for (const auto &candidate : candidates) {
        const bool vertical = candidate.first == QStringLiteral("left") || candidate.first == QStringLiteral("right");
        const bool overlaps = vertical
            ? qint64(geometry.bottom()) >= qint64(available.top()) - distance
                && qint64(geometry.top()) <= qint64(available.bottom()) + distance
            : qint64(geometry.right()) >= qint64(available.left()) - distance
                && qint64(geometry.left()) <= qint64(available.right()) + distance;
        if (!overlaps) continue;
        if (candidate.second < closest) { edge = candidate.first; closest = candidate.second; }
    }
    return edge;
}

QRect Desktop::snappedGeometry(const QRect &geometry, const QRect &available, const QString &edge)
{
    if (!geometry.isValid() || !available.isValid()) return geometry;
    if (edge != QStringLiteral("left") && edge != QStringLiteral("right")
        && edge != QStringLiteral("top") && edge != QStringLiteral("bottom")) return geometry;
    // Edge tabs can be smaller than the expanded panel's minimum dimensions.
    QRect result = clampGeometry(geometry, available, QSize(1, 1));
    if (edge == QStringLiteral("left")) result.moveLeft(available.left());
    else if (edge == QStringLiteral("right")) result.moveRight(available.right());
    else if (edge == QStringLiteral("top")) result.moveTop(available.top());
    else result.moveBottom(available.bottom());
    return result;
}

// Hyprland's clients query supplies the actual compositor address. Select only
// this process's main window; never use activewindow or caller-supplied IDs.
static void ownHyprlandWindow(QObject *context, const QString &hyprctl, const QStringList &prefix,
                              QWindow *window, std::function<void(const QJsonObject &)> callback,
                              std::function<void(const QString &)> failed)
{
    const QPointer<QWindow> guard(window);
    runProgram(context, hyprctl, prefix + QStringList{QStringLiteral("-j"), QStringLiteral("clients")},
        [guard, callback = std::move(callback), failed = std::move(failed)](bool ok, const QByteArray &output) {
            if (!guard) return;
            QJsonObject found;
            int matches = 0;
            if (ok) for (const auto &value : QJsonDocument::fromJson(output).array()) {
                const auto client = value.toObject();
                if (client.value("pid").toInteger() != QCoreApplication::applicationPid()
                    || client.value("title").toString() != guard->title()) continue;
                if (!QRegularExpression(QStringLiteral("^0x[0-9a-fA-F]+$")).match(client.value("address").toString()).hasMatch()) continue;
                found = client; ++matches;
            }
            if (matches != 1) { failed(QStringLiteral("Could not identify Shelf's mapped window in Hyprland")); return; }
            callback(found);
        });
}

void Desktop::completeMove(quint64 generation)
{
    if (generation != d->moveGeneration || !d->movingWindow) return;
    const QPointer<QWindow> guard = d->movingWindow;
    if (!guard->isVisible()) { clearMove(); return; }
    if (!isWayland()) {
        const QRect geometry = guard->geometry();
        QScreen *screen = screenFor(guard, {}, geometry);
        clearMove();
        if (geometry.isValid()) emit moveFinished(geometry, screen ? screen->name() : QString());
        return;
    }
    if (!d->hyprland()) { clearMove(); return; }
    ownHyprlandWindow(this, d->hyprctl, d->hyprArguments({}), guard,
        [this, guard, generation](const QJsonObject &client) {
            if (!guard || generation != d->moveGeneration || guard != d->movingWindow) return;
            const auto position = client.value("at").toArray();
            const auto size = client.value("size").toArray();
            if (position.size() != 2 || size.size() != 2) { clearMove(); return; }
            const QRect geometry(position[0].toInt(), position[1].toInt(),
                                 size[0].toInt(), size[1].toInt());
            QScreen *screen = screenFor(guard, {}, geometry);
            clearMove();
            if (geometry.isValid())
                emit moveFinished(geometry, screen ? screen->name() : QString());
        }, [this, generation](const QString &) {
            if (generation == d->moveGeneration) clearMove();
        });
}

void Desktop::restoreGeometry(QWindow *window, const QRect &geometry, const QString &screenName)
{
    if (!window || !geometry.isValid()) return;
    if (d->movingWindow == window) clearMove();
    QScreen *screen = screenFor(window, screenName, geometry);
    if (!screen) return;
    window->setScreen(screen);
    const QRect bounded = clampGeometry(geometry, screen->availableGeometry(), window->minimumSize());
    window->resize(bounded.size());
    if (!isWayland()) { window->setPosition(bounded.topLeft()); return; }
    if (!d->hyprland()) return; // xdg-shell delegates placement to compositor.
    const QPointer<QWindow> guard(window);
    const auto apply = [this, guard, bounded] {
        if (!guard) return;
        ownHyprlandWindow(this, d->hyprctl, d->hyprArguments({}), guard,
            [this, guard, bounded](const QJsonObject &client) {
                if (!guard) return;
                const QString address = client.value("address").toString();
                const QString batch = QStringLiteral("dispatch setfloating address:%1;dispatch resizewindowpixel exact %2 %3,address:%1;dispatch movewindowpixel exact %4 %5,address:%1")
                    .arg(address).arg(bounded.width()).arg(bounded.height()).arg(bounded.x()).arg(bounded.y());
                runProgram(this, d->hyprctl, d->hyprArguments({QStringLiteral("--batch"), batch}),
                    [this](bool ok, const QByteArray &) { if (!ok) setError(QStringLiteral("Hyprland did not restore Shelf's position")); });
            }, [this](const QString &error) { setError(error); });
    };
    if (window->isVisible()) { QTimer::singleShot(90, this, apply); return; }
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(window, &QWindow::visibleChanged, this,
        [this, connection, apply](bool visible) {
            if (!visible) return;
            QObject::disconnect(*connection);
            QTimer::singleShot(90, this, apply);
        });
}

void Desktop::dock(QWindow *window, const QString &edge)
{
    if (!window || !QStringList{QStringLiteral("left"), QStringLiteral("right"),
                              QStringLiteral("top"), QStringLiteral("bottom")}.contains(edge)) return;
    QScreen *screen = screenFor(window);
    if (!screen) return;
    QRect geometry = clampGeometry(window->geometry(), screen->availableGeometry(), window->minimumSize());
    const QRect available = screen->availableGeometry();
    if (edge == QStringLiteral("left")) geometry.moveLeft(available.left());
    else if (edge == QStringLiteral("right")) geometry.moveRight(available.right());
    else if (edge == QStringLiteral("top")) geometry.moveTop(available.top());
    else geometry.moveBottom(available.bottom());
    if (isWayland() && !d->hyprland()) {
        setError(QStringLiteral("This Wayland desktop manages window positions; use its native move command to place Shelf at an edge"));
        return;
    }
    restoreGeometry(window, geometry, screen->name());
}

void Desktop::captureGeometry(QWindow *window)
{
    if (!window) return;
    if (!isWayland()) {
        emit geometryObserved(window->geometry(), window->screen() ? window->screen()->name() : QString());
        return;
    }
    if (!d->hyprland() || !window->isVisible()) return;
    const QPointer<QWindow> guard(window);
    ownHyprlandWindow(this, d->hyprctl, d->hyprArguments({}), window,
        [this, guard](const QJsonObject &client) {
            if (!guard) return;
            const auto position = client.value("at").toArray();
            const auto size = client.value("size").toArray();
            if (position.size() != 2 || size.size() != 2) return;
            const QRect geometry(position[0].toInt(), position[1].toInt(), size[0].toInt(), size[1].toInt());
            if (!geometry.isValid()) return;
            QScreen *screen = screenFor(guard, {}, geometry);
            emit geometryObserved(geometry, screen ? screen->name() : QString());
        }, [](const QString &) { /* Window may have unmapped while observation was pending. */ });
}

void Desktop::setAlwaysOnTop(QWindow *window, bool enabled)
{
    if (!window) return;
    window->setFlag(Qt::WindowStaysOnTopHint, enabled);
    if (!isWayland()) return;
    if (!d->hyprland()) {
        if (enabled) setError(QStringLiteral("This Wayland desktop decides window stacking; always on top is a compositor preference"));
        return;
    }
    const QPointer<QWindow> guard(window);
    const auto apply = [this, guard, enabled] {
        if (!guard) return;
        ownHyprlandWindow(this, d->hyprctl, d->hyprArguments({}), guard,
            [this, enabled](const QJsonObject &client) {
                if (client.value("pinned").toBool() == enabled) return;
                const QString address = client.value("address").toString();
                const QString batch = QStringLiteral("dispatch setfloating address:%1;dispatch pin address:%1").arg(address);
                runProgram(this, d->hyprctl, d->hyprArguments({QStringLiteral("--batch"), batch}),
                    [this](bool ok, const QByteArray &) { if (!ok) setError(QStringLiteral("Hyprland did not change Shelf's pin state")); });
            }, [this](const QString &error) { setError(error); });
    };
    if (window->isVisible()) QTimer::singleShot(90, this, apply);
    else {
        auto connection = std::make_shared<QMetaObject::Connection>();
        *connection = QObject::connect(window, &QWindow::visibleChanged, this,
            [this, connection, apply](bool visible) {
                if (!visible) return;
                QObject::disconnect(*connection); QTimer::singleShot(90, this, apply);
            });
    }
}

bool Desktop::open(const QString &pathOrUrl)
{
    if (pathOrUrl.contains(QChar::Null)) { setError(QStringLiteral("Invalid path")); return false; }
    QUrl url;
    if (QDir::isAbsolutePath(pathOrUrl)) {
        if (!availablePath(pathOrUrl) || !QFileInfo::exists(pathOrUrl)) { setError(QStringLiteral("The original item is unavailable")); return false; }
        url = QUrl::fromLocalFile(pathOrUrl);
    } else {
        url = QUrl(pathOrUrl, QUrl::StrictMode);
        if (!url.isValid() || url.host().isEmpty()
            || (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http"))) {
            setError(QStringLiteral("Only local files and HTTP or HTTPS URLs can be opened")); return false;
        }
    }
    const bool opened = QDesktopServices::openUrl(url);
    if (!opened) setError(QStringLiteral("The desktop could not open this item"));
    return opened;
}

void Desktop::reveal(const QString &path)
{
    if (!availablePath(path)) { setError(QStringLiteral("The original item is unavailable")); return; }
#ifdef Q_OS_WIN
    const QString explorer = QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("explorer.exe"));
    runProgram(this, explorer, {QStringLiteral("/select,"), QDir::toNativeSeparators(path)},
        [this, path](bool ok, const QByteArray &) {
            if (!ok && !QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())))
                setError(QStringLiteral("Explorer could not reveal the original"));
        });
#elif defined(Q_OS_MACOS)
    runProgram(this, QStringLiteral("/usr/bin/open"), {QStringLiteral("-R"), path},
        [this](bool ok, const QByteArray &) { if (!ok) setError(QStringLiteral("Finder could not reveal the original")); });
#else
#ifdef ARCADE_SHELF_HAVE_DBUS
    QDBusMessage request = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"),
        QStringLiteral("/org/freedesktop/FileManager1"), QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"));
    request << QStringList{QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)} << QString();
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(request, 2000), this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, path] {
        if (watcher->reply().type() == QDBusMessage::ErrorMessage
            && !QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())))
            setError(QStringLiteral("The file manager could not reveal the original"));
        watcher->deleteLater();
    });
#else
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())))
        setError(QStringLiteral("The file manager could not reveal the original"));
#endif
#endif
}

QMimeData *Desktop::mimeDataForContents(const QJsonArray &contents)
{
    auto *mime = new QMimeData;
    QList<QUrl> urls;
    QStringList text;
    QStringList html;
    int count = 0;
    for (const auto &entry : contents) {
        const auto content = entry.toObject();
        const QString type = content.value("type").toString();
        if (type.startsWith(QStringLiteral("file/")) || type == QStringLiteral("folder/reference")) {
            QStringList paths;
            if (content.value("path").isString() && ++count <= 1000) paths << content.value("path").toString();
            for (const auto &value : content.value("paths").toArray()) {
                if (++count > 1000) break;
                if (value.isString()) paths << value.toString();
            }
            for (const QString &path : paths)
                if (availablePath(path)) {
                    const QUrl url = QUrl::fromLocalFile(path);
                    if (!urls.contains(url)) urls << url;
                }
        } else if (type == QStringLiteral("text/plain") || type == QStringLiteral("text/rich") || type == QStringLiteral("text/url")) {
            if (++count > 1000) break;
            QString plain = content.value("text").toString();
            if (plain.isEmpty() && availablePath(content.value("path").toString())) {
                const QString path = content.value("path").toString();
                QFile file(path);
                // Large staged snippets remain transferable as files. Only
                // bounded text is read synchronously when a drag actually starts.
                if (QFileInfo(path).size() <= 256 * 1024 && file.open(QIODevice::ReadOnly)) plain = QString::fromUtf8(file.read(256 * 1024));
                else urls << QUrl::fromLocalFile(path);
            }
            if (plain.toUtf8().size() <= 256 * 1024 && !plain.isEmpty()) text << plain;
            if (type == QStringLiteral("text/url")) {
                const QUrl url(plain, QUrl::StrictMode);
                if (url.isValid() && (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http"))) urls << url;
            }
            if (type == QStringLiteral("text/rich")) {
                const QString rich = content.value("html").toString();
                if (!rich.isEmpty() && rich.toUtf8().size() <= 256 * 1024) html << rich;
            }
        } else if (type == QStringLiteral("structured/shelf-reorder")) {
            const auto data = content.value("data").toObject();
            const auto ids = data.value("ids").toArray();
            const QString profile = data.value("profile").toString();
            bool valid = !profile.isEmpty() && profile.size() <= 128 && !ids.isEmpty() && ids.size() <= 1000;
            for (const auto &id : ids)
                valid = valid && id.isString() && !id.toString().isEmpty() && id.toString().size() <= 128;
            if (valid) mime->setData(QStringLiteral("application/x-arcade-shelf-items"),
                                    QJsonDocument(QJsonObject{{"profile", profile}, {"ids", ids}}).toJson(QJsonDocument::Compact));
        }
    }
    if (!urls.isEmpty()) mime->setUrls(urls);
    if (!text.isEmpty()) mime->setText(text.join(QLatin1Char('\n')));
    else if (!urls.isEmpty()) {
        QStringList paths;
        for (const QUrl &url : urls) paths << (url.isLocalFile() ? url.toLocalFile() : url.toString());
        mime->setText(paths.join(QLatin1Char('\n')));
    }
    if (!html.isEmpty()) mime->setHtml(html.join(QStringLiteral("<br>")));
    return mime;
}

void Desktop::startDrag(QWindow *window, const QJsonArray &contents)
{
    if (!window || contents.isEmpty()) return;
    std::unique_ptr<QMimeData> mime(mimeDataForContents(contents));
    if (mime->formats().isEmpty()) { setError(QStringLiteral("No available items to drag")); return; }
    auto *drag = new QDrag(window);
    drag->setMimeData(mime.release());
    auto action = std::make_shared<int>(int(Qt::IgnoreAction));
    QObject::connect(drag, &QDrag::actionChanged, this,
                     [action](Qt::DropAction changed) { *action = int(changed); });
    QObject::connect(drag, &QObject::destroyed, this,
                     [this, action] { emit dragFinished(*action); });
    // Copy-only is intentional: dragging out must never move or remove an
    // original file. Native Wayland/X11/OLE/Cocoa handle the actual transfer.
    *action = int(drag->exec(Qt::CopyAction, Qt::CopyAction));
    // Qt's X11 platform can return before the receiver fetches MIME data.
    // QDrag must remain platform-owned on the heap; deleting it here would
    // erase the selection before another process finishes reading it.
}

QString Desktop::validateShortcut(const QString &accelerator)
{
    if (accelerator.trimmed().isEmpty()) return {};
    const auto shortcut = parseShortcut(accelerator);
    if (shortcut.key == Qt::Key_unknown || shortcut.nativeKey.isEmpty())
        return QStringLiteral("Choose one letter, number, function key or navigation key");
    if (shortcut.modifiers.testFlag(Qt::KeypadModifier)
        || shortcut.modifiers.testFlag(Qt::GroupSwitchModifier)) return QStringLiteral("This shortcut modifier is not supported");
    const auto mods = shortcut.modifiers;
    const bool clipboard = ((mods == Qt::ControlModifier || mods == Qt::MetaModifier)
        && (shortcut.key == Qt::Key_C || shortcut.key == Qt::Key_V || shortcut.key == Qt::Key_X))
        || (shortcut.key == Qt::Key_Insert && (mods == Qt::ControlModifier || mods == Qt::ShiftModifier));
    if (clipboard) return QStringLiteral("Standard clipboard shortcuts cannot be used globally");
    if (mods == Qt::NoModifier && !(shortcut.key >= Qt::Key_F1 && shortcut.key <= Qt::Key_F35))
        return QStringLiteral("Use a modifier with a letter or navigation key");
    return {};
}

void Desktop::configureShortcut(const QString &accelerator, const QString &executable)
{
    clearShortcut();
    d->accelerator = accelerator.trimmed();
    d->executable = executable;
    const quint64 generation = ++d->generation;
    d->bindingToken = QStringLiteral("arcadeShelf_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    if (d->accelerator.isEmpty()) { setShortcutStatus(QStringLiteral("No global shortcut")); return; }
    const QString error = validateShortcut(d->accelerator);
    if (!error.isEmpty()) { setShortcutStatus(error); return; }
    if (d->isolated) { setShortcutStatus(QStringLiteral("Global shortcuts are disabled in this isolated instance")); return; }
#ifdef Q_OS_WIN
    const auto shortcut = parseShortcut(d->accelerator);
    UINT modifiers = MOD_NOREPEAT;
    if (shortcut.modifiers.testFlag(Qt::ControlModifier)) modifiers |= MOD_CONTROL;
    if (shortcut.modifiers.testFlag(Qt::AltModifier)) modifiers |= MOD_ALT;
    if (shortcut.modifiers.testFlag(Qt::ShiftModifier)) modifiers |= MOD_SHIFT;
    if (shortcut.modifiers.testFlag(Qt::MetaModifier)) modifiers |= MOD_WIN;
    UINT key = 0;
    if ((shortcut.key >= Qt::Key_A && shortcut.key <= Qt::Key_Z) || (shortcut.key >= Qt::Key_0 && shortcut.key <= Qt::Key_9)) key = UINT(shortcut.key);
    else if (shortcut.key >= Qt::Key_F1 && shortcut.key <= Qt::Key_F24) key = VK_F1 + UINT(shortcut.key - Qt::Key_F1);
    else {
        const QHash<int, UINT> keys{{Qt::Key_Space, VK_SPACE}, {Qt::Key_Tab, VK_TAB}, {Qt::Key_Return, VK_RETURN},
            {Qt::Key_Enter, VK_RETURN}, {Qt::Key_Escape, VK_ESCAPE}, {Qt::Key_Home, VK_HOME}, {Qt::Key_End, VK_END},
            {Qt::Key_Left, VK_LEFT}, {Qt::Key_Right, VK_RIGHT}, {Qt::Key_Up, VK_UP}, {Qt::Key_Down, VK_DOWN},
            {Qt::Key_PageUp, VK_PRIOR}, {Qt::Key_PageDown, VK_NEXT}, {Qt::Key_Insert, VK_INSERT}};
        key = keys.value(int(shortcut.key));
    }
    d->hotkeyRegistered = key && RegisterHotKey(nullptr, d->hotkeyId, modifiers, key);
    setShortcutStatus(d->hotkeyRegistered ? QStringLiteral("Ready · %1").arg(d->accelerator)
                                         : QStringLiteral("This shortcut is unavailable or already used by another application"));
#elif defined(Q_OS_MACOS)
    const auto shortcut = parseShortcut(d->accelerator);
    const QHash<int, UInt32> keys{{Qt::Key_A, kVK_ANSI_A}, {Qt::Key_B, kVK_ANSI_B}, {Qt::Key_C, kVK_ANSI_C},
        {Qt::Key_D, kVK_ANSI_D}, {Qt::Key_E, kVK_ANSI_E}, {Qt::Key_F, kVK_ANSI_F}, {Qt::Key_G, kVK_ANSI_G},
        {Qt::Key_H, kVK_ANSI_H}, {Qt::Key_I, kVK_ANSI_I}, {Qt::Key_J, kVK_ANSI_J}, {Qt::Key_K, kVK_ANSI_K},
        {Qt::Key_L, kVK_ANSI_L}, {Qt::Key_M, kVK_ANSI_M}, {Qt::Key_N, kVK_ANSI_N}, {Qt::Key_O, kVK_ANSI_O},
        {Qt::Key_P, kVK_ANSI_P}, {Qt::Key_Q, kVK_ANSI_Q}, {Qt::Key_R, kVK_ANSI_R}, {Qt::Key_S, kVK_ANSI_S},
        {Qt::Key_T, kVK_ANSI_T}, {Qt::Key_U, kVK_ANSI_U}, {Qt::Key_V, kVK_ANSI_V}, {Qt::Key_W, kVK_ANSI_W},
        {Qt::Key_X, kVK_ANSI_X}, {Qt::Key_Y, kVK_ANSI_Y}, {Qt::Key_Z, kVK_ANSI_Z},
        {Qt::Key_0, kVK_ANSI_0}, {Qt::Key_1, kVK_ANSI_1}, {Qt::Key_2, kVK_ANSI_2}, {Qt::Key_3, kVK_ANSI_3},
        {Qt::Key_4, kVK_ANSI_4}, {Qt::Key_5, kVK_ANSI_5}, {Qt::Key_6, kVK_ANSI_6}, {Qt::Key_7, kVK_ANSI_7},
        {Qt::Key_8, kVK_ANSI_8}, {Qt::Key_9, kVK_ANSI_9}, {Qt::Key_F1, kVK_F1}, {Qt::Key_F2, kVK_F2},
        {Qt::Key_F3, kVK_F3}, {Qt::Key_F4, kVK_F4}, {Qt::Key_F5, kVK_F5}, {Qt::Key_F6, kVK_F6},
        {Qt::Key_F7, kVK_F7}, {Qt::Key_F8, kVK_F8}, {Qt::Key_F9, kVK_F9}, {Qt::Key_F10, kVK_F10},
        {Qt::Key_F11, kVK_F11}, {Qt::Key_F12, kVK_F12}, {Qt::Key_F13, kVK_F13}, {Qt::Key_F14, kVK_F14},
        {Qt::Key_F15, kVK_F15}, {Qt::Key_F16, kVK_F16}, {Qt::Key_F17, kVK_F17}, {Qt::Key_F18, kVK_F18},
        {Qt::Key_F19, kVK_F19}, {Qt::Key_F20, kVK_F20}, {Qt::Key_Space, kVK_Space}, {Qt::Key_Tab, kVK_Tab},
        {Qt::Key_Return, kVK_Return}, {Qt::Key_Enter, kVK_ANSI_KeypadEnter}, {Qt::Key_Escape, kVK_Escape},
        {Qt::Key_Home, kVK_Home}, {Qt::Key_End, kVK_End}, {Qt::Key_Left, kVK_LeftArrow}, {Qt::Key_Right, kVK_RightArrow},
        {Qt::Key_Up, kVK_UpArrow}, {Qt::Key_Down, kVK_DownArrow}, {Qt::Key_PageUp, kVK_PageUp}, {Qt::Key_PageDown, kVK_PageDown}};
    UInt32 modifiers = 0;
    if (shortcut.modifiers.testFlag(Qt::ControlModifier)) modifiers |= cmdKey;
    if (shortcut.modifiers.testFlag(Qt::AltModifier)) modifiers |= optionKey;
    if (shortcut.modifiers.testFlag(Qt::ShiftModifier)) modifiers |= shiftKey;
    if (shortcut.modifiers.testFlag(Qt::MetaModifier)) modifiers |= controlKey;
    if (!d->eventHandler) {
        const EventTypeSpec type{kEventClassKeyboard, kEventHotKeyPressed};
        InstallEventHandler(GetApplicationEventTarget(), [](EventHandlerCallRef, EventRef event, void *context) -> OSStatus {
            EventHotKeyID id{};
            if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr,
                                  sizeof(id), nullptr, &id) != noErr || id.signature != 0x41534846 || id.id != 1) return eventNotHandledErr;
            emit static_cast<Desktop *>(context)->activated();
            return noErr;
        }, 1, &type, this, &d->eventHandler);
    }
    const auto status = keys.contains(int(shortcut.key))
        ? RegisterEventHotKey(keys.value(int(shortcut.key)), modifiers, EventHotKeyID{0x41534846, 1},
                              GetApplicationEventTarget(), 0, &d->hotkey) : paramErr;
    setShortcutStatus(status == noErr ? QStringLiteral("Ready · %1").arg(d->accelerator)
                                      : QStringLiteral("This shortcut is unavailable or already used by another application"));
#else
    Q_UNUSED(generation)
    startPortalShortcut(generation);
#endif
}

bool Desktop::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType) Q_UNUSED(result)
#ifdef Q_OS_WIN
    const auto *event = static_cast<MSG *>(message);
    if (event && d->movingWindow && event->hwnd == reinterpret_cast<HWND>(d->movingNativeId)) {
        if (event->message == WM_KEYDOWN && event->wParam == VK_ESCAPE) clearMove();
        else if (event->message == WM_EXITSIZEMOVE) {
            const quint64 generation = d->moveGeneration;
            QTimer::singleShot(0, this, [this, generation] { completeMove(generation); });
        }
    }
    if (event && event->message == WM_HOTKEY && event->wParam == WPARAM(d->hotkeyId)) {
        emit activated(); return true;
    }
#elif defined(ARCADE_SHELF_NATIVE_XCB_EVENTS)
    if (eventType == QByteArrayLiteral("xcb_generic_event_t") && message && d->movingWindow) {
        const auto *event = static_cast<xcb_generic_event_t *>(message);
        if ((event->response_type & 0x7f) == XCB_ENTER_NOTIFY) {
            const auto *crossing = reinterpret_cast<const xcb_enter_notify_event_t *>(event);
            if (crossing->event == d->movingNativeId && crossing->mode == XCB_NOTIFY_MODE_UNGRAB) {
                const quint64 generation = d->moveGeneration;
                QTimer::singleShot(0, this, [this, generation] { completeMove(generation); });
            }
        }
    }
#else
    Q_UNUSED(message)
#endif
    return false;
}

void Desktop::clearShortcut()
{
    ++d->generation;
#ifdef Q_OS_WIN
    if (d->hotkeyRegistered) UnregisterHotKey(nullptr, d->hotkeyId);
    d->hotkeyRegistered = false;
#endif
#ifdef Q_OS_MACOS
    if (d->hotkey) UnregisterEventHotKey(d->hotkey);
    d->hotkey = nullptr;
#endif
#ifdef ARCADE_SHELF_HAVE_DBUS
    for (ShelfPortalRequest *request : findChildren<ShelfPortalRequest *>()) request->fail();
    if (!d->portalSession.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(QStringLiteral("org.freedesktop.portal.Desktop"),
            d->portalSession, QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Closed"),
            d->portalSignals, SLOT(sessionClosed(QVariantMap,QDBusMessage)));
        QDBusMessage close = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),
            std::exchange(d->portalSession, {}), QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Close"));
        QDBusConnection::sessionBus().asyncCall(close, 1000);
    }
#endif
    if (d->hyprBound) {
        // A unique Lua handle removes this exact binding. It never unbinds a
        // chord broadly and cannot remove a later user binding on that key.
        const QString command = QStringLiteral("if _G.%1 then pcall(function() _G.%1:remove() end); _G.%1=nil end").arg(d->bindingToken);
        QProcess::startDetached(d->hyprctl, d->hyprArguments({QStringLiteral("eval"), command}));
        d->hyprBound = false;
    }
}

void Desktop::startPortalShortcut(quint64 generation)
{
#ifndef ARCADE_SHELF_HAVE_DBUS
    Q_UNUSED(generation)
    setShortcutStatus(QStringLiteral("Global shortcut portal support was not included in this build"));
#else
    if (generation != d->generation || d->isolated || d->accelerator.isEmpty()) return;
    if (!QDBusConnection::sessionBus().isConnected()) {
        setShortcutStatus(QStringLiteral("A desktop session bus is required for global shortcuts")); return;
    }
    setShortcutStatus(QStringLiteral("Connecting desktop shortcut…"));
    const auto create = [this, generation] {
        if (generation != d->generation) return;
        const QString token = QStringLiteral("arcade_shelf_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
        QString sender = QDBusConnection::sessionBus().baseService();
        sender.remove(QLatin1Char(':')); sender.replace(QLatin1Char('.'), QLatin1Char('_'));
        const QString predicted = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
        auto *request = new ShelfPortalRequest(predicted, this,
            [this, generation](uint response, const QVariantMap &results) {
                if (generation != d->generation) return;
                if (response != 0) {
                    setShortcutStatus(results.value("timeout").toBool() ? QStringLiteral("Desktop shortcut request timed out")
                        : QStringLiteral("Desktop shortcut session was declined; bind arcade-shelf --toggle in your desktop settings")); return;
                }
                const QVariant handle = results.value(QStringLiteral("session_handle"));
                const QString session = handle.canConvert<QDBusObjectPath>() ? handle.value<QDBusObjectPath>().path() : handle.toString();
                if (!session.startsWith(QStringLiteral("/org/freedesktop/portal/desktop/session/"))) {
                    setShortcutStatus(QStringLiteral("Desktop portal returned an invalid shortcut session")); return;
                }
                d->portalSession = session;
                QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.portal.Desktop"), session,
                    QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Closed"),
                    d->portalSignals, SLOT(sessionClosed(QVariantMap,QDBusMessage)));
                const QString bindToken = QStringLiteral("arcade_shelf_bind_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
                QString sender = QDBusConnection::sessionBus().baseService();
                sender.remove(QLatin1Char(':')); sender.replace(QLatin1Char('.'), QLatin1Char('_'));
                auto *binding = new ShelfPortalRequest(QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, bindToken), this,
                    [this, generation](uint code, const QVariantMap &) {
                        if (generation != d->generation) return;
                        if (code != 0) { clearShortcut(); setShortcutStatus(QStringLiteral("Desktop shortcut was not granted")); return; }
                        setShortcutStatus(QStringLiteral("Ready · %1").arg(d->accelerator));
                        if (d->hyprland()) installHyprlandShortcut(generation);
                    });
                const auto parsed = parseShortcut(d->accelerator);
                QStringList modifiers = parsed.nativeModifiers;
                modifiers.replaceInStrings(QStringLiteral("SUPER"), QStringLiteral("LOGO"));
                const QString preferred = (modifiers.isEmpty() ? QString() : modifiers.join(QLatin1Char('+')) + QLatin1Char('+')) + parsed.nativeKey;
                const QList<PortalShortcut> shortcuts{{QStringLiteral("arcade-shelf-show"),
                    {{QStringLiteral("description"), QStringLiteral("Show Arcade Shelf")}, {QStringLiteral("preferred_trigger"), preferred}}}};
                QDBusMessage bind = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),
                    QStringLiteral("/org/freedesktop/portal/desktop"), QStringLiteral("org.freedesktop.portal.GlobalShortcuts"), QStringLiteral("BindShortcuts"));
                bind << QDBusObjectPath(session) << QVariant::fromValue(shortcuts) << QString()
                     << QVariantMap{{QStringLiteral("handle_token"), bindToken}};
                auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(bind, 5000), this);
                const QPointer<ShelfPortalRequest> guard(binding);
                QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, guard] {
                    const auto reply = watcher->reply(); watcher->deleteLater();
                    if (!guard) return;
                    if (reply.type() == QDBusMessage::ErrorMessage) { guard->fail(); return; }
                    if (!reply.arguments().isEmpty()) guard->setPath(reply.arguments().first().value<QDBusObjectPath>().path());
                });
            });
        QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),
            QStringLiteral("/org/freedesktop/portal/desktop"), QStringLiteral("org.freedesktop.portal.GlobalShortcuts"), QStringLiteral("CreateSession"));
        message << QVariantMap{{QStringLiteral("handle_token"), token},
                               {QStringLiteral("session_handle_token"), token + QStringLiteral("_session")}};
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 5000), this);
        const QPointer<ShelfPortalRequest> guard(request);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, guard] {
            const auto reply = watcher->reply(); watcher->deleteLater();
            if (!guard) return;
            if (reply.type() == QDBusMessage::ErrorMessage) { guard->fail(); return; }
            if (!reply.arguments().isEmpty()) guard->setPath(reply.arguments().first().value<QDBusObjectPath>().path());
        });
    };
    if (d->portalRegistered) { create(); return; }
    QDBusMessage registration = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"), QStringLiteral("org.freedesktop.host.portal.Registry"), QStringLiteral("Register"));
    registration << QStringLiteral("arcade.shelf") << QVariantMap{};
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(registration, 2500), this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation, create] {
        const auto reply = watcher->reply(); watcher->deleteLater();
        if (generation != d->generation) return;
        if (reply.type() == QDBusMessage::ErrorMessage
            && reply.errorName() != QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod")
            && reply.errorName() != QStringLiteral("org.freedesktop.DBus.Error.UnknownInterface")) {
            setShortcutStatus(QStringLiteral("Desktop identity registration failed; install arcade.shelf.desktop or bind arcade-shelf --toggle manually")); return;
        }
        d->portalRegistered = true;
        create();
    });
#endif
}

void Desktop::installHyprlandShortcut(quint64 generation)
{
#ifdef ARCADE_SHELF_HAVE_DBUS
    if (generation != d->generation || !d->hyprland() || d->portalSession.isEmpty()) return;
    const auto parsed = parseShortcut(d->accelerator);
    runProgram(this, d->hyprctl, d->hyprArguments({QStringLiteral("-j"), QStringLiteral("globalshortcuts")}),
        [this, generation, parsed](bool ok, const QByteArray &output) {
            if (generation != d->generation) return;
            QStringList candidates;
            if (ok) for (const auto &value : QJsonDocument::fromJson(output).array()) {
                const QString name = value.toObject().value("name").toString();
                if (name.endsWith(QStringLiteral(":arcade-shelf-show"))) candidates << name;
            }
            candidates.removeDuplicates();
            const QString expected = QStringLiteral("arcade.shelf:arcade-shelf-show");
            const QString target = candidates.contains(expected) ? expected : (candidates.size() == 1 ? candidates.first() : QString());
            if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_.:-]+$")).match(target).hasMatch()) {
                setShortcutStatus(QStringLiteral("Portal connected; Hyprland has not published Shelf's shortcut target")); return;
            }
            runProgram(this, d->hyprctl, d->hyprArguments({QStringLiteral("-j"), QStringLiteral("binds")}),
                [this, generation, parsed, target](bool read, const QByteArray &bindings) {
                    if (generation != d->generation) return;
                    if (!read || !QJsonDocument::fromJson(bindings).isArray()) {
                        setShortcutStatus(QStringLiteral("Could not check desktop shortcut collisions")); return;
                    }
                    bool owned = false;
                    for (const auto &value : QJsonDocument::fromJson(bindings).array()) {
                        const auto bind = value.toObject();
                        if (bind.value("modmask").toInt() != parsed.hyprMask
                            || bind.value("key").toString().compare(parsed.nativeKey, Qt::CaseInsensitive) != 0) continue;
                        if (bind.value("description").toString() == d->bindingToken) owned = true;
                        else if (bind.value("dispatcher").toString() == QStringLiteral("global") && bind.value("arg").toString() == target) owned = true;
                        else { setShortcutStatus(QStringLiteral("Used by a desktop shortcut; choose another accelerator")); return; }
                    }
                    if (owned) { setShortcutStatus(QStringLiteral("Ready · %1").arg(d->accelerator)); return; }
                    const QString chord = parsed.nativeModifiers.isEmpty() ? parsed.nativeKey
                        : parsed.nativeModifiers.join(QStringLiteral(" + ")) + QStringLiteral(" + ") + parsed.nativeKey;
                    const QString lua = QStringLiteral("if _G.%1 then pcall(function() _G.%1:remove() end) end; _G.%1=hl.bind(\"%2\",hl.dsp.global(\"%3\"),{description=\"%1\"})")
                        .arg(d->bindingToken, chord, target);
                    runProgram(this, d->hyprctl, d->hyprArguments({QStringLiteral("eval"), lua}),
                        [this, generation](bool installed, const QByteArray &reply) {
                            if (generation != d->generation) return;
                            if (installed && reply.trimmed() == QByteArrayLiteral("ok")) {
                                d->hyprBound = true;
                                setShortcutStatus(QStringLiteral("Ready · %1").arg(d->accelerator));
                            } else setShortcutStatus(QStringLiteral("Portal connected; on older Hyprland bind its global Shelf target in compositor settings"));
                        });
                });
        });
#else
    Q_UNUSED(generation)
#endif
}

bool Desktop::startAtLogin() const
{
    const QFileInfo file(startupPath());
    if (!file.isFile() || file.isSymLink()) return false;
    QFile entry(file.filePath());
    return entry.open(QIODevice::ReadOnly) && entry.read(65536).contains("Arcade Shelf managed login entry");
}

bool Desktop::setStartAtLogin(bool enabled, const QString &executable)
{
    if (d->isolated) { setError(QStringLiteral("Login changes are disabled in isolated instances")); return false; }
    const QString target = startupPath();
    const QFileInfo existing(target);
    if (existing.isSymLink()) { setError(QStringLiteral("The login entry is a symlink; Shelf will not replace it")); return false; }
    if (existing.exists()) {
        QFile file(target);
        if (!file.open(QIODevice::ReadOnly) || !file.read(65536).contains("Arcade Shelf managed login entry")) {
            setError(QStringLiteral("An unrelated login entry already uses Shelf's name")); return false;
        }
    }
    if (!enabled) {
        if (!existing.exists() || QFile::remove(target)) return true;
        setError(QStringLiteral("Could not remove Shelf's login entry")); return false;
    }
    if (!stableExecutable(executable)) {
        setError(QStringLiteral("Install Shelf in a permanent location before enabling start at login")); return false;
    }
    if (!QDir().mkpath(existing.absolutePath())) { setError(QStringLiteral("Could not create the per-user login directory")); return false; }
    QByteArray bytes;
#ifdef Q_OS_WIN
    QString command = executable;
    command.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    bytes = (QStringLiteral("' Arcade Shelf managed login entry\r\nSet shell = CreateObject(\"WScript.Shell\")\r\n")
        + QStringLiteral("shell.Run Chr(34) & \"%1\" & Chr(34) & \" --background\", 0, False\r\n").arg(command)).toUtf8();
#elif defined(Q_OS_MACOS)
    QXmlStreamWriter xml(&bytes);
    xml.setAutoFormatting(true); xml.writeStartDocument();
    xml.writeDTD(QStringLiteral("<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">"));
    xml.writeComment(QStringLiteral("Arcade Shelf managed login entry"));
    xml.writeStartElement(QStringLiteral("plist")); xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
    xml.writeStartElement(QStringLiteral("dict"));
    xml.writeTextElement(QStringLiteral("key"), QStringLiteral("Label")); xml.writeTextElement(QStringLiteral("string"), QStringLiteral("arcade.shelf"));
    xml.writeTextElement(QStringLiteral("key"), QStringLiteral("ProgramArguments")); xml.writeStartElement(QStringLiteral("array"));
    xml.writeTextElement(QStringLiteral("string"), executable); xml.writeTextElement(QStringLiteral("string"), QStringLiteral("--background")); xml.writeEndElement();
    xml.writeTextElement(QStringLiteral("key"), QStringLiteral("RunAtLoad")); xml.writeEmptyElement(QStringLiteral("true"));
    xml.writeEndElement(); xml.writeEndElement(); xml.writeEndDocument();
#else
    bytes = (QStringLiteral("# Arcade Shelf managed login entry\n[Desktop Entry]\nType=Application\nName=Arcade Shelf\n")
        + QStringLiteral("Exec=%1 --background\nTerminal=false\nX-GNOME-Autostart-enabled=true\n").arg(quotedDesktopExec(executable))).toUtf8();
#endif
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setError(QStringLiteral("Could not save Shelf's login entry")); return false;
    }
    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

} // namespace Shelf

#ifdef ARCADE_SHELF_HAVE_DBUS
#include "Desktop.moc"
#endif
