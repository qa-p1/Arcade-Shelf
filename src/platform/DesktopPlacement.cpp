#include "DesktopPrivate.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QScreen>
#include <array>
#include <utility>

Q_LOGGING_CATEGORY(lcPlacement, "arcade.shelf.placement", QtWarningMsg)

namespace {

using Shelf::desktop::runProgram;

bool isWayland()
{
    return QGuiApplication::platformName().contains(QStringLiteral("wayland"), Qt::CaseInsensitive);
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

// Hyprland's clients query supplies the actual compositor address. Select only
// this process's main window; never use activewindow or caller-supplied IDs.
void ownHyprlandWindow(QObject *context, const QString &hyprctl, const QStringList &prefix,
                              QWindow *window, std::function<void(const QJsonObject &)> callback,
                              std::function<void(const QString &)> failed)
{
    const QPointer<QWindow> guard(window);
    runProgram(context, hyprctl, prefix + QStringList{QStringLiteral("-j"), QStringLiteral("clients")},
               [guard, callback = std::move(callback), failed = std::move(failed)](bool ok, const QByteArray &output) {
                   if (!guard) {
                       failed(QStringLiteral("Shelf's window closed"));
                       return;
                   }
                   QJsonObject found;
                   int matches = 0;
                   if (ok)
                       for (const auto &value : QJsonDocument::fromJson(output).array()) {
                           const auto client = value.toObject();
                           if (client.value("pid").toInteger() != QCoreApplication::applicationPid() || client.value("title").toString() != guard->title()) continue;
                           if (!QRegularExpression(QStringLiteral("^0x[0-9a-fA-F]+$")).match(client.value("address").toString()).hasMatch()) continue;
                           found = client;
                           ++matches;
                       }
                   if (matches != 1) {
                       failed(QStringLiteral("Couldn't identify Shelf's mapped window in Hyprland"));
                       return;
                   }
                   callback(found);
               });
}

} // namespace

namespace Shelf {

using desktop::runProgram;

void Desktop::queryHyprlandRounding()
{
    runProgram(this, d->hyprctl, d->hyprArguments({QStringLiteral("-j"), QStringLiteral("getoption"), QStringLiteral("decoration:rounding")}),
               [this](bool ok, const QByteArray &output) {
                   const QJsonValue value = QJsonDocument::fromJson(output).object().value(QStringLiteral("int"));
                   const int rounding = ok && value.isDouble() ? qBound(0, value.toInt(), 64) : -1;
                   if (rounding == d->hyprRounding) return;
                   d->hyprRounding = rounding;
                   emit compositorChanged();
               });
}

QRect Desktop::availableGeometry(const QString &screenName) const
{
    // Qt's Wayland screens report the whole output; Hyprland knows the bars.
    if (d->hyprland() && d->usableAreas.value(screenName).isValid()) return d->usableAreas.value(screenName);
    for (QScreen *screen : QGuiApplication::screens())
        if (screen->name() == screenName) return screen->availableGeometry();
    return {};
}

int Desktop::compositorRadius() const { return d->hyprland() ? d->hyprRounding : -1; }

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
    } else if (isWayland() && d->hyprland()) {
        if (!d->movePoll) {
            d->movePoll = new QTimer(this);
            d->movePoll->setInterval(150);
            QObject::connect(d->movePoll, &QTimer::timeout, this, [this] { sampleMove(d->moveGeneration); });
        }
        d->movePolls = 0;
        d->movePoll->start();
    }
#ifdef Q_OS_MACOS
    // Cocoa's performWindowDragWithEvent runs the native gesture synchronously.
    // Observe the final frame on the next event turn, after Qt synchronizes it.
    else
        QTimer::singleShot(0, this, [this, generation] { completeMove(generation); });
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
    if (d->movePoll) d->movePoll->stop();
    d->moveOffset.reset();
}

