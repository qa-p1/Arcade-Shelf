#pragma once

#include <QAbstractNativeEventFilter>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <memory>
#include <vector>

class QWindow;

namespace Shelf {

class NativeBackdrop;

// Native material belongs behind the original Qt surface. It never changes
// input handling, reparents Qt's content view, or installs compositor rules.
class Backdrop final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    explicit Backdrop(QObject *parent = nullptr);
    ~Backdrop() override;

    void attachWindow(QWindow *window);
    bool enabled() const { return m_enabled; }
    bool active() const { return m_active; }
    QString status() const { return m_status; }
    Q_INVOKABLE void setEnabled(bool enabled);
    Q_INVOKABLE void setDark(bool dark);
    Q_INVOKABLE void setRegion(const QRectF &region, qreal radius);

    // Region and radius are logical pixels, matching Qt Quick geometry.
    static QRectF boundedRegion(const QRectF &region, const QSize &windowSize);
    static qreal boundedRadius(qreal radius, const QRectF &region);

    bool nativeEventFilter(const QByteArray &eventType, void *message,
                           qintptr *result) override;
signals:
    void changed();
private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void scheduleRefresh();
    void refresh();
    void setState(bool active, const QString &status);
    QPointer<QWindow> m_window;
    std::vector<QMetaObject::Connection> m_connections;
    std::unique_ptr<NativeBackdrop> m_native;
    QRectF m_region;
    qreal m_radius = 18;
    bool m_enabled = true;
    bool m_active = false;
    bool m_dark = true;
    bool m_surfaceReady = false;
    bool m_refreshQueued = false;
    QString m_status;
};

} // namespace Shelf
