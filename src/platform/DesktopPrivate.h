#pragma once

// Shared by the Desktop implementation files, one per concern:
//   Desktop.cpp           construction, errors, native events, the helpers below
//   DesktopPlacement.cpp  move, snap, dock, arrange, and the Hyprland reconciliation
//   DesktopFiles.cpp      opening and revealing files, the MIME data for dragging out
//   DesktopShortcut.cpp   the global shortcut: validation, portal session, Hyprland binding
//   DesktopLogin.cpp      start at login: entry paths, ownership, the legacy-id migration
// Not part of Shelf's interface; include only from those files.

#include "Desktop.h"

#include <QHash>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QWindow>
#include <functional>
#include <optional>

#ifdef Q_OS_MACOS
#include <Carbon/Carbon.h>
#endif

Q_DECLARE_LOGGING_CATEGORY(lcPlacement)

#ifdef ARCADE_SHELF_HAVE_DBUS
class ShelfPortalSignals;
#endif

namespace Shelf::desktop {

using ProgramReply = std::function<void(bool, const QByteArray &)>;

// Only user actions and pending requests create a process/deadline. No timers
// run while idle. Arguments are passed separately, never through a shell.
void runProgram(QObject *context, const QString &program, const QStringList &arguments,
                ProgramReply reply, int timeoutMs = 1800);

bool isolatedDesktop();

} // namespace Shelf::desktop

namespace Shelf {

struct Desktop::Native {
    QString error;
    // Until a shortcut is configured nothing is registered; "No global shortcut"
    // is only for an empty one (SH-4: it showed under a recorded Ctrl Alt S).
    QString shortcutStatus = QStringLiteral("Not registered yet");
    QString accelerator;
    QString executable;
    quint64 generation = 0;
    bool isolated = desktop::isolatedDesktop();
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
    // Hyprland reports neither the end of an interactive move nor the button
    // state. While (and only while) a move runs, sample the cursor and window:
    // the window follows the cursor at a fixed offset until the button lifts.
    QTimer *movePoll = nullptr;
    int movePolls = 0;
    bool moveSampling = false;
    std::optional<QPoint> moveOffset;
    // Hyprland 0.55+ reads `hyprctl dispatch` as Lua; older releases use the
    // classic dispatcher names. Learned from the first accepted dispatch.
    enum class HyprSyntax { Unknown,
                            Lua,
                            Legacy };
    HyprSyntax hyprSyntax = HyprSyntax::Unknown;
    int hyprRounding = -1;
    QPointer<QWindow> placedWindow;
    Placement placement;
    bool placementValid = false;
    quint64 placementSerial = 0;
    QRect lastGeometry;
    // The rect Shelf changed size from, so collapsing returns exactly there
    // instead of re-anchoring (which would walk a tab along its edge).
    QRect returnTo;
    QHash<QString, QRect> usableAreas;
    std::optional<bool> tabStyled; // What the mapped window currently carries.
    bool pinned = false;
    bool reconciling = false;
    bool reconcileAgain = false;
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
    {
        return QStringList{QStringLiteral("-i"), hyprSignature} + arguments;
    }
};

} // namespace Shelf
