#pragma once

#include <QJsonArray>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>
#include <memory>
#include <vector>

class QWindow;

namespace Shelf {

// Suppression affects only the passive capsule. User visibility intent stays
// in Controller so fullscreen exit can restore it without activating a window.
class FullscreenGuard final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool suppressed READ suppressed NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    explicit FullscreenGuard(QObject *parent = nullptr);
    ~FullscreenGuard() override;
    void attachWindow(QWindow *window);
    void setCollapsed(bool collapsed);
    void setRequestedVisible(bool visible);
    bool suppressed() const { return m_suppressed; }
    QString status() const { return m_status; }

    static bool coversScreen(const QRect &window, const QRect &screen, int tolerance = 2);
    static bool hyprlandFullscreen(const QJsonArray &monitors, const QJsonArray &clients,
                                  const QString &screenName, qint64 ownPid,
                                  bool *monitorMatched = nullptr);
signals:
    void changed();
private:
    struct Native;
    friend struct Native;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void update();
    void setState(bool suppressed, const QString &status);
    QPointer<QWindow> m_window;
    std::vector<QMetaObject::Connection> m_connections;
    std::unique_ptr<Native> d;
    bool m_collapsed = true;
    bool m_requestedVisible = false;
    bool m_suppressed = false;
    QString m_status;
};

} // namespace Shelf
