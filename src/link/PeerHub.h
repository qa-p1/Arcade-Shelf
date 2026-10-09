#pragma once

#include "ArcadeLink.h"
#include <QJsonArray>
#include <QObject>
#include <QSet>
#include <QThread>
#include <atomic>
#include <memory>

Q_DECLARE_METATYPE(ArcadeLink::Error)

namespace Shelf {

// UI-facing cache. Registry I/O and every peer call run on workers.
class PeerHub final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    explicit PeerHub(QObject *parent = nullptr);
    ~PeerHub() override;
    void start();
    void setEnabled(bool enabled);
    void setPeerEnabled(QString id, bool enabled);
    QJsonArray manifests() const;
    QJsonArray offers(QJsonArray selected) const;
    QJsonArray sources() const;
    void invoke(QString app, QString action, QJsonArray inputs = {}, QJsonObject options = {});
    void cancel();
    bool busy() const { return m_busy; }

signals:
    void registryChanged();
    void progress(double fraction, QString message);
    void finished(QJsonObject result, ArcadeLink::Error error);
    void busyChanged();

private:
    bool uses(const QString &id) const;
    void refreshWorkerPolicy();
    QJsonArray m_manifests;
    QSet<QString> m_disabled;
    bool m_enabled = true;
    bool m_busy = false;
    bool m_started = false;
    std::atomic_bool m_stopping{false};
    std::shared_ptr<std::atomic_bool> m_cancel;
    QThread m_registryThread;
    QObject *m_registryWorker = nullptr;
    QThread *m_invokeThread = nullptr;
    // Accessed exclusively on m_registryThread.
    ArcadeLink::Registry *m_registry = nullptr;
    bool m_workerEnabled = true;
    QSet<QString> m_workerDisabled;
    void publishRegistry();
};

} // namespace Shelf
