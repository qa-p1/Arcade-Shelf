#include "Service.h"
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace Shelf {
QJsonArray publicActions()
{
    const auto action = [](QString id, QString title, QString verb, QJsonArray accepts, QJsonArray produces, QString effect, bool interactive) {
        return QJsonObject{{"id", id}, {"version", 1}, {"title", title}, {"verb", verb}, {"accepts", accepts},
            {"produces", produces}, {"effects", QJsonArray{effect}}, {"interactive", interactive}, {"privacy", "local"}};
    };
    return {action("shelf.add", "Add to Shelf", "add", {"file/*", "file/*[]", "folder/reference", "text/plain", "text/url", "text/rich"}, {}, "persists", false),
        action("shelf.show", "Show Shelf", "show", {}, {}, "opens-ui", true),
        action("shelf.pick", "Pick from Shelf", "pick", {}, {"file/*[]", "folder/reference", "text/plain", "text/url"}, "opens-ui", true)};
}
QJsonObject manifest(bool enabled, QString shortcut)
{
    QJsonArray shortcuts;
    if (!shortcut.isEmpty()) shortcuts.append(QJsonObject{{"id", "show-shelf"}, {"accelerator", shortcut}});
    return {{"schema", 1}, {"id", AppId}, {"name", "Arcade Shelf"}, {"version", Version},
        {"link", QJsonObject{{"protocol", QJsonArray{1}}}}, {"executable", ArcadeLink::executablePath()},
        {"launch", QJsonObject{{"background", QJsonArray{"--background"}}}}, {"shortcuts", shortcuts},
        {"settings", QJsonObject{{"linkEnabled", enabled}}}, {"actions", enabled ? publicActions() : QJsonArray{}}};
}
QString dataRoot()
{
    const QString override = qEnvironmentVariable("ARCADE_SHELF_HOME");
    return override.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) : QDir(override).absolutePath();
}
Service::Service(QString root, QObject *parent, bool background) : QObject(parent), m_root(std::move(root)), m_background(background)
{
    m_pickTimer.setParent(this);
    m_pickTimer.setInterval(100);
    connect(&m_pickTimer, &QTimer::timeout, this, [this] {
        if (m_picker && m_picker->cancelled()) {
            m_picker->finishError(ArcadeLink::Error::make("cancelled", "Cancelled."));
            m_picker.reset(); m_pickTimer.stop(); emit pickEnded();
        }
    });
}
void Service::start()
{
    m_store = std::make_unique<Store>(m_root);
    QString error;
    if (!m_store->open(&error)) { emit fatalError(error); return; }
    const auto controlLocations = ArcadeLink::Locations::under(m_root + "/instance");
    m_control = std::make_unique<ArcadeLink::Server>(AppId, Version, controlLocations);
    m_control->activate = [this] { emit showRequested({}, false); return true; };
    m_control->quit = [this] { emit quitRequested(); return true; };
    m_control->status = [this] { return QJsonObject{{"mode", m_background ? "background" : "foreground"}, {"shelf", m_store->activeShelf()}}; };
    m_control->invoke = [this](const QJsonObject &req, const ArcadeLink::Responder &r) {
        const QString action = req["action"].toString();
        if (action == "internal.settings") { emit showRequested({}, true); r.done({{"message", "Showing settings"}}); }
        else if (action == "internal.toggle") { emit toggleRequested(); r.done({}); }
        else if (action == "internal.add") { const auto result = m_store->add(req["inputs"].toArray()); if (result.error.isError()) r.fail(result.error); else { finishAdd(result, true); r.done(result.result); } }
        else r.fail(ArcadeLink::Error::make("unsupported_input", "Unknown lifecycle command"));
    };
    if (!m_control->start(&error)) { emit fatalError(error); return; }
    publish(); refresh();
}
void Service::stop()
{
    cancelPick();
    m_server.reset(); m_control.reset(); m_store.reset();
}
void Service::publish()
{
    const QJsonObject settings = m_store->settings();
    const bool enabled = settings["linkEnabled"].toBool(true);
    QString error;
    ArcadeLink::writeManifest(ArcadeLink::Locations::discover(), manifest(enabled, settings["shortcut"].toString("Ctrl+Alt+S")), &error);
    if (!error.isEmpty()) emit feedback(error, false);
    if (!enabled) { cancelPick(); m_server.reset(); return; }
    if (!m_server) {
        m_server = std::make_unique<ArcadeLink::Server>(AppId, Version, ArcadeLink::Locations::discover());
        m_server->describe = [] { return publicActions(); };
        m_server->invoke = [this](const auto &req, const auto &r) { invoke(req, r); };
        m_server->activate = [this] { emit showRequested({}, false); return true; };
        m_server->quit = [this] { emit quitRequested(); return true; };
        m_server->status = [this] { return QJsonObject{{"mode", m_background ? "background" : "foreground"}, {"shelf", m_store->activeShelf()}, {"items", m_store->items(m_store->activeShelf()).size()}}; };
        if (!m_server->start(&error)) { emit feedback(error, false); m_server.reset(); }
    } else m_server->notifyChanged();
}
bool Service::target(QString id)
{
    if (id.isEmpty()) return true;
    for (const auto &value : m_store->shelves()) {
        const auto shelf = value.toObject();
        if (shelf["id"] == id || shelf["name"] == id) return m_store->setActiveShelf(shelf["id"].toString());
    }
    return false;
}
void Service::invoke(const QJsonObject &req, const ArcadeLink::Responder &r)
{
    if (!m_store->settings()["linkEnabled"].toBool(true)) { r.fail(ArcadeLink::Error::make("denied", "Connections disabled", "disabled")); return; }
    if (req.contains("version") && (!req["version"].isDouble() || req["version"].toInt() != 1)) { r.fail(ArcadeLink::Error::make("version_mismatch", "Unsupported action version")); return; }
    const QString action = req["action"].toString();
    if (!req["inputs"].isArray() || (req.contains("options") && !req["options"].isObject())) { r.fail(ArcadeLink::Error::make("unsupported_input", "Malformed action inputs or options")); return; }
    const QJsonArray inputs = req["inputs"].toArray();
    const QJsonObject options = req["options"].toObject();
    if (action == "shelf.add") {
        if ((options.contains("shelf") && !options["shelf"].isString()) || (options.contains("reveal") && !options["reveal"].isBool())) { r.fail(ArcadeLink::Error::make("unsupported_input", "Invalid add options")); return; }
        const AddResult result = m_store->add(inputs, options);
        if (result.error.isError()) r.fail(result.error);
        else { finishAdd(result, options["reveal"].toBool(true)); r.done(result.result); }
    } else if (action == "shelf.show") {
        if (!inputs.isEmpty() || (options.contains("shelf") && !options["shelf"].isString()) || !target(options["shelf"].toString())) { r.fail(ArcadeLink::Error::make("unsupported_input", "Unknown shelf or unexpected input")); return; }
        refresh(); emit showRequested(m_store->activeShelf(), false); r.done({{"message", "Showing Arcade Shelf"}});
    } else if (action == "shelf.pick") {
        if (m_picker) { r.fail(ArcadeLink::Error::make("busy", "A picker is already open")); return; }
        if (!inputs.isEmpty() || (options.contains("multiple") && !options["multiple"].isBool()) || (options.contains("types") && !options["types"].isArray())) { r.fail(ArcadeLink::Error::make("unsupported_input", "Invalid picker options")); return; }
        m_pickTypes.clear();
        for (const auto &v : options["types"].toArray()) {
            if (!v.isString() || v.toString().isEmpty()) { r.fail(ArcadeLink::Error::make("unsupported_input", "Invalid picker type filter")); return; }
            m_pickTypes << v.toString();
        }
        m_multiple = options["multiple"].toBool(false);
        m_picker = r; r.startJob(); m_pickTimer.start();
        emit pickRequested(m_multiple, m_pickTypes);
    } else r.fail(ArcadeLink::Error::make("unsupported_input", "Unknown Shelf action"));
}
void Service::refresh()
{
    if (m_store) emit snapshot(m_store->shelves(), m_store->items(m_store->activeShelf()), m_store->activeShelf(), m_store->settings(), !m_undo.isEmpty());
}
void Service::finishAdd(const AddResult &result, bool reveal)
{
    if (!result.addedIds.isEmpty()) m_undo = result.addedIds;
    refresh(); emit feedback(result.result["message"].toString(), reveal);
}
void Service::add(QJsonArray inputs, QJsonObject options)
{
    auto result = m_store->add(inputs, options);
    if (result.error.isError()) emit feedback(result.error.userMessage("Arcade Shelf"), false);
    else finishAdd(result, options["reveal"].toBool(true));
}
void Service::remove(QStringList ids)
{
    QStringList safe;
    for (const auto &id : ids) { if (m_retained.contains(id)) m_deferredRemoval << id; else safe << id; }
    if (!m_store->remove(safe)) emit feedback("Could not remove the selected items.", false);
    m_undo.clear(); refresh();
}
void Service::undo() { auto ids = m_undo; m_undo.clear(); remove(ids); emit feedback("Last addition undone.", false); }
void Service::reorder(QString shelf, QStringList ids) { if (!m_store->reorder(shelf, ids)) emit feedback("Could not reorder items.", false); refresh(); }
void Service::switchShelf(QString id) { if (!target(id)) emit feedback("That shelf is unavailable.", false); refresh(); }
void Service::createShelf(QString name, bool temporary) { auto id = m_store->createShelf(name, temporary); if (id.isEmpty()) emit feedback("Could not create shelf.", false); else m_store->setActiveShelf(id); refresh(); }
void Service::renameShelf(QString id, QString name) { if (!m_store->renameShelf(id, name)) emit feedback("Could not rename shelf.", false); refresh(); }
void Service::deleteShelf(QString id) {
    if (!m_retained.isEmpty()) { emit feedback("Wait for the active action before deleting a shelf.", false); return; }
    if (!m_store->deleteShelf(id)) emit feedback("Quick Shelf cannot be deleted.", false); m_undo.clear(); refresh();
}
void Service::pinShelf(QString id, bool pin) { m_store->pinShelf(id, pin); refresh(); }
void Service::reorderShelves(QStringList ids) { m_store->reorderShelves(ids); refresh(); }
void Service::saveSettings(QJsonObject settings)
{
    if (!m_store->saveSettings(settings)) emit feedback("Could not save preferences.", false);
    // Acknowledge the submitted patch before the committed snapshot. The GUI
    // can then stop overlaying its pending values, including failed writes.
    emit settingsSaved(settings);
    publish(); refresh();
}
void Service::completePick(QStringList ids)
{
    if (!m_picker) return;
    ArcadeLink::Error error;
    auto outputs = m_store->exportItems(ids, m_pickTypes, m_multiple, &error);
    if (error.isError()) { emit feedback(error.userMessage("Arcade Shelf"), false); return; }
    if (outputs.isEmpty()) { emit feedback("Select an item to continue.", false); return; }
    m_picker->finish({{"outputs", outputs}, {"message", QString("Picked %1 item%2").arg(ids.size()).arg(ids.size() == 1 ? "" : "s")}});
    m_picker.reset(); m_pickTimer.stop(); emit pickEnded();
}
void Service::cancelPick()
{
    if (!m_picker) return;
    m_picker->finishError(ArcadeLink::Error::make("denied", "Cancelled.", "user_cancelled"));
    m_picker.reset(); m_pickTimer.stop(); emit pickEnded();
}
void Service::selectedContents(QStringList ids, std::function<void(QJsonArray, ArcadeLink::Error)> callback)
{
    ArcadeLink::Error error; auto items = m_store->exportItems(ids, {}, true, &error); callback(items, error);
}
void Service::retain(QStringList ids) { m_retained = std::move(ids); }
void Service::release() { m_retained.clear(); auto ids = m_deferredRemoval; m_deferredRemoval.clear(); if (!ids.isEmpty()) remove(ids); }
}