void Desktop::sampleMove(quint64 generation)
{
    if (generation != d->moveGeneration || !d->movingWindow) {
        clearMove();
        return;
    }
    if (++d->movePolls > 200) {
        clearMove();
        return;
    } // 30 s: give up quietly.
    if (d->moveSampling) return;
    d->moveSampling = true;
    const QPointer<QWindow> guard = d->movingWindow;
    runProgram(this, d->hyprctl, d->hyprArguments({QStringLiteral("-j"), QStringLiteral("cursorpos")}),
               [this, guard, generation](bool ok, const QByteArray &output) {
                   const QJsonObject cursor = QJsonDocument::fromJson(output).object();
                   if (!ok || !guard || generation != d->moveGeneration || !cursor.contains("x")) {
                       d->moveSampling = false;
                       return;
                   }
                   const QPoint pointer(cursor.value("x").toInt(), cursor.value("y").toInt());
                   ownHyprlandWindow(this, d->hyprctl, d->hyprArguments({}), guard, [this, generation, pointer](const QJsonObject &client) {
                    d->moveSampling = false;
                    if (generation != d->moveGeneration) return;
                    const auto at = client.value("at").toArray();
                    if (at.size() != 2) return;
                    const QPoint offset = pointer - QPoint(at[0].toInt(), at[1].toInt());
                    if (!d->moveOffset) { d->moveOffset = offset; return; }
                    if ((offset - *d->moveOffset).manhattanLength() > 2) completeMove(generation); }, [this](const QString &) { d->moveSampling = false; });
               });
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
    if (!window || flags == Qt::Edges() || (edges & ~int(Qt::TopEdge | Qt::BottomEdge | Qt::LeftEdge | Qt::RightEdge)) || (flags.testFlag(Qt::TopEdge) && flags.testFlag(Qt::BottomEdge)) || (flags.testFlag(Qt::LeftEdge) && flags.testFlag(Qt::RightEdge))) return false;
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
    // A window dragged past an edge while still on the monitor counts as
    // touching it: floating windows on Wayland may hang off-screen.
    const bool onMonitor = geometry.intersects(available);
    const auto gap = [onMonitor](qint64 inward) { return onMonitor ? qMax<qint64>(0, inward) : qAbs(inward); };
    const std::pair<QString, qint64> candidates[] = {
        {QStringLiteral("left"), gap(qint64(geometry.left()) - available.left())},
        {QStringLiteral("right"), gap(qint64(available.right()) - geometry.right())},
        {QStringLiteral("top"), gap(qint64(geometry.top()) - available.top())},
        {QStringLiteral("bottom"), gap(qint64(available.bottom()) - geometry.bottom())}};
    QString edge;
    qint64 closest = qint64(distance) + 1;
    for (const auto &candidate : candidates) {
        const bool vertical = candidate.first == QStringLiteral("left") || candidate.first == QStringLiteral("right");
        const bool overlaps = vertical
                                  ? qint64(geometry.bottom()) >= qint64(available.top()) - distance && qint64(geometry.top()) <= qint64(available.bottom()) + distance
                                  : qint64(geometry.right()) >= qint64(available.left()) - distance && qint64(geometry.left()) <= qint64(available.right()) + distance;
        if (!overlaps) continue;
        if (candidate.second < closest) {
            edge = candidate.first;
            closest = candidate.second;
        }
    }
    return edge;
}

QRect Desktop::snappedGeometry(const QRect &geometry, const QRect &available, const QString &edge)
{
    if (!geometry.isValid() || !available.isValid()) return geometry;
    if (edge != QStringLiteral("left") && edge != QStringLiteral("right") && edge != QStringLiteral("top") && edge != QStringLiteral("bottom")) return geometry;
    // Edge tabs can be smaller than the expanded panel's minimum dimensions.
    QRect result = clampGeometry(geometry, available, QSize(1, 1));
    if (edge == QStringLiteral("left")) result.moveLeft(available.left());
    else if (edge == QStringLiteral("right")) result.moveRight(available.right());
    else if (edge == QStringLiteral("top")) result.moveTop(available.top());
    else result.moveBottom(available.bottom());
    return result;
}

