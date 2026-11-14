#include "DesktopPrivate.h"
#include "Identity.h"

#include <QDir>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QProcess>
#include <QRegularExpression>
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

namespace {

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
    const QString canonical = ArcadeLink::normalizeAccelerator(text);
    QStringList components = canonical.split(QLatin1Char('+'));
    for (QString &component : components) {
        const QString token = component.trimmed().toLower();
        if (token == QStringLiteral("ctrl")) {
#ifdef Q_OS_MACOS
            component = QStringLiteral("Meta"); // Qt Meta is the physical Control key on macOS.
#else
            component = QStringLiteral("Ctrl");
#endif
        } else if (token == QStringLiteral("option")) component = QStringLiteral("Alt");
        else if (token == QStringLiteral("super") || token == QStringLiteral("win")) {
#ifdef Q_OS_MACOS
            component = QStringLiteral("Ctrl"); // Qt Control is the physical Command key on macOS.
#else
            component = QStringLiteral("Meta");
#endif
        } else if (token == QStringLiteral("cmd") || token == QStringLiteral("command")) {
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
    if (result.modifiers.testFlag(Qt::ShiftModifier)) {
        result.nativeModifiers << QStringLiteral("SHIFT");
        result.hyprMask |= 1;
    }
    if (result.modifiers.testFlag(Qt::ControlModifier)) {
        result.nativeModifiers << QStringLiteral("CTRL");
        result.hyprMask |= 4;
    }
    if (result.modifiers.testFlag(Qt::AltModifier)) {
        result.nativeModifiers << QStringLiteral("ALT");
        result.hyprMask |= 8;
    }
    if (result.modifiers.testFlag(Qt::MetaModifier)) {
        result.nativeModifiers << QStringLiteral("SUPER");
        result.hyprMask |= 64;
    }
    if (result.key >= Qt::Key_A && result.key <= Qt::Key_Z) result.nativeKey = QChar(int(result.key));
    else if (result.key >= Qt::Key_0 && result.key <= Qt::Key_9) result.nativeKey = QChar(int(result.key));
    else if (result.key >= Qt::Key_F1 && result.key <= Qt::Key_F35)
        result.nativeKey = QStringLiteral("F%1").arg(int(result.key) - int(Qt::Key_F1) + 1);
    else {
        const QHash<int, QString> keys{{Qt::Key_Space, "space"}, {Qt::Key_Tab, "Tab"}, {Qt::Key_Return, "Return"}, {Qt::Key_Enter, "Return"}, {Qt::Key_Escape, "Escape"}, {Qt::Key_Home, "Home"}, {Qt::Key_End, "End"}, {Qt::Key_Left, "Left"}, {Qt::Key_Right, "Right"}, {Qt::Key_Up, "Up"}, {Qt::Key_Down, "Down"}, {Qt::Key_PageUp, "Prior"}, {Qt::Key_PageDown, "Next"}, {Qt::Key_Insert, "Insert"}};
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
    argument.beginStructure();
    argument << shortcut.id << shortcut.properties;
    argument.endStructure();
    return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalShortcut &shortcut)
{
    argument.beginStructure();
    argument >> shortcut.id >> shortcut.properties;
    argument.endStructure();
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
        disconnectSignal();
        m_path = path;
        connectSignal();
    }
    void cancel()
    {
        m_deadline.stop();
        QDBusMessage close = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),
                                                            m_path, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Close"));
        QDBusConnection::sessionBus().asyncCall(close, 1000);
    }
    void fail()
    {
        cancel();
        finish(2, {});
    }
private slots:
    void response(uint code, const QVariantMap &results) { finish(code, results); }

private:
    void connectSignal()
    {
        QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.portal.Desktop"), m_path,
                                              QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"), this,
                                              SLOT(response(uint, QVariantMap)));
    }
    void disconnectSignal()
    {
        QDBusConnection::sessionBus().disconnect(QStringLiteral("org.freedesktop.portal.Desktop"), m_path,
                                                 QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"), this,
                                                 SLOT(response(uint, QVariantMap)));
    }
    void finish(uint code, const QVariantMap &results)
    {
        if (!m_completion) return;
        m_deadline.stop();
        disconnectSignal();
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
    {
        Q_UNUSED(timestamp)
        Q_UNUSED(options)
        if (activation) activation(session.path(), shortcut);
    }
    void sessionClosed(const QVariantMap &details, const QDBusMessage &message)
    {
        Q_UNUSED(details)
        if (closed) closed(message.path());
    }
};
#endif

namespace Shelf {

using desktop::runProgram;

// The portal's Activated and session Closed signals, and a restart of the
// portal service, reach the shortcut through here (from the constructor).
void Desktop::watchShortcutPortal()
{
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
                                          SLOT(activated(QDBusObjectPath, QString, qulonglong, QVariantMap)));
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
}

QString Desktop::shortcutStatus() const { return d->shortcutStatus; }
void Desktop::setShortcutStatus(const QString &message)
{
    const bool changed = d->shortcutStatus != message;
    d->shortcutStatus = message;
    if (changed) emit shortcutStatusChanged();
    if (!m_shortcutReply || message.startsWith(QStringLiteral("Connecting desktop shortcut"))) return;
    auto reply = std::exchange(m_shortcutReply, {});
    const bool applied = message.startsWith(QStringLiteral("Ready"));
#ifdef Q_OS_LINUX
    QJsonObject result{{"applied", applied}, {"via", applied ? (d->hyprBound ? "hyprland-runtime" : "portal") : "manual"}};
    if (!applied) result.insert("hint", message + QStringLiteral(". Bind arcade-shelf --toggle in your desktop keyboard settings."));
    reply(result, {});
#else
    if (applied) reply({{"applied", true}, {"via", "native"}}, {});
    else if (d->isolated) reply({{"applied", false}, {"via", "manual"}, {"hint", message}}, {});
    else reply({}, ArcadeLink::Error::make("denied", message));
#endif
}

QString Desktop::validateShortcut(const QString &accelerator)
{
    if (accelerator.trimmed().isEmpty()) return {};
    const auto shortcut = parseShortcut(accelerator);
    if (shortcut.key == Qt::Key_unknown || shortcut.nativeKey.isEmpty())
        return QStringLiteral("Choose one letter, number, function key or navigation key");
    if (shortcut.modifiers.testFlag(Qt::KeypadModifier) || shortcut.modifiers.testFlag(Qt::GroupSwitchModifier)) return QStringLiteral("This shortcut modifier is not supported");
    const auto mods = shortcut.modifiers;
#ifdef Q_OS_MACOS
    const auto physicalControl = Qt::MetaModifier; // "Ctrl" parses to Qt Meta on macOS.
#else
    const auto physicalControl = Qt::ControlModifier;
#endif
    const bool clipboard = ((mods == Qt::ControlModifier || mods == Qt::MetaModifier) && (shortcut.key == Qt::Key_C || shortcut.key == Qt::Key_V || shortcut.key == Qt::Key_X)) || (shortcut.key == Qt::Key_Insert && (mods == physicalControl || mods == Qt::ShiftModifier));
    if (clipboard) return QStringLiteral("Couldn't use this shortcut. Copy, cut and paste keys can't be global; choose another.");
    if (mods == Qt::NoModifier && !(shortcut.key >= Qt::Key_F1 && shortcut.key <= Qt::Key_F35))
        return QStringLiteral("Use a modifier with a letter or navigation key");
    return {};
}

void Desktop::rebindShortcut(const QString &accelerator, const QString &executable,
                             std::function<void(QJsonObject, ArcadeLink::Error)> reply)
{
    const QString canonical = ArcadeLink::normalizeAccelerator(accelerator);
    const QString error = canonical.isEmpty() ? QStringLiteral("Invalid accelerator.") : validateShortcut(canonical);
    if (!error.isEmpty()) {
        reply({}, ArcadeLink::Error::make("denied", error));
        return;
    }
    if (m_shortcutReply) {
        reply({}, ArcadeLink::Error::make("busy", "A shortcut change is in progress."));
        return;
    }
    const QString previous = d->accelerator;
    m_shortcutReply = [this, previous, executable, reply = std::move(reply)](QJsonObject result, ArcadeLink::Error failure) {
        if (failure.isError()) configureShortcut(previous, executable);
        reply(result, failure);
    };
    configureShortcut(canonical, executable);
}
void Desktop::configureShortcut(const QString &accelerator, const QString &executable)
{
    clearShortcut();
    d->accelerator = accelerator.trimmed();
    d->executable = executable;
    [[maybe_unused]] const quint64 generation = ++d->generation; // used where a portal or Hyprland binds the shortcut
    d->bindingToken = QStringLiteral("arcadeShelf_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    if (d->accelerator.isEmpty()) {
        setShortcutStatus(QStringLiteral("No global shortcut"));
        return;
    }
    const QString error = validateShortcut(d->accelerator);
    if (!error.isEmpty()) {
        setShortcutStatus(error);
        return;
    }
    if (d->isolated) {
        setShortcutStatus(QStringLiteral("Global shortcuts are disabled in this isolated instance"));
        return;
    }
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
        const QHash<int, UINT> keys{{Qt::Key_Space, VK_SPACE}, {Qt::Key_Tab, VK_TAB}, {Qt::Key_Return, VK_RETURN}, {Qt::Key_Enter, VK_RETURN}, {Qt::Key_Escape, VK_ESCAPE}, {Qt::Key_Home, VK_HOME}, {Qt::Key_End, VK_END}, {Qt::Key_Left, VK_LEFT}, {Qt::Key_Right, VK_RIGHT}, {Qt::Key_Up, VK_UP}, {Qt::Key_Down, VK_DOWN}, {Qt::Key_PageUp, VK_PRIOR}, {Qt::Key_PageDown, VK_NEXT}, {Qt::Key_Insert, VK_INSERT}};
        key = keys.value(int(shortcut.key));
    }
    d->hotkeyRegistered = key && RegisterHotKey(nullptr, d->hotkeyId, modifiers, key);
    setShortcutStatus(d->hotkeyRegistered ? QStringLiteral("Ready · %1").arg(d->accelerator)
                                          : QStringLiteral("This shortcut is unavailable or already used by another application"));
#elif defined(Q_OS_MACOS)
    const auto shortcut = parseShortcut(d->accelerator);
    const QHash<int, UInt32> keys{{Qt::Key_A, kVK_ANSI_A}, {Qt::Key_B, kVK_ANSI_B}, {Qt::Key_C, kVK_ANSI_C}, {Qt::Key_D, kVK_ANSI_D}, {Qt::Key_E, kVK_ANSI_E}, {Qt::Key_F, kVK_ANSI_F}, {Qt::Key_G, kVK_ANSI_G}, {Qt::Key_H, kVK_ANSI_H}, {Qt::Key_I, kVK_ANSI_I}, {Qt::Key_J, kVK_ANSI_J}, {Qt::Key_K, kVK_ANSI_K}, {Qt::Key_L, kVK_ANSI_L}, {Qt::Key_M, kVK_ANSI_M}, {Qt::Key_N, kVK_ANSI_N}, {Qt::Key_O, kVK_ANSI_O}, {Qt::Key_P, kVK_ANSI_P}, {Qt::Key_Q, kVK_ANSI_Q}, {Qt::Key_R, kVK_ANSI_R}, {Qt::Key_S, kVK_ANSI_S}, {Qt::Key_T, kVK_ANSI_T}, {Qt::Key_U, kVK_ANSI_U}, {Qt::Key_V, kVK_ANSI_V}, {Qt::Key_W, kVK_ANSI_W}, {Qt::Key_X, kVK_ANSI_X}, {Qt::Key_Y, kVK_ANSI_Y}, {Qt::Key_Z, kVK_ANSI_Z}, {Qt::Key_0, kVK_ANSI_0}, {Qt::Key_1, kVK_ANSI_1}, {Qt::Key_2, kVK_ANSI_2}, {Qt::Key_3, kVK_ANSI_3}, {Qt::Key_4, kVK_ANSI_4}, {Qt::Key_5, kVK_ANSI_5}, {Qt::Key_6, kVK_ANSI_6}, {Qt::Key_7, kVK_ANSI_7}, {Qt::Key_8, kVK_ANSI_8}, {Qt::Key_9, kVK_ANSI_9}, {Qt::Key_F1, kVK_F1}, {Qt::Key_F2, kVK_F2}, {Qt::Key_F3, kVK_F3}, {Qt::Key_F4, kVK_F4}, {Qt::Key_F5, kVK_F5}, {Qt::Key_F6, kVK_F6}, {Qt::Key_F7, kVK_F7}, {Qt::Key_F8, kVK_F8}, {Qt::Key_F9, kVK_F9}, {Qt::Key_F10, kVK_F10}, {Qt::Key_F11, kVK_F11}, {Qt::Key_F12, kVK_F12}, {Qt::Key_F13, kVK_F13}, {Qt::Key_F14, kVK_F14}, {Qt::Key_F15, kVK_F15}, {Qt::Key_F16, kVK_F16}, {Qt::Key_F17, kVK_F17}, {Qt::Key_F18, kVK_F18}, {Qt::Key_F19, kVK_F19}, {Qt::Key_F20, kVK_F20}, {Qt::Key_Space, kVK_Space}, {Qt::Key_Tab, kVK_Tab}, {Qt::Key_Return, kVK_Return}, {Qt::Key_Enter, kVK_ANSI_KeypadEnter}, {Qt::Key_Escape, kVK_Escape}, {Qt::Key_Home, kVK_Home}, {Qt::Key_End, kVK_End}, {Qt::Key_Left, kVK_LeftArrow}, {Qt::Key_Right, kVK_RightArrow}, {Qt::Key_Up, kVK_UpArrow}, {Qt::Key_Down, kVK_DownArrow}, {Qt::Key_PageUp, kVK_PageUp}, {Qt::Key_PageDown, kVK_PageDown}};
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
            return noErr; }, 1, &type, this, &d->eventHandler);
    }
    const auto status = keys.contains(int(shortcut.key))
                            ? RegisterEventHotKey(keys.value(int(shortcut.key)), modifiers, EventHotKeyID{0x41534846, 1},
                                                  GetApplicationEventTarget(), 0, &d->hotkey)
                            : paramErr;
    setShortcutStatus(status == noErr ? QStringLiteral("Ready · %1").arg(d->accelerator)
                                      : QStringLiteral("This shortcut is unavailable or already used by another application"));
