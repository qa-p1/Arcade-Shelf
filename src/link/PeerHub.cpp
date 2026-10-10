#include "PeerHub.h"
#include "ShelfInvoke.h"

#include <QCoreApplication>
#include <QJsonValue>
#include <QMetaObject>
#include <limits>

namespace Shelf {
namespace {
bool arrayPattern(const QJsonArray &accepts)
{
    for (const auto &pattern : accepts)
        if (pattern.toString().section(';', 0, 0).endsWith("[]")) return true;
    return false;
}

qint64 inputBytes(const QJsonArray &inputs)
{
    qint64 total = 0;
    for (const auto &value : inputs) {
        const auto content = value.toObject();
        qint64 bytes = content.value("size").toInteger(-1);
        if (bytes < 0) bytes = content.value("text").toString().toUtf8().size()
            + content.value("html").toString().toUtf8().size();
        if (total > std::numeric_limits<qint64>::max() - bytes)
            return std::numeric_limits<qint64>::max();
        total += bytes;
    }
    return total;
}

bool requiresConfirmation(const QJsonArray &effects)
{
    for (const auto &value : effects) {
        const auto effect = value.toString();
        if (effect == "network" || effect == "uploads-content" || effect == "sends-to-device"
            || effect == "executes-commands" || effect == "overwrites-files"
            || effect == "deletes-files" || effect == "window-control") return true;
    }
    return false;
}

QJsonObject offer(const QJsonObject &manifest, QJsonObject action, const QJsonArray &inputs)
{
    action.insert("app", manifest.value("id"));
    action.insert("appName", manifest.value("name"));
    action.insert("action", action.value("id"));
    action.insert("confirmationRequired", requiresConfirmation(action.value("effects").toArray()));
    const qint64 limit = action.value("maxBytes").toInteger(-1);
    const bool oversized = limit >= 0 && inputBytes(inputs) > limit;
    action.insert("enabled", !oversized);
    if (oversized)
        action.insert("reason", ArcadeLink::standardMessage("too_large", manifest.value("name").toString(), {}, limit));
    return action;
}

bool compatible(const QJsonObject &manifest, const QJsonObject &action, const QJsonArray &inputs)
{
    if (!ArcadeLink::actionUsable(manifest, action)) return false;
    const auto accepts = action.value("accepts").toArray();
    if (inputs.isEmpty() || accepts.isEmpty()) return false;
    // Historical one-value consumers must never receive mixed Content values.
    if (inputs.size() > 1 && (!arrayPattern(accepts)
        || (manifest.value("id").toString() == "arcade.clipboard"
            && action.value("id").toString() == "clipboard.add"))) return false;
    for (const auto &value : inputs)
        if (!value.isObject() || !ArcadeLink::acceptsContent(accepts, value.toObject())) return false;
    return true;
}
} // namespace

PeerHub::PeerHub(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<ArcadeLink::Error>();
}

PeerHub::~PeerHub()
{
    m_stopping.store(true);
    cancel();
    // The bounded adapter disconnects cancelled jobs without waiting on peers.
    if (m_invokeThread) {
        m_invokeThread->wait();
        delete m_invokeThread;
    }
    m_registryThread.quit();
    m_registryThread.wait();
}

void PeerHub::start()
{
    if (m_started) return;
    m_started = true;
    m_workerEnabled = m_enabled;
    m_workerDisabled = m_disabled;
    m_registryWorker = new QObject;
    m_registryWorker->moveToThread(&m_registryThread);
    connect(&m_registryThread, &QThread::finished, m_registryWorker, &QObject::deleteLater);
    connect(&m_registryThread, &QThread::started, m_registryWorker, [this] {
        m_registry = new ArcadeLink::Registry(ArcadeLink::Locations::discover(), m_registryWorker);
        m_registry->refresh();
        m_registry->watch();
        connect(m_registry, &ArcadeLink::Registry::changed, m_registryWorker, [this] { publishRegistry(); });
        publishRegistry();
    });
    m_registryThread.start();
}

void PeerHub::publishRegistry()
{
    QJsonArray snapshot;
    for (auto manifest : m_registry->apps()) {
        if (manifest.value("id").toString() == "arcade.shelf") continue;
        QString runningVersion;
        const auto state = ArcadeLink::appState(m_registry->locations(), *m_registry,
            manifest.value("id").toString(), "arcade.shelf", &runningVersion);
        manifest.insert("state", state);
        manifest.insert("running", state == "Running");
        if (state == "Running") manifest.insert("runningVersion", runningVersion);
        // Pipelines have individual contracts; the broad run action is insufficient.
        // Fetch the read-only catalog on registry changes, never when opening menus.
        if (m_workerEnabled && !m_workerDisabled.contains("arcade.box")
            && manifest.value("id").toString() == "arcade.box" && state == "Running") {
            bool canList = false;
            for (const auto &value : manifest.value("actions").toArray()) {
                const auto action = value.toObject();
                if (action.value("id").toString() == "box.pipelines"
                    && ArcadeLink::actionUsable(manifest, action)) canList = true;
            }
            if (canList) {
                QJsonObject result;
                ArcadeLink::Error error;
                const QJsonObject request{{"action", "box.pipelines"}, {"version", 1},
                    {"inputs", QJsonArray{}}, {"options", QJsonObject{}},
                    {"context", QJsonObject{{"source", "arcade.shelf"}, {"interactive", false}, {"reason", "discovery"}}}};
                if (ShelfInvoke::run(m_registry->locations(), manifest, request, &result, &error, {}, &m_stopping, 3000, false)) {
                    for (const auto &value : result.value("outputs").toArray()) {
                        const auto output = value.toObject();
                        if (output.value("type").toString() == "structured/pipelines") {
                            manifest.insert("pipelines", output.value("data"));
                            manifest.insert("pipelinesLoaded", true);
                        }
                    }
                }
            }
        }
        snapshot.append(manifest);
    }
    if (m_stopping.load()) return;
    QMetaObject::invokeMethod(this, [this, snapshot] {
        m_manifests = snapshot;
        emit registryChanged();
    }, Qt::QueuedConnection);
}

void PeerHub::refreshWorkerPolicy()
{
    if (!m_registryWorker) return;
    const bool enabled = m_enabled;
    const auto disabled = m_disabled;
    QMetaObject::invokeMethod(m_registryWorker, [this, enabled, disabled] {
        m_workerEnabled = enabled;
        m_workerDisabled = disabled;
        if (m_registry) publishRegistry();
    }, Qt::QueuedConnection);
}

void PeerHub::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (!enabled) cancel();
    refreshWorkerPolicy();
    emit registryChanged();
}