void Desktop::completeMove(quint64 generation)
{
    if (generation != d->moveGeneration || !d->movingWindow) return;
    const QPointer<QWindow> guard = d->movingWindow;
    if (!guard->isVisible()) {
        clearMove();
        return;
    }
    if (!isWayland()) {
        const QRect geometry = guard->geometry();
        QScreen *screen = screenFor(guard, {}, geometry);
        clearMove();
        if (geometry.isValid()) emit moveFinished(geometry, screen ? screen->name() : QString());
        return;
    }
    if (!d->hyprland()) {
        clearMove();
        return;
    }
    ownHyprlandWindow(this, d->hyprctl, d->hyprArguments({}), guard, [this, guard, generation](const QJsonObject &client) {
            if (!guard || generation != d->moveGeneration || guard != d->movingWindow) return;
            const auto position = client.value("at").toArray();
            const auto size = client.value("size").toArray();
            if (position.size() != 2 || size.size() != 2) { clearMove(); return; }
            const QRect geometry(position[0].toInt(), position[1].toInt(),
                                 size[0].toInt(), size[1].toInt());
            QScreen *screen = screenFor(guard, {}, geometry);
            clearMove();
            if (geometry.isValid()) { d->lastGeometry = geometry; d->returnTo = {}; }
            if (geometry.isValid())
                emit moveFinished(geometry, screen ? screen->name() : QString()); }, [this, generation](const QString &) {
            if (generation == d->moveGeneration) clearMove(); });
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
    if (!isWayland()) {
        window->setPosition(bounded.topLeft());
        return;
    }
    if (!d->hyprland()) return; // xdg-shell delegates placement to compositor.
    arrange(window, Placement{bounded.size(), {}, true, bounded, screen->name()});
}