#else
    Q_UNUSED(generation)
    startPortalShortcut(generation);
#endif
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
                                                 d->portalSignals, SLOT(sessionClosed(QVariantMap, QDBusMessage)));
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
        setShortcutStatus(QStringLiteral("A desktop session bus is required for global shortcuts"));
        return;
    }
    setShortcutStatus(QStringLiteral("Connecting desktop shortcut…"));
    const auto create = [this, generation] {
        if (generation != d->generation) return;
        const QString token = QStringLiteral("arcade_shelf_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
        QString sender = QDBusConnection::sessionBus().baseService();
        sender.remove(QLatin1Char(':'));
        sender.replace(QLatin1Char('.'), QLatin1Char('_'));
        const QString predicted = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
        auto *request = new ShelfPortalRequest(predicted, this,
                                               [this, generation](uint response, const QVariantMap &results) {
                                                   if (generation != d->generation) return;
                                                   if (response != 0) {
                                                       setShortcutStatus(results.value("timeout").toBool() ? QStringLiteral("Desktop shortcut request timed out")
                                                                                                           : QStringLiteral("Desktop shortcut session was declined; bind arcade-shelf --toggle in your desktop settings"));
                                                       return;
                                                   }
                                                   const QVariant handle = results.value(QStringLiteral("session_handle"));
                                                   const QString session = handle.canConvert<QDBusObjectPath>() ? handle.value<QDBusObjectPath>().path() : handle.toString();
                                                   if (!session.startsWith(QStringLiteral("/org/freedesktop/portal/desktop/session/"))) {
                                                       setShortcutStatus(QStringLiteral("Desktop portal returned an invalid shortcut session"));
                                                       return;
                                                   }
                                                   d->portalSession = session;
                                                   QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.portal.Desktop"), session,
                                                                                         QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Closed"),
                                                                                         d->portalSignals, SLOT(sessionClosed(QVariantMap, QDBusMessage)));
                                                   const QString bindToken = QStringLiteral("arcade_shelf_bind_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
                                                   QString bindSender = QDBusConnection::sessionBus().baseService();
                                                   bindSender.remove(QLatin1Char(':'));
                                                   bindSender.replace(QLatin1Char('.'), QLatin1Char('_'));
                                                   auto *binding = new ShelfPortalRequest(QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(bindSender, bindToken), this,
                                                                                          [this, generation](uint code, const QVariantMap &) {
                                                                                              if (generation != d->generation) return;
                                                                                              if (code != 0) {
                                                                                                  clearShortcut();
                                                                                                  setShortcutStatus(QStringLiteral("Desktop shortcut was not granted"));
                                                                                                  return;
                                                                                              }
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
                                                       const auto reply = watcher->reply();
                                                       watcher->deleteLater();
                                                       if (!guard) return;
                                                       if (reply.type() == QDBusMessage::ErrorMessage) {
                                                           guard->fail();
                                                           return;
                                                       }
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
            const auto reply = watcher->reply();
            watcher->deleteLater();
            if (!guard) return;
            if (reply.type() == QDBusMessage::ErrorMessage) {
                guard->fail();
                return;
            }
            if (!reply.arguments().isEmpty()) guard->setPath(reply.arguments().first().value<QDBusObjectPath>().path());
        });
    };
    if (d->portalRegistered) {
        create();
        return;
    }
    QDBusMessage registration = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),
                                                               QStringLiteral("/org/freedesktop/portal/desktop"), QStringLiteral("org.freedesktop.host.portal.Registry"), QStringLiteral("Register"));
    registration << OsId << QVariantMap{};
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(registration, 2500), this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation, create] {
        const auto reply = watcher->reply();
        watcher->deleteLater();
        if (generation != d->generation) return;
        if (reply.type() == QDBusMessage::ErrorMessage && reply.errorName() != QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod") && reply.errorName() != QStringLiteral("org.freedesktop.DBus.Error.UnknownInterface")) {
            // No portal at all (common on X11 desktops) is not a missing launcher.
            const bool noPortal = reply.errorName() == QStringLiteral("org.freedesktop.DBus.Error.ServiceUnknown") || reply.errorName() == QStringLiteral("org.freedesktop.DBus.Error.NameHasNoOwner");
            setShortcutStatus(noPortal
                                  ? QStringLiteral("This desktop has no portal for global shortcuts; bind arcade-shelf --toggle in its keyboard settings")
                                  : QStringLiteral("Desktop identity registration failed; install dev.arcade.Shelf.desktop or bind arcade-shelf --toggle manually"));
            return;
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
                   if (ok)
                       for (const auto &value : QJsonDocument::fromJson(output).array()) {
                           const QString name = value.toObject().value("name").toString();
                           if (name.endsWith(QStringLiteral(":arcade-shelf-show"))) candidates << name;
                       }
                   candidates.removeDuplicates();
                   const QString expected = OsId + QStringLiteral(":arcade-shelf-show");
                   const QString target = candidates.contains(expected) ? expected : (candidates.size() == 1 ? candidates.first() : QString());
                   if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_.:-]+$")).match(target).hasMatch()) {
                       setShortcutStatus(QStringLiteral("Portal connected; Hyprland has not published Shelf's shortcut target"));
                       return;
                   }
                   runProgram(this, d->hyprctl, d->hyprArguments({QStringLiteral("-j"), QStringLiteral("binds")}),
                              [this, generation, parsed, target](bool read, const QByteArray &bindings) {
                                  if (generation != d->generation) return;
                                  if (!read || !QJsonDocument::fromJson(bindings).isArray()) {
                                      setShortcutStatus(QStringLiteral("Couldn't check desktop shortcut collisions"));
                                      return;
                                  }
                                  bool owned = false;
                                  for (const auto &value : QJsonDocument::fromJson(bindings).array()) {
                                      const auto bind = value.toObject();
                                      if (bind.value("modmask").toInt() != parsed.hyprMask || bind.value("key").toString().compare(parsed.nativeKey, Qt::CaseInsensitive) != 0) continue;
                                      if (bind.value("description").toString() == d->bindingToken) owned = true;
                                      else if (bind.value("dispatcher").toString() == QStringLiteral("global") && bind.value("arg").toString() == target) owned = true;
                                      else {
                                          setShortcutStatus(QStringLiteral("Used by a desktop shortcut; choose another accelerator"));
                                          return;
                                      }
                                  }
                                  if (owned) {
                                      setShortcutStatus(QStringLiteral("Ready · %1").arg(d->accelerator));
                                      return;
                                  }
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

} // namespace Shelf

#ifdef ARCADE_SHELF_HAVE_DBUS
#include "DesktopShortcut.moc"
#endif
