#pragma once

#include <QAbstractNativeEventFilter>
#include <QJsonArray>
#include <QObject>
#include <QRect>
#include <QString>
#include <memory>

class QMimeData;
class QWindow;

namespace Shelf {

// Ordinary native toplevel surfaces keep Qt's Wayland data-device transport.
// Platform extensions supplement placement and shortcuts; they never replace
// native dragging with an overlay or simulate pointer/keyboard input.
class Desktop final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(QString shortcutStatus READ shortcutStatus NOTIFY shortcutStatusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit Desktop(QObject *parent = nullptr);
    ~Desktop() override;

    Q_INVOKABLE bool move(QWindow *window);
    Q_INVOKABLE bool resize(QWindow *window, int edges);
    Q_INVOKABLE void dock(QWindow *window, const QString &edge);
    Q_INVOKABLE void setAlwaysOnTop(QWindow *window, bool enabled);
    Q_INVOKABLE void restoreGeometry(QWindow *window, const QRect &geometry,
                                    const QString &screenName = {});
    Q_INVOKABLE void captureGeometry(QWindow *window);
    Q_INVOKABLE void reveal(const QString &path);
    Q_INVOKABLE bool open(const QString &pathOrUrl);
    Q_INVOKABLE void startDrag(QWindow *window, const QJsonArray &contents);

    QString shortcutStatus() const;
    QString lastError() const;
    Q_INVOKABLE void configureShortcut(const QString &accelerator,
                                       const QString &executable);
    Q_INVOKABLE bool setStartAtLogin(bool enabled, const QString &executable);
    Q_INVOKABLE bool startAtLogin() const;

    // These pure helpers are shared by tests and the native adapter.
    static QMimeData *mimeDataForContents(const QJsonArray &contents);
    static QRect clampGeometry(const QRect &geometry, const QRect &available,
                               const QSize &minimum = QSize(200, 64));
    static QString validateShortcut(const QString &accelerator);

    bool nativeEventFilter(const QByteArray &eventType, void *message,
                           qintptr *result) override;

signals:
    void activated();
    void shortcutStatusChanged();
    void errorChanged();
    void errorOccurred(const QString &message);
    void dragFinished(int action);
    void geometryObserved(const QRect &geometry, const QString &screenName);

private:
    struct Native;
    std::unique_ptr<Native> d;
    void setError(const QString &message);
    void setShortcutStatus(const QString &message);
    void clearShortcut();
    void startPortalShortcut(quint64 generation);
    void installHyprlandShortcut(quint64 generation);
};

} // namespace Shelf