void Desktop::dock(QWindow *window, const QString &edge)
{
    if (!window || !QStringList{QStringLiteral("left"), QStringLiteral("right"),
                                QStringLiteral("top"), QStringLiteral("bottom")}
                        .contains(edge)) return;
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
    // Qt does not know a Wayland window's position; Hyprland snaps from the
    // compositor's own geometry instead.
    if (d->hyprland()) {
        arrange(window, Placement{window->size(), edge, false, {}, {}});
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
    ownHyprlandWindow(this, d->hyprctl, d->hyprArguments({}), window, [this, guard](const QJsonObject &client) {
            if (!guard) return;
            const auto position = client.value("at").toArray();
            const auto size = client.value("size").toArray();
            if (position.size() != 2 || size.size() != 2) return;
            const QRect geometry(position[0].toInt(), position[1].toInt(), size[0].toInt(), size[1].toInt());
            if (!geometry.isValid()) return;
            // Moved some other way (e.g. the compositor's own drag binding).
            if (geometry.topLeft() != d->lastGeometry.topLeft()) d->returnTo = {};
            d->lastGeometry = geometry;
            QScreen *screen = screenFor(guard, {}, geometry);
            emit geometryObserved(geometry, screen ? screen->name() : QString()); }, [](const QString &) { /* Window may have unmapped while observation was pending. */ });
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
    d->placedWindow = window;
    d->pinned = enabled;
    reconcileHyprland();
}

QRect Desktop::arrangedGeometry(const QRect &current, const QRect &usable, const QSize &size,
                                const QString &edge, bool restore, const QRect &saved)
{
    if (!usable.isValid() || !size.isValid()) return current;
    QRect result(QPoint(), size);
    const QRect reference = restore ? saved : current;
    if (reference.isValid()) {
        // Resize toward the middle of the monitor: a capsule resting near the
        // right or bottom edge opens its panel inward instead of off-screen.
        const QPoint middle = usable.center();
        result.moveLeft(reference.center().x() > middle.x() ? reference.right() + 1 - size.width() : reference.left());
        result.moveTop(reference.center().y() > middle.y() ? reference.bottom() + 1 - size.height() : reference.top());
    } else {
        // First run: near the right edge, vertically centred.
        result.moveTopLeft(QPoint(usable.right() + 1 - size.width() - 12,
                                  usable.top() + (usable.height() - size.height()) / 2));
    }
    result = clampGeometry(result, usable, QSize(1, 1));
    return edge.isEmpty() ? result : snappedGeometry(result, usable, edge);
}

void Desktop::arrange(QWindow *window, const Placement &placement)
{
    if (!window || !placement.size.isValid()) return;
    if (!d->hyprland()) {
        // Other desktops honour Qt's own resize; only positions need help.
        if (placement.restore && placement.saved.isValid())
            restoreGeometry(window, QRect(placement.saved.topLeft(), window->size()), placement.screen);
        if (!placement.edge.isEmpty()) dock(window, placement.edge);
        else if (!placement.restore) restoreGeometry(window, window->geometry());
        return;
    }
    d->placedWindow = window;
    d->placement = placement;
    d->placementValid = true;
    ++d->placementSerial;
    reconcileHyprland();
}

// One query/dispatch round at a time. A request that arrives meanwhile reruns
// against the latest placement, so concurrent callers cannot interleave
// stale geometry or toggle the pin twice.
void Desktop::reconcileHyprland()
{
    const QPointer<QWindow> window = d->placedWindow;
    if (!window || !d->hyprland()) return;
    if (d->reconciling) {
        d->reconcileAgain = true;
        return;
    }
    if (!window->isVisible()) return; // The next openwindow event applies it.
    d->reconciling = true;
    d->reconcileAgain = false;
    const Placement placement = d->placement;
    const bool placed = d->placementValid;
    const quint64 serial = d->placementSerial;
    const bool pinned = d->pinned;
    const auto finish = [this] {
        d->reconciling = false;
        if (std::exchange(d->reconcileAgain, false)) reconcileHyprland();
    };
    ownHyprlandWindow(this, d->hyprctl, d->hyprArguments({}), window, [this, window, placement, placed, serial, pinned, finish](const QJsonObject &client) { runProgram(this, d->hyprctl, d->hyprArguments({QStringLiteral("-j"), QStringLiteral("monitors")}),
                                                                                                                                                                        [this, window, placement, placed, serial, pinned, finish, client](bool ok, const QByteArray &output) {
                                                                                                                                                                            if (!window) {
                                                                                                                                                                                finish();
                                                                                                                                                                                return;
                                                                                                                                                                            }
                                                                                                                                                                            const QString address = client.value("address").toString();
                                                                                                                                                                            const auto at = client.value("at").toArray();
                                                                                                                                                                            const auto size = client.value("size").toArray();
                                                                                                                                                                            const QRect current = at.size() == 2 && size.size() == 2
                                                                                                                                                                                                      ? QRect(at[0].toInt(), at[1].toInt(), size[0].toInt(), size[1].toInt())
                                                                                                                                                                                                      : QRect();
                                                                                                                                                                            // Hyprland reports monitor size in pixels, positions and
                                                                                                                                                                            // reservations (bars, gaps) in layout coordinates.
                                                                                                                                                                            const auto usableArea = [](const QJsonObject &monitor) {
                                                                                                                                                                                const double scale = monitor.value("scale").toDouble(1) > 0 ? monitor.value("scale").toDouble(1) : 1;
                                                                                                                                                                                int width = qRound(monitor.value("width").toDouble() / scale);
                                                                                                                                                                                int height = qRound(monitor.value("height").toDouble() / scale);
                                                                                                                                                                                if (monitor.value("transform").toInt() % 2) std::swap(width, height);
                                                                                                                                                                                const auto reserved = monitor.value("reserved").toArray();
                                                                                                                                                                                const int left = reserved.size() == 4 ? reserved[0].toInt() : 0, top = reserved.size() == 4 ? reserved[1].toInt() : 0;
                                                                                                                                                                                const int right = reserved.size() == 4 ? reserved[2].toInt() : 0, bottom = reserved.size() == 4 ? reserved[3].toInt() : 0;
                                                                                                                                                                                return QRect(monitor.value("x").toInt() + left, monitor.value("y").toInt() + top,
                                                                                                                                                                                             width - left - right, height - top - bottom);
                                                                                                                                                                            };
                                                                                                                                                                            QRect usable;
                                                                                                                                                                            const QJsonArray monitors = ok ? QJsonDocument::fromJson(output).array() : QJsonArray();
                                                                                                                                                                            for (const auto &value : monitors)
                                                                                                                                                                                d->usableAreas.insert(value.toObject().value("name").toString(), usableArea(value.toObject()));
                                                                                                                                                                            if (placement.restore && !placement.screen.isEmpty())
                                                                                                                                                                                for (const auto &value : monitors)
                                                                                                                                                                                    if (value.toObject().value("name").toString() == placement.screen) usable = usableArea(value.toObject());
                                                                                                                                                                            if (!usable.isValid())
                                                                                                                                                                                for (const auto &value : monitors)
                                                                                                                                                                                    if (value.toObject().value("id").toInt(-1) == client.value("monitor").toInt(-2)) usable = usableArea(value.toObject());
                                                                                                                                                                            QList<std::pair<QString, QString>> commands;
                                                                                                                                                                            if (!client.value("floating").toBool())
                                                                                                                                                                                commands.append({QStringLiteral("hl.dsp.window.float({action='on', window='address:%1'})").arg(address),
                                                                                                                                                                                                 QStringLiteral("setfloating address:%1").arg(address)});
                                                                                                                                                                            if (placed && usable.isValid()) {
                                                                                                                                                                                // A new minimum size makes Hyprland grow the window
                                                                                                                                                                                // before this query runs. Anchor on the last settled
                                                                                                                                                                                // geometry unless only its position changed (a move
                                                                                                                                                                                // through the compositor's own bindings).
                                                                                                                                                                                const QRect anchor = d->lastGeometry.isValid() && current.size() != d->lastGeometry.size()
                                                                                                                                                                                                         ? d->lastGeometry
                                                                                                                                                                                                         : current;
                                                                                                                                                                                const bool returning = !placement.restore && d->returnTo.size() == placement.size && anchor == d->lastGeometry && usable.contains(d->returnTo);
                                                                                                                                                                                const QRect target = returning
                                                                                                                                                                                                         ? (placement.edge.isEmpty() ? d->returnTo : snappedGeometry(d->returnTo, usable, placement.edge))
                                                                                                                                                                                                         : arrangedGeometry(anchor, usable, placement.size, placement.edge,
                                                                                                                                                                                                                            placement.restore, placement.saved);
                                                                                                                                                                                d->returnTo = placement.restore || returning || anchor.size() == target.size() ? QRect() : anchor;
                                                                                                                                                                                if (target.size() != current.size())
                                                                                                                                                                                    commands.append({QStringLiteral("hl.dsp.window.resize({x=%2, y=%3, relative=false, window='address:%1'})").arg(address).arg(target.width()).arg(target.height()),
                                                                                                                                                                                                     QStringLiteral("resizewindowpixel exact %2 %3,address:%1").arg(address).arg(target.width()).arg(target.height())});
                                                                                                                                                                                if (target.topLeft() != current.topLeft())
                                                                                                                                                                                    commands.append({QStringLiteral("hl.dsp.window.move({x=%2, y=%3, relative=false, window='address:%1'})").arg(address).arg(target.x()).arg(target.y()),
                                                                                                                                                                                                     QStringLiteral("movewindowpixel exact %2 %3,address:%1").arg(address).arg(target.x()).arg(target.y())});
                                                                                                                                                                                qCDebug(lcPlacement) << "current" << current << "anchor" << anchor << "usable" << usable
                                                                                                                                                                                                     << "size" << placement.size << "edge" << placement.edge
                                                                                                                                                                                                     << "restore" << placement.restore << placement.saved << "->" << target;
                                                                                                                                                                                d->lastGeometry = target;
                                                                                                                                                                                // Later rounds keep the window where it now is.
                                                                                                                                                                                if (serial == d->placementSerial) d->placement.restore = false;
                                                                                                                                                                            } else if (placed) qWarning("Arcade Shelf: Hyprland did not report the monitor showing Shelf");
                                                                                                                                                                            if (placed && d->tabStyled != placement.tab) {
                                                                                                                                                                                // Window properties for Shelf's own window only, reset when it leaves the edge.
                                                                                                                                                                                static const std::array<std::array<const char *, 3>, 4> properties{{{"rounding", "rounding", "0"}, {"border_size", "bordersize", "0"}, {"no_shadow", "noshadow", "1"}, {"no_blur", "noblur", "1"}}};
                                                                                                                                                                                for (const auto &[lua, legacy, on] : properties) {
                                                                                                                                                                                    const QString value = placement.tab ? QString::fromLatin1(on) : QStringLiteral("unset");
                                                                                                                                                                                    commands.append({QStringLiteral("hl.dsp.window.set_prop({prop='%2', value='%3', window='address:%1'})").arg(address, QLatin1String(lua), value),
                                                                                                                                                                                                     QStringLiteral("setprop address:%1 %2 %3").arg(address, QLatin1String(legacy), value)});
                                                                                                                                                                                }
                                                                                                                                                                                d->tabStyled = placement.tab;
                                                                                                                                                                            }
                                                                                                                                                                            // The legacy dispatcher toggles, so only send it on a change.
                                                                                                                                                                            if (client.value("pinned").toBool() != pinned)
                                                                                                                                                                                commands.append({QStringLiteral("hl.dsp.window.pin({action='%2', window='address:%1'})").arg(address, pinned ? QStringLiteral("on") : QStringLiteral("off")),
                                                                                                                                                                                                 QStringLiteral("pin address:%1").arg(address)});
                                                                                                                                                                            if (commands.isEmpty()) {
                                                                                                                                                                                finish();
                                                                                                                                                                                return;
                                                                                                                                                                            }
                                                                                                                                                                            dispatchHyprland(commands, [this, finish](bool done) {
                                                                                                                                                                                if (!done) setError(QStringLiteral("Hyprland did not accept Shelf's window placement"));
                                                                                                                                                                                finish();
                                                                                                                                                                            });
                                                                                                                                                                        }); }, [finish](const QString &error) {
            // Expected while the surface is still mapping; openwindow retries.
            qCDebug(lcPlacement) << error;
            finish(); });
}

void Desktop::dispatchHyprland(const QList<std::pair<QString, QString>> &commands, std::function<void(bool)> done)
{
    const auto batch = [&commands](bool lua) {
        QStringList parts;
        for (const auto &command : commands) parts << QStringLiteral("dispatch ") + (lua ? command.first : command.second);
        return parts.join(QStringLiteral(" ; "));
    };
    // Every dispatch in a batch replies "ok" or an error line.
    const auto accepted = [](bool ok, const QByteArray &reply) {
        if (!ok) return false;
        for (const QByteArray &line : reply.split('\n'))
            if (!line.trimmed().isEmpty() && line.trimmed() != QByteArrayLiteral("ok")) return false;
        return true;
    };
    const QString lua = batch(true), legacy = batch(false);
    const auto tryLegacy = [this, legacy, accepted, done] {
        runProgram(this, d->hyprctl, d->hyprArguments({QStringLiteral("--batch"), legacy}),
                   [this, accepted, done](bool ok, const QByteArray &reply) {
                       const bool good = accepted(ok, reply);
                       if (good) d->hyprSyntax = Native::HyprSyntax::Legacy;
                       else qWarning("Arcade Shelf: hyprctl rejected placement: %s", reply.trimmed().constData());
                       done(good);
                   });
    };
    if (d->hyprSyntax == Native::HyprSyntax::Legacy) {
        tryLegacy();
        return;
    }
    runProgram(this, d->hyprctl, d->hyprArguments({QStringLiteral("--batch"), lua}),
               [this, accepted, done, tryLegacy](bool ok, const QByteArray &reply) {
                   if (accepted(ok, reply)) {
                       d->hyprSyntax = Native::HyprSyntax::Lua;
                       done(true);
                       return;
                   }
                   if (d->hyprSyntax == Native::HyprSyntax::Unknown) {
                       tryLegacy();
                       return;
                   }
                   qWarning("Arcade Shelf: hyprctl rejected placement: %s", reply.trimmed().constData());
                   done(false);
               });
}

} // namespace Shelf
