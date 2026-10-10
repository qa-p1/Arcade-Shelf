#pragma once

#include <QAbstractNativeEventFilter>
#include <QJsonArray>
#include <QObject>
#include <QRect>
#include <QString>
#include <functional>
#include <memory>

class QMimeData;
class QWindow;

namespace Shelf {

// Ordinary native toplevel surfaces keep Qt's Wayland data-device transport.
// Platform extensions supplement placement and shortcuts; they never replace
// native dragging with an overlay or simulate pointer/keyboard input.
class Desktop final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    // What Shelf's window should look like. On Hyprland the adapter applies it
    // with runtime dispatches aimed only at Shelf's own window, again after
    // every map, because the compositor ignores client-side resizes and moves.
    struct Placement {
        QSize size;
        QString edge;
        // When restoring, place at saved's top-left (or the default spot when
        // saved has no position). Otherwise keep the current compositor
        // position, anchored to whichever monitor edge the window is near.
        bool restore = false;
        QRect saved;
        QString screen;
        // A docked handle draws its own edge-flush shape; the compositor's
        // rounding, border, shadow and blur would square it off.
        bool tab = false;
    };

    Q_PROPERTY(QString shortcutStatus READ shortcutStatus NOTIFY shortcutStatusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit Desktop(QObject *parent = nullptr);
    ~Desktop() override;

    Q_INVOKABLE bool move(QWindow *window);
    Q_INVOKABLE bool resize(QWindow *window, int edges);
    Q_INVOKABLE void dock(QWindow *window, const QString &edge);
    void arrange(QWindow *window, const Placement &placement);
    Q_INVOKABLE void setAlwaysOnTop(QWindow *window, bool enabled);
    Q_INVOKABLE void restoreGeometry(QWindow *window, const QRect &geometry,
                                    const QString &screenName = {});
    Q_INVOKABLE void captureGeometry(QWindow *window);
    Q_INVOKABLE void reveal(const QString &path);
    Q_INVOKABLE bool open(const QString &pathOrUrl);
    Q_INVOKABLE void startDrag(QWindow *window, const QJsonArray &contents);

    QString shortcutStatus() const;
    QString lastError() const;
    // The compositor's own corner radius when it shapes Shelf's window, else -1.
    int compositorRadius() const;
    // The usable area of a monitor, without panels and bars where known.
    QRect availableGeometry(const QString &screenName) const;
    Q_INVOKABLE void configureShortcut(const QString &accelerator,
                                       const QString &executable);
    Q_INVOKABLE bool setStartAtLogin(bool enabled, const QString &executable);
    Q_INVOKABLE bool startAtLogin() const;

    // These pure helpers are shared by tests and the native adapter.
    static QMimeData *mimeDataForContents(const QJsonArray &contents);
    static QRect clampGeometry(const QRect &geometry, const QRect &available,
                               const QSize &minimum = QSize(200, 64));
    static QString nearestEdge(const QRect &geometry, const QRect &available,
                               int distance = 20);
    static QRect snappedGeometry(const QRect &geometry, const QRect &available,
                                 const QString &edge);
    static QString validateShortcut(const QString &accelerator);
    static QRect arrangedGeometry(const QRect &current, const QRect &usable,
                                  const QSize &size, const QString &edge,
                                  bool restore, const QRect &saved);

    bool eventFilter(QObject *watched, QEvent *event) override;
    bool nativeEventFilter(const QByteArray &eventType, void *message,
                           qintptr *result) override;

signals:
    void activated();
    void shortcutStatusChanged();
    void errorChanged();
    void errorOccurred(const QString &message);
    void compositorChanged();
    void dragFinished(int action);
    void geometryObserved(const QRect &geometry, const QString &screenName);
    // Emitted only after a native movement gesture finishes. On Wayland only
    // supported compositor geometry is reported; synthetic Qt button release
    // at the beginning of xdg move is never treated as completion.
    void moveFinished(const QRect &geometry, const QString &screenName);

private:
    struct Native;
    std::unique_ptr<Native> d;
    void setError(const QString &message);
    void setShortcutStatus(const QString &message);
    void clearShortcut();
    void clearMove();
    void completeMove(quint64 generation);
    void sampleMove(quint64 generation);
    void startPortalShortcut(quint64 generation);
    void installHyprlandShortcut(quint64 generation);
    void reconcileHyprland();
    void queryHyprlandRounding();
    void dispatchHyprland(const QList<std::pair<QString, QString>> &commands,
                          std::function<void(bool)> done);
};

} // namespace Shelf
