#include "DesktopPrivate.h"

#include <QDir>
#include <QGuiApplication>
#include <QProcess>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#if defined(ARCADE_SHELF_HAVE_X11) && __has_include(<xcb/xcb.h>)
#define ARCADE_SHELF_NATIVE_XCB_EVENTS 1
#include <xcb/xcb.h>
#endif

namespace Shelf::desktop {

void runProgram(QObject *context, const QString &program, const QStringList &arguments,
                ProgramReply reply, int timeoutMs)
{
    if (program.isEmpty()) {
        reply(false, {});
        return;
    }
    auto *process = new QProcess(context);
    auto *deadline = new QTimer(process);
    deadline->setSingleShot(true);
    auto output = std::make_shared<QByteArray>();
    auto finished = std::make_shared<bool>(false);
    const auto finish = [process, deadline, output, finished, reply](bool ok) {
        if (std::exchange(*finished, true)) return;
        deadline->stop();
        output->append(process->readAllStandardOutput());
        if (output->size() > 2 * 1024 * 1024) {
            output->clear();
            ok = false;
        }
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
    return platform == QStringLiteral("offscreen") || platform == QStringLiteral("minimal") || qEnvironmentVariableIntValue("ARCADE_SHELF_DISABLE_GLOBAL_SHORTCUT") == 1 || qEnvironmentVariableIntValue("ARCADE_SHELF_TESTING") == 1 || (!qEnvironmentVariableIsEmpty("ARCADE_HOME") && qEnvironmentVariableIntValue("ARCADE_SHELF_ALLOW_DESKTOP_IN_TESTS") != 1);
}

} // namespace Shelf::desktop

namespace Shelf {

Desktop::Desktop(QObject *parent) : QObject(parent), d(std::make_unique<Native>())
{
    if (qGuiApp) qGuiApp->installNativeEventFilter(this);
    watchShortcutPortal();
    if (d->hyprland()) {
        const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
        const QString socket = QDir(runtime).filePath(QStringLiteral("hypr/%1/.socket2.sock").arg(d->hyprSignature));
        d->hyprEvents = new QLocalSocket(this);
        QObject::connect(d->hyprEvents, &QLocalSocket::readyRead, this, [this] {
            d->hyprEventBuffer.append(d->hyprEvents->readAll());
            if (d->hyprEventBuffer.size() > 65536) {
                d->hyprEventBuffer.clear();
                return;
            }
            qsizetype newline;
            while ((newline = d->hyprEventBuffer.indexOf('\n')) >= 0) {
                const QByteArray event = d->hyprEventBuffer.left(newline);
                d->hyprEventBuffer.remove(0, newline + 1);
                if (event.startsWith("configreloaded>>")) {
                    queryHyprlandRounding();
                    if (!d->accelerator.isEmpty()) {
                        d->hyprBound = false;
                        installHyprlandShortcut(d->generation);
                    }
                } else if (event.startsWith("openwindow>>") && d->placementValid) {
                    // ADDRESS,WORKSPACE,CLASS,TITLE. Every map (first show, or
                    // after hiding for a fullscreen app) lands wherever the
                    // compositor chose, so put Shelf back where it was.
                    const QList<QByteArray> fields = event.mid(12).split(',');
                    if (fields.size() >= 4 && QString::fromUtf8(fields[2]) == QGuiApplication::desktopFileName()) {
                        qCDebug(lcPlacement) << "mapped; restoring" << d->lastGeometry;
                        d->placement.restore = true;
                        d->tabStyled.reset(); // A new mapping starts with compositor defaults.
                        if (d->lastGeometry.isValid()) d->placement.saved = d->lastGeometry;
                        ++d->placementSerial;
                        reconcileHyprland();
                    }
                }
            }
        });
        d->hyprEvents->connectToServer(socket);
        queryHyprlandRounding();
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

QString Desktop::lastError() const { return d->error; }
void Desktop::setError(const QString &message)
{
    if (d->error == message) return;
    d->error = message;
    emit errorChanged();
    if (!message.isEmpty()) emit errorOccurred(message);
}

bool Desktop::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType)
    Q_UNUSED(result)
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
        emit activated();
        return true;
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

} // namespace Shelf
