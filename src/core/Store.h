#pragma once

#include "ArcadeLink.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QStringList>

namespace Shelf {

struct AddResult {
    QJsonObject result;
    ArcadeLink::Error error;
    QStringList addedIds;
};

// Thread-confined. Construct, use, and destroy a Store on the same worker.
class Store {
public:
    explicit Store(QString root);
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    bool open(QString *error = nullptr);
    QString root() const { return m_root; }
    QJsonArray shelves();
    QJsonArray items(QString shelfId);
    QString activeShelf();
    bool setActiveShelf(QString id);
    AddResult add(QJsonArray inputs, QJsonObject options = {});
    bool remove(QStringList ids);
    bool reorder(QString shelfId, QStringList ids);
    QString createShelf(QString name, bool temporary = false);
    bool renameShelf(QString id, QString name);
    bool deleteShelf(QString id);
    bool pinShelf(QString id, bool pin);
    bool reorderShelves(QStringList ids);
    QJsonObject settings();
    bool saveSettings(QJsonObject settings);
    QJsonArray exportItems(QStringList ids, QStringList types = {}, bool multiple = true,
                           ArcadeLink::Error *error = nullptr);
    bool cleanup();
    qint64 ownedBytes();

private:
    QString m_root;
    QString m_connection;
    QSqlDatabase m_db;
    QHash<QString, QString> m_handoffs;
    qint64 m_handoffBytes = 0;
    bool exec(const QString &sql);
    bool hasShelf(const QString &id);
    QJsonObject item(const QString &id);
    bool removeOwnedPath(const QString &path);
    bool privateOwnedDirectory(const QString &path);
    QString textHandoff(const QString &id, const QByteArray &bytes, ArcadeLink::Error *error);
    QString fileHandoff(const QString &id, const QString &path, qint64 size, ArcadeLink::Error *error);
    QString writeHandoff(const QString &key, const QString &name, const QByteArray &bytes,
                         const QString &source, qint64 size, ArcadeLink::Error *error);
};

} // namespace Shelf