void PeerHub::setPeerEnabled(QString id, bool enabled)
{
    if (enabled == !m_disabled.contains(id)) return;
    if (enabled) m_disabled.remove(id);
    else m_disabled.insert(id);
    refreshWorkerPolicy();
    emit registryChanged();
}

bool PeerHub::uses(const QString &id) const
{
    return m_enabled && id != "arcade.shelf" && !m_disabled.contains(id);
}

QJsonArray PeerHub::manifests() const { return m_manifests; }

QJsonArray PeerHub::offers(QJsonArray selected) const
{
    QJsonArray out;
    if (!m_enabled || selected.isEmpty()) return out;
    for (const auto &value : m_manifests) {
        const auto manifest = value.toObject();
        if (!uses(manifest.value("id").toString())) continue;
        for (const auto &entry : manifest.value("actions").toArray()) {
            const auto action = entry.toObject();
            if (action.value("id").toString() == "box.pipeline.run") {
                for (const auto &pipelineValue : manifest.value("pipelines").toArray()) {
                    const auto pipeline = pipelineValue.toObject();
                    auto candidate = action;
                    for (const auto &key : {"accepts", "produces", "effects", "interactive"})
                        candidate.insert(key, pipeline.value(key));
                    candidate.insert("title", pipeline.value("name"));
                    candidate.insert("options", QJsonObject{{"pipeline", pipeline.value("id")}});
                    if (compatible(manifest, candidate, selected)) out.append(offer(manifest, candidate, selected));
                }
            } else if (compatible(manifest, action, selected)) out.append(offer(manifest, action, selected));
        }
    }
    return out;
}

QJsonArray PeerHub::sources() const
{
    QJsonArray out;
    if (!m_enabled) return out;
    for (const auto &value : m_manifests) {
        const auto manifest = value.toObject();
        if (!uses(manifest.value("id").toString())) continue;
        for (const auto &entry : manifest.value("actions").toArray()) {
            const auto action = entry.toObject();
            if (!ArcadeLink::actionUsable(manifest, action)
                || !action.value("accepts").toArray().isEmpty()
                || !action.value("interactive").toBool()) continue;
            bool useful = false;
            for (const auto &type : action.value("produces").toArray())
                if (type.toString().startsWith("file/") || type.toString().startsWith("text/")
                    || type.toString() == "folder/reference") useful = true;
            if (useful) out.append(offer(manifest, action, {}));
        }
    }
    return out;
}

void PeerHub::invoke(QString app, QString action, QJsonArray inputs, QJsonObject options)
{
    auto fail = [this](const ArcadeLink::Error &error) { emit finished({}, error); };
    if (!uses(app)) { fail(ArcadeLink::Error::make("denied", {}, "disabled")); return; }
    if (m_busy) { fail(ArcadeLink::Error::make("busy", {})); return; }
    QJsonObject manifest;
    for (const auto &value : m_manifests)
        if (value.toObject().value("id").toString() == app) manifest = value.toObject();
    if (manifest.isEmpty()) { fail(ArcadeLink::Error::make("not_installed", {})); return; }
    QJsonObject request{{"action", action}, {"inputs", inputs}, {"options", options},
        {"context", QJsonObject{{"source", "arcade.shelf"}, {"interactive", true}, {"reason", "user-click"}}}};
    const auto contract = ShelfInvoke::actionFor(manifest, request);
    if (contract.isEmpty()) { fail(ArcadeLink::Error::make("unavailable", {})); return; }
    request.insert("version", contract.value("version").toInt(1));
    if (!inputs.isEmpty() && !compatible(manifest, contract, inputs)) {
        fail(ArcadeLink::Error::make("unsupported_input", {})); return;
    }
    if (m_invokeThread) {
        m_invokeThread->wait();
        delete m_invokeThread;
        m_invokeThread = nullptr;
    }
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_cancel;
    m_busy = true;
    emit busyChanged();
    m_invokeThread = QThread::create([this, manifest, request, cancelled] {
        QJsonObject result;
        ArcadeLink::Error error;
        const auto report = [this](double fraction, const QString &message) {
            if (m_stopping.load()) return;
            QMetaObject::invokeMethod(this, [this, fraction, message] { emit progress(fraction, message); }, Qt::QueuedConnection);
        };
        ShelfInvoke::run(ArcadeLink::Locations::discover(), manifest, request, &result, &error, report, cancelled.get());
        if (m_stopping.load()) return;
        QMetaObject::invokeMethod(this, [this, result, error] {
            m_busy = false;
            emit busyChanged();
            emit finished(result, error);
        }, Qt::QueuedConnection);
    });
    m_invokeThread->start();
}

void PeerHub::cancel()
{
    if (m_cancel) m_cancel->store(true);
}

} // namespace Shelf
