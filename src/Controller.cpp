#include "Controller.h"
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QScreen>
#include <QJsonDocument>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QCryptographicHash>
#include <QJSValue>

namespace Shelf {
Controller::Controller(QString root, bool background, QObject *parent)
    : QObject(parent), m_service(new Service(root,nullptr,background)), m_peers(this), m_desktop(this), m_root(std::move(root)), m_visible(!background)
{
    m_service->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_service, &QObject::deleteLater);
    connect(m_service, &Service::snapshot, this, [this](QJsonArray collections, QJsonArray content, QString active, QJsonObject settings, bool undo) {
        const bool first = m_settings.isEmpty();
        const QString oldActive = m_active;
        const bool contentsChanged = m_items != content || m_active != active;
        const bool collectionsChanged = m_shelves != collections;
        m_shelves = collections; m_items = content; m_active = active; m_settings = settings; m_undoAvailable = undo;
        if (oldActive != active) { m_selected.clear(); m_anchor.clear(); }
        QSet<QString> existing;
        for (auto v : m_items) existing.insert(v.toObject()["id"].toString());
        m_selected.intersect(existing);
        if (contentsChanged) { m_itemsCacheValid = false; ++m_selectionRevision; if (!m_selected.isEmpty()) prepareSelection(); emit itemsChanged(); }
        if (collectionsChanged) emit shelvesChanged();
        m_peers.setEnabled(linkEnabled());
        const auto peerSettings = settings["peers"].toObject();
        for (auto it = peerSettings.begin(); it != peerSettings.end(); ++it) m_peers.setPeerEnabled(it.key(), it.value().toBool(true));
        if (first) {
            m_dock = settings["dockEdge"].toString();
            if (shortcutWarning().isEmpty()) m_desktop.configureShortcut(shortcut(), ArcadeLink::executablePath());
            applyWindowPreferences();
        }
        emit changed();
    });
    connect(m_service, &Service::feedback, this, [this](QString message, bool reveal) {
        feedback(message);
        if (reveal) { m_visible = true; emit changed(); emit pulse(); } // No requestActivate on noninteractive add.
    });
    connect(m_service, &Service::showRequested, this, [this](QString, bool settings) { if (settings) showSettings(); else show(); });
    connect(m_service, &Service::toggleRequested, this, &Controller::toggle);
    connect(m_service, &Service::pickRequested, this, [this](bool multiple, QStringList types) { m_picking = true; m_pickMultiple = multiple; m_pickTypes = types; m_itemsCacheValid = false; clearSelection(); emit itemsChanged(); show(); });
    connect(m_service, &Service::pickEnded, this, [this] { m_picking = false; m_pickTypes.clear(); m_itemsCacheValid = false; clearSelection(); emit itemsChanged(); collapse(); });
    connect(m_service, &Service::quitRequested, qApp, &QCoreApplication::quit);
    connect(m_service, &Service::fatalError, this, [](QString error) { qCritical("Shelf startup failed: %s", qPrintable(error)); QCoreApplication::exit(1); });
    connect(&m_peers, &PeerHub::registryChanged, this, &Controller::changed);
    connect(&m_peers, &PeerHub::busyChanged, this, &Controller::changed);
    connect(&m_peers, &PeerHub::progress, this, [this](double fraction, QString message) { m_progress = fraction; feedback(message); });
    connect(&m_peers, &PeerHub::finished, this, [this](QJsonObject result, ArcadeLink::Error error) {
        m_progress = -1;
        QMetaObject::invokeMethod(m_service, [this] { m_service->release(); });
        if (error.isError()) { feedback(error.userMessage("Arcade app")); return; }
        const QJsonArray outputs = result["outputs"].toArray();
        QJsonArray imports;
        for (auto v : outputs) {
            const auto o = v.toObject(); const auto type = o["type"].toString();
            if (type.startsWith("file/") || type == "folder/reference" || type.startsWith("text/")) imports << o;
            else if (type.startsWith("structured/") && type != "structured/region" && type != "structured/pipelines") {
                const auto data = o["data"];
                m_analysis = {{"title",type == "structured/file-info" ? "File inspection" : "Analysis results"},{"message",result["message"]},{"formatted",QString::fromUtf8(QJsonDocument(data.isArray() ? QJsonDocument(data.toArray()) : QJsonDocument(data.toObject())).toJson(QJsonDocument::Indented))}};
            }
        }
        if (!imports.isEmpty()) QMetaObject::invokeMethod(m_service, [this, imports] { m_service->add(imports); });
        if (!result["message"].toString().isEmpty()) feedback(result["message"].toString());
        else if (imports.isEmpty()) feedback("Action completed.");
        emit changed();
    });
    connect(&m_desktop, &Desktop::activated, this, &Controller::toggle);
    connect(&m_desktop, &Desktop::shortcutStatusChanged, this, &Controller::changed);
    connect(&m_desktop, &Desktop::errorOccurred, this, &Controller::feedback);
    connect(&m_desktop, &Desktop::geometryObserved, this, [this](QRect actual,QString screen) {
        auto geometry = m_settings["geometry"].toObject();
        geometry["x"] = actual.x(); geometry["y"] = actual.y(); geometry["screen"] = screen;
        if (m_expanded) { geometry["width"] = actual.width(); geometry["height"] = actual.height(); }
        saveSetting("geometry",geometry);
    });
}
Controller::~Controller()
{
    m_peers.cancel();
    if (m_thread.isRunning()) { QMetaObject::invokeMethod(m_service, &Service::stop, Qt::BlockingQueuedConnection); m_thread.quit(); m_thread.wait(); }
}
void Controller::start() { m_thread.start(); QMetaObject::invokeMethod(m_service, &Service::start); m_peers.start(); }
void Controller::attachWindow(QWindow *window)
{
    m_window = window;
    if (!window) return;
    window->installEventFilter(this);
    m_desktop.setAlwaysOnTop(window, alwaysOnTop());
    if (!m_settings.isEmpty()) applyWindowPreferences();
}
void Controller::applyWindowPreferences()
{
    if (!m_window) return;
    const auto g = m_settings["geometry"].toObject();
    m_window->setProperty("panelWidth",qBound(440,g["width"].toInt(640),2000));
    m_window->setProperty("panelHeight",qBound(350,g["height"].toInt(490),1600));
    if (g.contains("x")) m_desktop.restoreGeometry(m_window,QRect(g["x"].toInt(),g["y"].toInt(),m_window->width(),m_window->height()),g["screen"].toString());
    m_desktop.setAlwaysOnTop(m_window,alwaysOnTop());
    if (!m_dock.isEmpty()) QTimer::singleShot(0,this,[this] { m_desktop.dock(m_window,m_dock); });
}
QVariantMap Controller::preferences() const
{
    auto preferences = m_settings.toVariantMap();
    const auto locations = ArcadeLink::Locations::discover();
    preferences["dataPath"] = m_root;
    preferences["registryPath"] = locations.registry;
    preferences["runtimePath"] = locations.runtime;
    preferences["handoffPath"] = locations.handoff;
    preferences["version"] = Version;
    preferences["license"] = "MIT · Qt under LGPLv3 (dynamic linking)";
    return preferences;
}
bool Controller::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_window || m_picking) return QObject::eventFilter(watched,event);
    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
        auto *drag = static_cast<QDragMoveEvent *>(event);
        if (drag->mimeData()->hasImage() && !drag->mimeData()->hasUrls()) { drag->acceptProposedAction(); return true; }
    } else if (event->type() == QEvent::Drop) {
        auto *drop = static_cast<QDropEvent *>(event);
        const auto *mime = drop->mimeData();
        if (mime->hasFormat("application/x-arcade-shelf-items")) {
            const auto marker = QJsonDocument::fromJson(mime->data("application/x-arcade-shelf-items")).object();
            const QString profile = QString::fromLatin1(QCryptographicHash::hash(m_root.toUtf8(),QCryptographicHash::Sha256).toHex());
            if (marker["profile"].toString() == profile) {
                QString before;
                QVariant beforeValue;
                QMetaObject::invokeMethod(m_window,"itemBeforeAt",Q_RETURN_ARG(QVariant,beforeValue),Q_ARG(QVariant,drop->position().x()),Q_ARG(QVariant,drop->position().y()));
                before = beforeValue.toString();
                QStringList moving, order;
                for (auto id : marker["ids"].toArray()) if (!item(id.toString()).isEmpty()) moving << id.toString();
                for (auto v : m_items) if (!moving.contains(v.toObject()["id"].toString())) order << v.toObject()["id"].toString();
                if (!moving.contains(before)) { int index = order.indexOf(before); if (index < 0) index = order.size(); for (const auto &id : moving) order.insert(index++,id); const auto active = m_active; QMetaObject::invokeMethod(m_service,[this,active,order] { m_service->reorder(active,order); }); }
                drop->setDropAction(Qt::CopyAction); drop->accept(); return true;
            }
        }
        if (mime->hasImage() && !mime->hasUrls()) { stageImage(qvariant_cast<QImage>(mime->imageData())); drop->acceptProposedAction(); return true; }
    }
    return QObject::eventFilter(watched,event);
}
QString Controller::activeName() const { for (auto v : m_shelves) if (v.toObject()["id"] == m_active) return v.toObject()["name"].toString(); return "Quick Shelf"; }
bool Controller::eligible(const QJsonObject &o) const
{
    if (!m_picking || m_pickTypes.isEmpty()) return true;
    const QJsonObject content{{"type", o["type"]}};
    for (const auto &pattern : m_pickTypes) if (ArcadeLink::contentMatches(pattern, content)) return true;
    return false;
}
QVariantList Controller::items() const
{
    if (m_itemsCacheValid) return m_itemsCache;
    m_itemsCache.clear();
    m_itemsCache.reserve(m_items.size());
    for (auto v : m_items) {
        auto o = v.toObject();
        if (!eligible(o)) continue;
        if (!m_filter.isEmpty() && !o["name"].toString().contains(m_filter, Qt::CaseInsensitive) && !o["text"].toString().contains(m_filter, Qt::CaseInsensitive) && !o["path"].toString().contains(m_filter, Qt::CaseInsensitive)) continue;
        if (o["type"] == "file/image" && o["available"].toBool()) o["thumbnail"] = "image://thumb/" + QString::fromLatin1(QUrl::toPercentEncoding(o["path"].toString()));
        m_itemsCache << o.toVariantMap();
    }
    m_itemsCacheValid = true;
    return m_itemsCache;
}
QJsonObject Controller::item(QString id) const { for (auto v : m_items) if (v.toObject()["id"] == id) return v.toObject(); return {}; }
QStringList Controller::selectedIds() const { QStringList ids; for (auto v : m_items) if (m_selected.contains(v.toObject()["id"].toString())) ids << v.toObject()["id"].toString(); return ids; }
QJsonArray Controller::cachedContents() const
{
    QJsonArray result, paths;
    QString kind;
    qint64 total = 0;
    for (auto v : m_items) {
        auto o = v.toObject(); if (!m_selected.contains(o["id"].toString())) continue;
        const QString type = o["type"].toString() == "text/rich" ? QStringLiteral("text/plain") : o["type"].toString();
        if (type.startsWith("file/")) { paths << o["path"]; if (kind.isEmpty()) kind = type; else if (kind != type) kind = "file/any"; total += o["size"].toInteger(); }
        else if (type == "folder/reference") result << QJsonObject{{"type", type}, {"path", o["path"]}};
        else { QJsonObject c{{"type", type}, {"size", o["size"]}}; if (o["size"].toInteger() > 256 * 1024) c["path"] = o["path"]; else c["text"] = o["text"]; if (type == "text/rich") c["html"] = o["html"]; result << c; }
    }
    if (paths.size() == 1) result.prepend(QJsonObject{{"type", kind}, {"path", paths[0]}, {"size", total}});
    else if (!paths.isEmpty()) result.prepend(QJsonObject{{"type", kind + "[]"}, {"paths", paths}, {"size", total}});
    return result;
}
QVariantList Controller::offers() const { return m_peers.offers(cachedContents()).toVariantList(); }
QVariantList Controller::peers() const
{
    QVariantList rows; QSet<QString> known;
    for (auto v : m_peers.manifests()) {
        auto o = v.toObject(); if (o["id"] == AppId) continue;
        const QString id = o["id"].toString(); known.insert(id);
        rows << QVariantMap{{"id", id}, {"name", o["name"].toString()}, {"version", o["version"].toString()}, {"installed", true}, {"running", o["running"].toBool()}, {"enabled", m_settings["peers"].toObject()[id].toBool(true)}, {"linkEnabled", o["settings"].toObject()["linkEnabled"].toBool(true)}};
    }
    for (const auto &id : QStringList{"arcade.look", "arcade.lens", "arcade.box", "arcade.wheel", "arcade.clipboard", "arcade.find", "arcade.tools"}) {
        if (!known.contains(id)) rows << QVariantMap{{"id", id}, {"name", id == "arcade.find" ? "Arcade Find" : ArcadeLink::appName(id)}, {"installed", false}, {"enabled", true}, {"linkEnabled", false}};
    }
    return rows;
}
QString Controller::shortcutWarning() const
{
    const QString normalized = ArcadeLink::normalizeAccelerator(shortcut());
    for (auto v : m_peers.manifests()) { auto app = v.toObject(); if (app["id"] == AppId) continue; for (auto s : app["shortcuts"].toArray()) if (!normalized.isEmpty() && ArcadeLink::normalizeAccelerator(s.toObject()["accelerator"].toString()) == normalized) return "Used by " + app["name"].toString(); }
    return {};
}
void Controller::saveSetting(QString key, QJsonValue value) { if (m_settings[key] == value) return; m_settings[key] = value; auto settings = m_settings; QMetaObject::invokeMethod(m_service, [this, settings] { m_service->saveSettings(settings); }); emit changed(); }
void Controller::feedback(QString message) { m_message = std::move(message); emit changed(); }
void Controller::setFilter(QString value) { if (m_filter == value) return; m_filter = std::move(value); m_itemsCacheValid = false; emit itemsChanged(); emit changed(); }
void Controller::setTheme(QString value) { if (QStringList{"system", "dark", "light"}.contains(value)) saveSetting("theme", value); }
void Controller::setShortcut(QString value) { if (value.size() > 100) return; saveSetting("shortcut", value); if (shortcutWarning().isEmpty()) m_desktop.configureShortcut(value, ArcadeLink::executablePath()); else feedback(shortcutWarning()); }
void Controller::setExpanded(bool value) { if (value) show(); else collapse(); }
void Controller::setSettingsOpen(bool value) { m_settingsOpen = value; if (value) show(); emit changed(); }
void Controller::setLinkEnabled(bool value) { saveSetting("linkEnabled", value); m_peers.setEnabled(value); }
void Controller::setAlwaysOnTop(bool value) { saveSetting("alwaysOnTop", value); if (m_window) m_desktop.setAlwaysOnTop(m_window, value); }
void Controller::setReducedMotion(bool value) { saveSetting("reducedMotion", value); }
void Controller::toggle() { if (m_expanded) collapse(); else show(); }
void Controller::show() { m_visible = true; m_expanded = true; m_dock.clear(); emit changed(); if (m_window) { m_window->show(); m_window->raise(); m_window->requestActivate(); } QMetaObject::invokeMethod(m_service, &Service::refresh); }
void Controller::collapse() { if (m_picking) { cancelPick(); return; } m_expanded = false; m_settingsOpen = false; emit changed(); }
void Controller::acceptDrop(QVariant urls, QString text, QString html)
{
    QJsonArray inputs;
    if (urls.metaType() == QMetaType::fromType<QJSValue>()) urls = urls.value<QJSValue>().toVariant();
    for (auto v : urls.toList()) {
        while (v.metaType() == QMetaType::fromType<QVariant>()) v = v.value<QVariant>();
        if (v.metaType() == QMetaType::fromType<QJSValue>()) v = v.value<QJSValue>().toVariant();
        const QUrl url(v.toUrl());
        if (url.isLocalFile()) inputs << ArcadeLink::fileContent(url.toLocalFile());
        else if (url.isValid() && (url.scheme() == "https" || url.scheme() == "http")) inputs << ArcadeLink::textContent("text/url", url.toString());
    }
    if (inputs.isEmpty() && !text.isEmpty()) {
        const QUrl url(text.trimmed());
        QString type = !html.isEmpty() ? "text/rich" : ((url.isValid() && (url.scheme() == "http" || url.scheme() == "https") && !text.contains('\n')) ? "text/url" : "text/plain");
        QJsonObject c = ArcadeLink::textContent(type, text); if (!html.isEmpty()) c["html"] = html; inputs << c;
    }
    if (inputs.isEmpty()) { feedback("This content cannot be staged."); return; }
    QMetaObject::invokeMethod(m_service, [this, inputs] {
        QJsonArray normalized;
        QStringList generated;
        for (auto v : inputs) {
            auto o = v.toObject(); const auto bytes = o["text"].toString().toUtf8();
            if (o["type"].toString().startsWith("text/") && bytes.size() + o["html"].toString().toUtf8().size() > 256*1024) {
                if (bytes.size() > 16*1024*1024) { m_service->add({}); return; }
                const QString dir = ArcadeLink::Locations::discover().handoff + "/" + QUuid::createUuid().toString(QUuid::Id128);
                if (!ArcadeLink::ensurePrivateDir(dir)) continue;
                const QString path = dir + "/text.txt";
                QSaveFile file(path);
                if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner|QFile::WriteOwner) || file.write(bytes) != bytes.size() || !file.commit()) { QDir().rmdir(dir); continue; }
                o.remove("text"); o["path"] = path; o["owner"] = AppId; generated << path;
            }
            normalized << o;
        }
        m_service->add(normalized);
        for (const auto &path : generated) { QFile::remove(path); QDir().rmdir(QFileInfo(path).absolutePath()); }
    });
}
void Controller::stageImage(QImage image)
{
    if (image.isNull() || image.sizeInBytes() > m_settings["quotaBytes"].toInteger(512*1024*1024)) { feedback("This image exceeds the storage limit."); return; }
    QMetaObject::invokeMethod(m_service,[this,image] {
        const QString dir = ArcadeLink::Locations::discover().handoff + "/" + QUuid::createUuid().toString(QUuid::Id128);
        if (!ArcadeLink::ensurePrivateDir(dir)) { emit m_service->feedback("Could not stage this image.",false); return; }
        const QString path = dir + "/image.png";
        if (!image.save(path,"PNG")) { QDir().rmdir(dir); emit m_service->feedback("Could not stage this image.",false); return; }
        QFile::setPermissions(path,QFile::ReadOwner|QFile::WriteOwner);
        auto input = ArcadeLink::fileContent(path); input["owner"] = AppId;
        m_service->add({input}); QFile::remove(path); QDir().rmdir(dir);
    });
}
void Controller::paste()
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (mime->hasUrls()) { QVariantList urls; for (auto url : mime->urls()) urls << url; acceptDrop(urls, {}, {}); }
    else if (mime->hasImage()) {
        stageImage(qvariant_cast<QImage>(mime->imageData()));
    } else acceptDrop({}, mime->text(), mime->html());
}
void Controller::select(QString id, bool additive, bool range)
{
    if (item(id).isEmpty() || !eligible(item(id))) return;
    if (m_picking && !m_pickMultiple) { m_selected = {id}; }
    else if (range && !m_anchor.isEmpty()) {
        auto visibleItems = items(); int a = -1, b = -1; for (int i = 0; i < visibleItems.size(); ++i) { auto key = visibleItems[i].toMap()["id"].toString(); if (key == m_anchor) a = i; if (key == id) b = i; }
        if (!additive) m_selected.clear(); if (a >= 0 && b >= 0) for (int i = qMin(a,b); i <= qMax(a,b); ++i) m_selected.insert(visibleItems[i].toMap()["id"].toString());
    } else { if (!additive) m_selected.clear(); if (additive && m_selected.contains(id)) m_selected.remove(id); else m_selected.insert(id); m_anchor = id; }
    ++m_selectionRevision; prepareSelection(); emit changed();
}
void Controller::prepareSelection()
{
    m_dragContent = {}; m_dragRevision = -1;
    const auto ids = selectedIds(); const auto revision = m_selectionRevision;
    QPointer<Controller> guard(this);
    QMetaObject::invokeMethod(m_service,[this,ids,revision,guard] {
        m_service->selectedContents(ids,[guard,revision](QJsonArray content,ArcadeLink::Error error) {
            if (!guard) return;
            QMetaObject::invokeMethod(guard,[guard,revision,content,error] { if (guard && revision == guard->m_selectionRevision && !error.isError()) { guard->m_dragContent = content; guard->m_dragRevision = revision; } });
        });
    });
}
void Controller::selectAll() { if (m_picking && !m_pickMultiple) return; m_selected.clear(); for (auto v : items()) m_selected.insert(v.toMap()["id"].toString()); ++m_selectionRevision; prepareSelection(); emit changed(); }
void Controller::clearSelection() { m_selected.clear(); m_dragContent = {}; m_dragRevision = -1; ++m_selectionRevision; emit changed(); }
void Controller::removeSelected() { if (m_picking) return; auto ids = selectedIds(); QMetaObject::invokeMethod(m_service, [this, ids] { m_service->remove(ids); }); clearSelection(); }
void Controller::undo() { QMetaObject::invokeMethod(m_service, &Service::undo); }
void Controller::openItem(QString id) { auto o = item(id); if (!o["available"].toBool(true)) { feedback("The original is unavailable."); return; } if (o["type"] == "text/url") QDesktopServices::openUrl(QUrl(o["text"].toString())); else if (!o["path"].toString().isEmpty()) m_desktop.open(o["path"].toString()); }
void Controller::revealItem(QString id) { auto path = item(id)["path"].toString(); if (!path.isEmpty()) m_desktop.reveal(path); }
void Controller::copySelected(QString mode)
{
    const auto ids = selectedIds();
    QPointer<Controller> guard(this);
    QMetaObject::invokeMethod(m_service, [this, ids, mode, guard] {
        m_service->selectedContents(ids, [guard, mode](QJsonArray content, ArcadeLink::Error error) {
            if (!guard) return;
            QMetaObject::invokeMethod(guard, [guard, mode, content, error] {
                if (!guard) return; if (error.isError()) { guard->feedback(error.userMessage("Arcade Shelf")); return; }
                auto *mime = new QMimeData; QList<QUrl> urls; QStringList text; QString html;
                for (auto v : content) { auto o = v.toObject(); const QString type = o["type"].toString(); if (type.startsWith("file/") || type == "folder/reference") { if (o.contains("path")) urls << QUrl::fromLocalFile(o["path"].toString()); for (auto p : o["paths"].toArray()) urls << QUrl::fromLocalFile(p.toString()); } else { QString t = o["text"].toString(); if (o.contains("path")) { QFile f(o["path"].toString()); if (f.open(QIODevice::ReadOnly) && f.size() <= 16 * 1024 * 1024) t = QString::fromUtf8(f.readAll()); } text << t; html += o["html"].toString(); } }
                if (mode == "paths") { for (auto url : urls) text << url.toLocalFile(); }
                else if (mode == "content" && !urls.isEmpty()) mime->setUrls(urls);
                if (!text.isEmpty()) mime->setText(text.join('\n')); if (!html.isEmpty() && mode != "paths") mime->setHtml(html);
                QGuiApplication::clipboard()->setMimeData(mime); guard->feedback("Copied.");
            });
        });
    });
}
void Controller::dragSelected()
{
    // Native drag must begin synchronously within the originating pointer gesture.
    // References and bounded inline text are already cached; no IPC or file read here.
    if (!m_window || m_selected.isEmpty()) return;
    auto contents = cachedContents();
    bool needsPrepared = false; for (auto v : m_items) if (m_selected.contains(v.toObject()["id"].toString()) && (v.toObject()["type"].toString().startsWith("text/") || v.toObject()["owned"].toBool())) needsPrepared = true;
    if (needsPrepared) { if (m_dragRevision != m_selectionRevision) { feedback("Preparing selected content. Try again."); return; } contents = m_dragContent; }
    contents << QJsonObject{{"type","structured/shelf-reorder"},{"data",QJsonObject{{"profile",QString::fromLatin1(QCryptographicHash::hash(m_root.toUtf8(),QCryptographicHash::Sha256).toHex())},{"ids",QJsonArray::fromStringList(selectedIds())}}}};
    m_desktop.startDrag(m_window, contents);
}
void Controller::reorderItem(QString id, QString before)
{
    QStringList ids; for (auto v : m_items) if (v.toObject()["id"] != id) ids << v.toObject()["id"].toString(); auto at = ids.indexOf(before); ids.insert(at < 0 ? ids.size() : at, id); auto active = m_active; QMetaObject::invokeMethod(m_service, [this, active, ids] { m_service->reorder(active, ids); });
}
void Controller::createShelf(QString name, bool temporary) { QMetaObject::invokeMethod(m_service, [this,name,temporary] { m_service->createShelf(name,temporary); }); }
void Controller::renameShelf(QString id, QString name) { QMetaObject::invokeMethod(m_service, [this,id,name] { m_service->renameShelf(id,name); }); }
void Controller::deleteShelf(QString id) { QMetaObject::invokeMethod(m_service, [this,id] { m_service->deleteShelf(id); }); }
void Controller::switchShelf(QString id) { m_filter.clear(); clearSelection(); QMetaObject::invokeMethod(m_service, [this,id] { m_service->switchShelf(id); }); }
void Controller::pinShelf(QString id, bool pin) { QMetaObject::invokeMethod(m_service, [this,id,pin] { m_service->pinShelf(id,pin); }); }
void Controller::moveShelf(QString id, int delta) { QStringList ids; for (auto v : m_shelves) ids << v.toObject()["id"].toString(); int a = ids.indexOf(id); if (a < 0) return; int b = qBound(0,a+delta,int(ids.size())-1); ids.move(a,b); QMetaObject::invokeMethod(m_service,[this,ids] { m_service->reorderShelves(ids); }); }
void Controller::peerCall(QString app, QString action, QJsonArray inputs, QJsonObject options)
{
    if (busy()) { feedback("An action is already running."); return; }
    m_progress = -1; m_peers.invoke(app,action,inputs,options); emit changed();
}
void Controller::runPeer(QString app, QString action)
{
    QJsonObject chosen;
    for (auto v : m_peers.offers(cachedContents())) if (v.toObject()["app"] == app && v.toObject()["action"] == action) { chosen = v.toObject(); break; }
    if (chosen.isEmpty() || !chosen["enabled"].toBool(true)) { feedback(chosen["reason"].toString("This action is unavailable.")); return; }
    const auto ids = selectedIds(); const auto options = chosen["options"].toObject(); QPointer<Controller> guard(this);
    QMetaObject::invokeMethod(m_service, [this,ids,guard,app,action,options] {
        m_service->retain(ids);
        m_service->selectedContents(ids, [guard,app,action,options](QJsonArray inputs, ArcadeLink::Error error) {
            if (!guard) return;
            QMetaObject::invokeMethod(guard,[guard,app,action,options,inputs,error] { if (!guard) return; if (error.isError()) { guard->feedback(error.userMessage("Arcade Shelf")); QMetaObject::invokeMethod(guard->m_service,&Service::release); } else guard->peerCall(app,action,inputs,options); });
        });
    });
}
void Controller::runSource(QString app, QString action)
{
    if (app == "arcade.wheel" && action == "wheel.add_action") {
        peerCall(app,action,{QJsonObject{{"type","structured/arcade-action"},{"data",QJsonObject{{"app",AppId},{"action","shelf.show"},{"version",1},{"title","Show " + activeName()},{"options",QJsonObject{{"shelf",m_active}}},{"input","none"}}}}},{}); return;
    }
    for (auto v : m_peers.sources()) if (v.toObject()["app"] == app && v.toObject()["action"] == action) { peerCall(app,action,{},v.toObject()["options"].toObject()); return; }
    feedback("This action is unavailable.");
}
void Controller::cancelJob() { m_peers.cancel(); }
void Controller::confirmPick() { auto ids = selectedIds(); QMetaObject::invokeMethod(m_service,[this,ids] { m_service->completePick(ids); }); }
void Controller::cancelPick() { QMetaObject::invokeMethod(m_service,&Service::cancelPick); }
void Controller::showSettings() { m_settingsOpen = true; show(); }
void Controller::nativeMove() { if (m_window) m_desktop.move(m_window); }
void Controller::nativeResize(int edges) { if (m_window) m_desktop.resize(m_window,edges); }
void Controller::dock(QString edge) { if (!QStringList{"","left","right","top","bottom"}.contains(edge)) return; m_dock = edge; saveSetting("dockEdge",edge); m_desktop.dock(m_window,edge); }
void Controller::saveGeometry(int x,int y,int w,int h) {
    QJsonObject geometry{{"x",x},{"y",y},{"width",m_expanded ? w : m_settings["geometry"].toObject()["width"].toInt(640)},{"height",m_expanded ? h : m_settings["geometry"].toObject()["height"].toInt(490)}};
    if (m_window && m_window->screen()) geometry["screen"] = m_window->screen()->name();
    if (QGuiApplication::platformName() != "wayland") saveSetting("geometry",geometry);
    else { auto saved = m_settings["geometry"].toObject(); saved["width"] = geometry["width"]; saved["height"] = geometry["height"]; saveSetting("geometry",saved); }
    if (m_window) m_desktop.captureGeometry(m_window);
}
QVariantMap Controller::restoreGeometry() const { auto o = m_settings["geometry"].toObject(); if (!o.contains("width")) o["width"] = 620; if (!o.contains("height")) o["height"] = 480; o["dockEdge"] = m_dock; return o.toVariantMap(); }
void Controller::setPeerEnabled(QString id,bool enabled) { auto p = m_settings["peers"].toObject(); p[id] = enabled; saveSetting("peers",p); m_peers.setPeerEnabled(id,enabled); }
void Controller::getPeer(QString id) {
    const auto toolsPolicy = m_settings["peers"].toObject()["arcade.tools"].toBool(true);
    if (linkEnabled() && toolsPolicy) {
        for (auto v : m_peers.manifests()) {
            const auto app = v.toObject(); if (app["id"] != "arcade.tools") continue;
            for (auto a : app["actions"].toArray()) if (a.toObject()["id"] == "tools.install" && ArcadeLink::actionUsable(app,a.toObject())) { peerCall("arcade.tools","tools.install",{ArcadeLink::textContent("text/plain",id)},{{"app",id}}); return; }
        }
    }
    if (id == "arcade.find") QDesktopServices::openUrl(QUrl("https://github.com/qa-p1/Arcade-Find/releases")); else QDesktopServices::openUrl(QUrl(ArcadeLink::releasesUrl(id)));
}
void Controller::setRetention(int days) { if (days >= 0 && days <= 3650) saveSetting("retentionDays",days); }
void Controller::setQuota(int mib) { if (mib >= 1 && mib <= 4096) saveSetting("quotaBytes",qint64(mib)*1024*1024); }
void Controller::setStartAtLogin(bool value) { if (m_desktop.setStartAtLogin(value,ArcadeLink::executablePath())) saveSetting("startAtLogin",value); else feedback(m_desktop.lastError()); }
}
