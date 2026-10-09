#pragma once
#include "core/Store.h"
#include "ArcadeLink.h"
#include <QObject>
#include <QTimer>
#include <optional>

namespace Shelf {
inline const QString AppId = QStringLiteral("arcade.shelf");
inline const QString Version = QStringLiteral("0.1.0");
QJsonArray publicActions();
QJsonObject manifest(bool enabled = true, QString shortcut = QStringLiteral("Ctrl+Alt+S"));
QString dataRoot();

// This object, both servers, and SQLite stay on one dedicated event-loop thread.
class Service : public QObject {
    Q_OBJECT
public:
    explicit Service(QString root, QObject *parent = nullptr, bool background = true);
    void start();
    void stop();
    void refresh();
    void add(QJsonArray inputs, QJsonObject options = {});
    void remove(QStringList ids);
    void undo();
    void reorder(QString shelf, QStringList ids);
    void switchShelf(QString id);
    void createShelf(QString name, bool temporary);
    void renameShelf(QString id, QString name);
    void deleteShelf(QString id);
    void pinShelf(QString id, bool pin);
    void reorderShelves(QStringList ids);
    void saveSettings(QJsonObject settings);
    void completePick(QStringList ids);
    void cancelPick();
    void selectedContents(QStringList ids, std::function<void(QJsonArray, ArcadeLink::Error)> callback);
    void retain(QStringList ids);
    void release();
signals:
    void snapshot(QJsonArray shelves, QJsonArray items, QString active, QJsonObject settings, bool undoAvailable);
    void feedback(QString message, bool pulse);
    void showRequested(QString shelf, bool settings);
    void toggleRequested();
    void pickRequested(bool multiple, QStringList types);
    void pickEnded();
    void quitRequested();
    void fatalError(QString error);
private:
    void invoke(const QJsonObject &req, const ArcadeLink::Responder &r);
    void publish();
    bool target(QString id);
    void finishAdd(const AddResult &result, bool reveal);
    QString m_root;
    std::unique_ptr<Store> m_store;
    std::unique_ptr<ArcadeLink::Server> m_server, m_control;
    std::optional<ArcadeLink::Responder> m_picker;
    QTimer m_pickTimer;
    QStringList m_pickTypes, m_undo, m_retained, m_deferredRemoval;
    bool m_multiple = false;
    bool m_background = true;
};
}
