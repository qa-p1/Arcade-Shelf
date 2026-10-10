#include "Store.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringDecoder>
#include <QUuid>
#include <QUrl>
#include <limits>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Shelf {
namespace {
constexpr qint64 InlineLimit = 256 * 1024;
constexpr qint64 DefaultQuota = 512LL * 1024 * 1024;
constexpr qint64 MaxText = 16LL * 1024 * 1024;
constexpr int MaxInputs = 1000;

QString uid(const QString &prefix = {})
{
    return prefix + QUuid::createUuid().toString(QUuid::Id128);
}

ArcadeLink::Error failure(const QString &code, const QString &reason = {}, qint64 limit = -1)
{
    return ArcadeLink::Error::make(code, ArcadeLink::standardMessage(code, QStringLiteral("Arcade Shelf"), reason, limit), reason, limit);
}

bool safeString(const QString &s)
{
    // Unpaired UTF-16 surrogates would be replaced during UTF-8 encoding and
    // could accidentally refer to a different path on Unix.
    return !s.isEmpty() && !s.contains(QChar::Null) && QString::fromUtf8(s.toUtf8()) == s;
}
bool beneath(const QString &root, const QString &path)
{
    return QDir::cleanPath(path).startsWith(QDir::cleanPath(root) + QLatin1Char('/'));
}

struct PathInfo {
    bool exists = false, directory = false, regular = false, symlink = false;
    qint64 size = 0;
};

// QFileInfo::exists follows a symlink and misses dangling references; lstat does not.
PathInfo inspect(const QString &path)
{
    PathInfo result;
#ifdef Q_OS_UNIX
    struct stat st {};
    const QByteArray encoded = QFile::encodeName(path);
    if (::lstat(encoded.constData(), &st) != 0) return result;
    result.exists = true;
    result.directory = S_ISDIR(st.st_mode);
    result.regular = S_ISREG(st.st_mode);
    result.symlink = S_ISLNK(st.st_mode);
    result.size = result.regular ? st.st_size : 0;
#else
    const QFileInfo f(path);
    result.symlink = f.isSymLink();
    result.exists = result.symlink || f.exists();
    result.directory = !result.symlink && f.isDir();
    result.regular = !result.symlink && f.isFile();
    result.size = result.regular ? f.size() : 0;
#endif
    return result;
}

bool openRegular(QFile &file, QIODevice::OpenMode mode = QIODevice::ReadOnly)
{
#ifdef Q_OS_UNIX
    // A peer can replace a validated path before open(). O_NONBLOCK prevents a
    // substituted FIFO from hanging the worker; fstat validates the descriptor
    // we actually received, and O_NOFOLLOW preserves handoff ownership rules.
    const int access = mode.testFlag(QIODevice::WriteOnly) ? O_RDWR : O_RDONLY;
    const int fd = ::open(QFile::encodeName(file.fileName()).constData(),access | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
    if (fd < 0) return false;
    struct stat st {};
    if (::fstat(fd,&st) != 0 || !S_ISREG(st.st_mode) || !file.open(fd,mode,QFileDevice::AutoCloseHandle)) {
        ::close(fd); return false;
    }
    return true;
#else
    const auto info = inspect(file.fileName());
    return info.regular && !info.symlink && file.open(mode);
#endif
}

QString referenceKey(QString path)
{
#ifdef Q_OS_WIN
    path = QDir::fromNativeSeparators(path).toCaseFolded();
#endif
    // Never canonicalize: two symlinks are two references, including dangling ones.
    return path;
}

QString safeName(QString name, const QString &fallback)
{
    if (!name.isEmpty()) name = QFileInfo(name).fileName();
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]")), QStringLiteral("_"));
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..")) name = fallback;
    return name.left(180);
}

bool validName(const QString &name) { return safeString(name.trimmed()) && name.size() <= 100; }

bool privateDirectoryBoundary(const QString &path)
{
    if (!QDir::isAbsolutePath(path) || QFileInfo(path).isSymLink()) return false;
#ifdef Q_OS_UNIX
    struct stat st {};
    if (::lstat(QFile::encodeName(path).constData(),&st) != 0) return false;
    return S_ISDIR(st.st_mode) && st.st_uid == ::getuid() && (st.st_mode & 0077) == 0;
#else
    return QFileInfo(path).isDir();
#endif
}

bool isHandoff(const QJsonObject &content, const QString &path)
{
    const auto locations = ArcadeLink::Locations::discover();
    if (!content.value(QStringLiteral("owner")).isString()) return false;
    const auto owner = content.value(QStringLiteral("owner")).toString();
    if (!QRegularExpression(QStringLiteral("^arcade\\.[a-z][a-z0-9.-]{0,63}$")).match(owner).hasMatch()) return false;
    if (!beneath(locations.handoff, path)) return false;
    const QFileInfo file(path);
    if (file.isSymLink()) return false;
    const QString actual = file.canonicalFilePath();
    const QString base = QFileInfo(locations.handoff).canonicalFilePath();
    return !base.isEmpty() && beneath(base, actual);
}

QJsonObject rowItem(QSqlQuery &q, bool preview = false)
{
    QJsonObject result{{QStringLiteral("id"), q.value(0).toString()},
                       {QStringLiteral("shelf"), q.value(1).toString()},
                       {QStringLiteral("type"), q.value(2).toString()},
                       {QStringLiteral("name"), q.value(3).toString()},
                       {QStringLiteral("path"), q.value(4).toString()},
                       {QStringLiteral("text"), q.value(5).toString()},
                       {QStringLiteral("html"), q.value(6).toString()},
                       {QStringLiteral("size"), q.value(7).toLongLong()},
                       {QStringLiteral("owned"), q.value(8).toBool()},
                       {QStringLiteral("position"), q.value(9).toInt()},
                       {QStringLiteral("created"), q.value(10).toLongLong()}};
    const QString path = result.value(QStringLiteral("path")).toString();
    result.insert(QStringLiteral("available"), path.isEmpty() || inspect(path).exists);
    if (preview) {
        const auto text = result.value(QStringLiteral("text")).toString();
        const auto html = result.value(QStringLiteral("html")).toString();
        result.insert(QStringLiteral("textTruncated"),text.size() > 2048 ||
            (result.value(QStringLiteral("type")).toString().startsWith(QStringLiteral("text/")) && !path.isEmpty() && result.value(QStringLiteral("size")).toInteger() > InlineLimit));
        result.insert(QStringLiteral("text"),text.left(2048));
        result.insert(QStringLiteral("html"),html.left(2048));
    }
    return result;
}
const QString ItemColumns = QStringLiteral("id,shelf,type,name,path,text,html,size,owned,position,created");
} // namespace

Store::Store(QString root) : m_root(QDir::cleanPath(std::move(root))), m_connection(uid(QStringLiteral("shelf-"))) {}

Store::~Store()
{
    if (m_db.isValid()) m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connection);
}

bool Store::exec(const QString &sql) { QSqlQuery q(m_db); return q.exec(sql); }

bool Store::privateOwnedDirectory(const QString &path)
{
    if (!QDir::isAbsolutePath(path) || QFileInfo(path).isSymLink()) return false;
#ifdef Q_OS_UNIX
    struct stat st {};
    const QByteArray encoded = QFile::encodeName(path);
    if (::lstat(encoded.constData(), &st) == 0 && (!S_ISDIR(st.st_mode) || st.st_uid != ::getuid())) return false;
#endif
    return ArcadeLink::ensurePrivateDir(path);
}

bool Store::ownedStorageSafe() const
{
    // Recheck before owned I/O. A resident process can outlive a renamed data
    // directory; an ancestor alias must not redirect cleanup into user files.
    const QString objects = m_root + QStringLiteral("/objects");
    if (!m_boundariesRecorded || !privateDirectoryBoundary(m_root) || !privateDirectoryBoundary(objects) ||
        QFileInfo(m_root).canonicalFilePath() != m_canonicalRoot || QFileInfo(objects).canonicalFilePath() != m_canonicalObjects) return false;
#ifdef Q_OS_UNIX
    struct stat rootStat {}, objectsStat {};
    if (::lstat(QFile::encodeName(m_root).constData(),&rootStat) != 0 || ::lstat(QFile::encodeName(objects).constData(),&objectsStat) != 0) return false;
    if (static_cast<quint64>(rootStat.st_dev) != m_rootDevice || static_cast<quint64>(rootStat.st_ino) != m_rootInode ||
        static_cast<quint64>(objectsStat.st_dev) != m_objectsDevice || static_cast<quint64>(objectsStat.st_ino) != m_objectsInode) return false;
#endif
    return true;
}

bool Store::ownedObjectSafe(const QString &path) const
{
    if (!ownedStorageSafe()) return false;
    const QString objects = m_root + QStringLiteral("/objects");
    const auto info = inspect(path);
    const QString actualRoot = QFileInfo(objects).canonicalFilePath();
    return info.regular && !info.symlink && beneath(objects,path) && !actualRoot.isEmpty() &&
        beneath(actualRoot,QFileInfo(path).canonicalFilePath());
}

bool Store::open(QString *error)
{
    if (error) error->clear();
    if (m_db.isOpen()) return true;
    const auto fail = [&](const QString &message) { if (error) *error = message; m_db.close(); return false; };
    if (!privateOwnedDirectory(m_root) || !privateOwnedDirectory(m_root + QStringLiteral("/objects")))
        return fail(QStringLiteral("Shelf storage must be a private directory owned by the current user."));
    m_boundariesRecorded = false;
    m_canonicalRoot = QFileInfo(m_root).canonicalFilePath();
    m_canonicalObjects = QFileInfo(m_root + QStringLiteral("/objects")).canonicalFilePath();
    if (m_canonicalRoot.isEmpty() || m_canonicalObjects.isEmpty()) return fail(QStringLiteral("Could not resolve Shelf storage boundaries."));
#ifdef Q_OS_UNIX
    struct stat rootStat {}, objectsStat {};
    if (::lstat(QFile::encodeName(m_root).constData(),&rootStat) != 0 || ::lstat(QFile::encodeName(m_root + QStringLiteral("/objects")).constData(),&objectsStat) != 0)
        return fail(QStringLiteral("Could not inspect Shelf storage boundaries."));
    m_rootDevice = rootStat.st_dev; m_rootInode = rootStat.st_ino;
    m_objectsDevice = objectsStat.st_dev; m_objectsInode = objectsStat.st_ino;
#endif
    m_boundariesRecorded = true;
    const auto database = m_root + QStringLiteral("/shelf.sqlite3");
    if (QFileInfo(database).isSymLink()) return fail(QStringLiteral("Refusing a symlinked Shelf database."));
    if (!m_db.isValid()) m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    m_db.setDatabaseName(database);
    m_db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=2000"));
    if (!m_db.open()) return fail(m_db.lastError().text());
    QFile::setPermissions(database, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!exec(QStringLiteral("PRAGMA foreign_keys=ON")) || !exec(QStringLiteral("PRAGMA journal_mode=WAL")) || !exec(QStringLiteral("PRAGMA synchronous=FULL")))
        return fail(QStringLiteral("Could not configure SQLite durability."));
    QSqlQuery version(m_db);
    if (!version.exec(QStringLiteral("PRAGMA user_version")) || !version.next()) return fail(QStringLiteral("Could not read the storage schema."));
    if (version.value(0).toInt() > 1) return fail(QStringLiteral("This Shelf database was created by a newer version of Arcade Shelf."));
    if (!m_db.transaction()) return fail(m_db.lastError().text());
    const QStringList schema{
        QStringLiteral("CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY,value TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS shelves(id TEXT PRIMARY KEY,name TEXT NOT NULL,temporary INTEGER NOT NULL DEFAULT 0,pinned INTEGER NOT NULL DEFAULT 0,position INTEGER NOT NULL,created INTEGER NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS items(id TEXT PRIMARY KEY,shelf TEXT NOT NULL REFERENCES shelves(id) ON DELETE CASCADE,type TEXT NOT NULL,name TEXT NOT NULL,path TEXT NOT NULL DEFAULT '',text TEXT NOT NULL DEFAULT '',html TEXT NOT NULL DEFAULT '',size INTEGER NOT NULL DEFAULT 0,owned INTEGER NOT NULL DEFAULT 0,position INTEGER NOT NULL,created INTEGER NOT NULL,refkey TEXT)"),
        QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS item_references ON items(shelf,refkey) WHERE refkey IS NOT NULL"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS item_order ON items(shelf,position)"),
        QStringLiteral("INSERT OR IGNORE INTO shelves(id,name,temporary,pinned,position,created) VALUES('quick','Quick Shelf',1,0,0,strftime('%s','now')*1000)"),
        QStringLiteral("INSERT OR IGNORE INTO meta(key,value) VALUES('activeShelf','quick')"),
        QStringLiteral("PRAGMA user_version=1")};
    for (const auto &sql : schema) {
        if (!exec(sql)) { m_db.rollback(); return fail(QStringLiteral("Could not migrate Shelf storage.")); }
    }
    if (!m_db.commit()) return fail(QStringLiteral("Could not commit Shelf storage migration."));
    QSqlQuery check(m_db);
    if (!check.exec(QStringLiteral("PRAGMA quick_check")) || !check.next() || check.value(0).toString() != QStringLiteral("ok"))
        return fail(QStringLiteral("Shelf storage is damaged. Preserve this directory before recovering a backup."));
    if (!hasShelf(activeShelf())) setActiveShelf(QStringLiteral("quick"));
    if (!cleanup()) return fail(QStringLiteral("Could not recover Shelf storage or safely inspect owned handoffs."));
    return true;
}

bool Store::hasShelf(const QString &id)
{
    QSqlQuery q(m_db); q.prepare(QStringLiteral("SELECT 1 FROM shelves WHERE id=?")); q.addBindValue(id);
    return q.exec() && q.next();
}

QJsonArray Store::shelves()
{
    QJsonArray result;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT s.id,s.name,s.temporary,s.pinned,s.position,COUNT(i.id) FROM shelves s LEFT JOIN items i ON i.shelf=s.id GROUP BY s.id ORDER BY s.position,s.created"))) return result;
    while (q.next()) result.append(QJsonObject{{QStringLiteral("id"),q.value(0).toString()},{QStringLiteral("name"),q.value(1).toString()},
        {QStringLiteral("temporary"),q.value(2).toBool()},{QStringLiteral("pinned"),q.value(3).toBool()},
        {QStringLiteral("position"),q.value(4).toInt()},{QStringLiteral("count"),q.value(5).toInt()}});
    return result;
}

QJsonArray Store::items(QString shelfId)
{
    QJsonArray result; QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT ") + ItemColumns + QStringLiteral(" FROM items WHERE shelf=? ORDER BY position,created,id"));
    q.addBindValue(shelfId);
    if (q.exec()) while (q.next()) result.append(rowItem(q,true));
    return result;
}

QJsonObject Store::item(const QString &id)
{
    QSqlQuery q(m_db); q.prepare(QStringLiteral("SELECT ") + ItemColumns + QStringLiteral(" FROM items WHERE id=?")); q.addBindValue(id);
    return q.exec() && q.next() ? rowItem(q) : QJsonObject{};
}

QString Store::activeShelf()
{
    QSqlQuery q(m_db); q.prepare(QStringLiteral("SELECT value FROM meta WHERE key='activeShelf'"));
    return q.exec() && q.next() ? q.value(0).toString() : QStringLiteral("quick");
}

bool Store::setActiveShelf(QString id)
{
    if (!hasShelf(id)) return false;
    QSqlQuery q(m_db); q.prepare(QStringLiteral("INSERT OR REPLACE INTO meta(key,value) VALUES('activeShelf',?)")); q.addBindValue(id); return q.exec();
}

QJsonObject Store::settings()
{
    QJsonObject result{{QStringLiteral("quotaBytes"),DefaultQuota},{QStringLiteral("retentionDays"),30},
        {QStringLiteral("thumbnailQuotaBytes"),64 * 1024 * 1024},{QStringLiteral("linkEnabled"),true},
        {QStringLiteral("theme"),QStringLiteral("system")},{QStringLiteral("alwaysOnTop"),true},{QStringLiteral("reducedMotion"),false}};
    QSqlQuery q(m_db); q.prepare(QStringLiteral("SELECT value FROM meta WHERE key='settings'"));
    if (q.exec() && q.next()) {
        const auto saved = QJsonDocument::fromJson(q.value(0).toByteArray()).object();
        for (auto it = saved.begin(); it != saved.end(); ++it) result.insert(it.key(),it.value());
    }
    return result;
}

bool Store::saveSettings(QJsonObject values)
{
    if (values.contains(QStringLiteral("quotaBytes")) && (!values.value(QStringLiteral("quotaBytes")).isDouble() || values.value(QStringLiteral("quotaBytes")).toDouble() < 0 || values.value(QStringLiteral("quotaBytes")).toDouble() > 64.0 * 1024 * 1024 * 1024)) return false;
    if (values.contains(QStringLiteral("retentionDays")) && (!values.value(QStringLiteral("retentionDays")).isDouble() || values.value(QStringLiteral("retentionDays")).toInt(-1) < 0 || values.value(QStringLiteral("retentionDays")).toInt() > 3650)) return false;
    if (values.contains(QStringLiteral("thumbnailQuotaBytes")) && (!values.value(QStringLiteral("thumbnailQuotaBytes")).isDouble() || values.value(QStringLiteral("thumbnailQuotaBytes")).toDouble() < 0 || values.value(QStringLiteral("thumbnailQuotaBytes")).toDouble() > 1024.0 * 1024 * 1024)) return false;
    auto merged = settings();
    for (auto it = values.begin(); it != values.end(); ++it) merged.insert(it.key(),it.value());
    QSqlQuery q(m_db); q.prepare(QStringLiteral("INSERT OR REPLACE INTO meta(key,value) VALUES('settings',?)"));
    q.addBindValue(QString::fromUtf8(QJsonDocument(merged).toJson(QJsonDocument::Compact))); return q.exec();
}

qint64 Store::ownedBytes()
{
    QSqlQuery q(m_db); q.prepare(QStringLiteral("SELECT COALESCE(SUM(size),0) FROM items WHERE owned=1"));
    return q.exec() && q.next() ? q.value(0).toLongLong() : 0;
}

AddResult Store::add(QJsonArray inputs, QJsonObject options)
{
    AddResult result;
    if (!m_db.isOpen()) { result.error = failure(QStringLiteral("internal"),QStringLiteral("Storage is not open")); return result; }
    if (!ownedStorageSafe()) { result.error = failure(QStringLiteral("internal"),QStringLiteral("Shelf storage permissions or directory boundaries changed")); return result; }
    if ((options.contains(QStringLiteral("shelf")) && (!options.value(QStringLiteral("shelf")).isString() || !safeString(options.value(QStringLiteral("shelf")).toString()))) ||
        (options.contains(QStringLiteral("reveal")) && !options.value(QStringLiteral("reveal")).isBool())) {
        result.error = failure(QStringLiteral("unsupported_input"),QStringLiteral("Invalid Shelf options")); return result;
    }
    const QString shelf = options.value(QStringLiteral("shelf")).toString(activeShelf());
    if (!hasShelf(shelf)) { result.error = failure(QStringLiteral("unsupported_input"),QStringLiteral("The target shelf does not exist")); return result; }
    QJsonArray expanded;
    for (const auto &value : inputs) {
        const auto c = value.toObject();
        if (c.value(QStringLiteral("type")).toString().endsWith(QStringLiteral("[]"))) {
            if (!c.value(QStringLiteral("paths")).isArray() || c.value(QStringLiteral("paths")).toArray().isEmpty()) { expanded.append(c); continue; }
            for (const auto &path : c.value(QStringLiteral("paths")).toArray()) {
                auto one = c; one.insert(QStringLiteral("type"),c.value(QStringLiteral("type")).toString().chopped(2));
                one.remove(QStringLiteral("paths")); one.insert(QStringLiteral("path"),path); expanded.append(one);
                if (expanded.size() > MaxInputs) break;
            }
        } else expanded.append(value);
        if (expanded.size() > MaxInputs) break;
    }
    if (expanded.size() > MaxInputs) { result.error = failure(QStringLiteral("unsupported_input"),QStringLiteral("At most 1000 items can be added in one request")); return result; }
    if (expanded.isEmpty()) { result.error = failure(QStringLiteral("unsupported_input")); return result; }
    // An array of short path values can fit the request line but overflow the
    // response after adding stable IDs and structured skip reasons. Reject
    // before persistence so callers never see a transport failure after commit.
    qint64 responseBytes = 4096;
    for (const auto &value : expanded) {
        const auto c = value.toObject();
        QJsonObject added{{QStringLiteral("item"),QString(35,QLatin1Char('x'))}};
        QJsonObject skipped{{QStringLiteral("reason"),QString(32,QLatin1Char('x'))}};
        if (c.contains(QStringLiteral("path"))) {
            added.insert(QStringLiteral("path"),c.value(QStringLiteral("path")));
            skipped.insert(QStringLiteral("path"),c.value(QStringLiteral("path")));
        } else skipped.insert(QStringLiteral("type"),c.value(QStringLiteral("type")));
        responseBytes += qMax(QJsonDocument(added).toJson(QJsonDocument::Compact).size(),
                              QJsonDocument(skipped).toJson(QJsonDocument::Compact).size()) + 1;
        if (responseBytes > 900 * 1024) {
            result.error = failure(QStringLiteral("too_large"),{},ArcadeLink::MaxLineBytes);
            return result;
        }
    }
    const qint64 quota = settings().value(QStringLiteral("quotaBytes")).toVariant().toLongLong();
    qint64 used = ownedBytes();
    QSqlQuery order(m_db); order.prepare(QStringLiteral("SELECT COALESCE(MAX(position),-1)+1 FROM items WHERE shelf=?")); order.addBindValue(shelf);
    if (!order.exec() || !order.next() || !m_db.transaction()) { result.error = failure(QStringLiteral("busy")); return result; }
    int position = order.value(0).toInt();
    QJsonArray added, skipped;
    QStringList staged;
    bool exceeded = false, duplicate = false;
    qint64 rejectedLimit = quota;
    const auto skip = [&](const QJsonObject &c,const QString &why) {
        QJsonObject o{{QStringLiteral("reason"),why}};
        if (c.contains(QStringLiteral("path"))) o.insert(QStringLiteral("path"),c.value(QStringLiteral("path")));
        else o.insert(QStringLiteral("type"),c.value(QStringLiteral("type")));
        skipped.append(o);
    };
    const auto abort = [&] {
        m_db.rollback(); for (const auto &path : staged) removeOwnedPath(path);
        result.addedIds.clear(); result.error = failure(QStringLiteral("internal"),QStringLiteral("Could not persist items"));
    };
    const QSet<QString> fileKinds{QStringLiteral("image"),QStringLiteral("video"),QStringLiteral("audio"),QStringLiteral("pdf"),QStringLiteral("document"),QStringLiteral("spreadsheet"),QStringLiteral("presentation"),QStringLiteral("archive"),QStringLiteral("text"),QStringLiteral("code"),QStringLiteral("font"),QStringLiteral("model"),QStringLiteral("any")};
    for (const auto &value : expanded) {
        const auto c = value.toObject();
        QString type = c.value(QStringLiteral("type")).toString();
        const bool file = type.startsWith(QStringLiteral("file/")) && fileKinds.contains(type.mid(5));
        const bool folder = type == QStringLiteral("folder/reference");
        const bool textContent = type == QStringLiteral("text/plain") || type == QStringLiteral("text/rich") || type == QStringLiteral("text/url");
        if (!value.isObject() || (!file && !folder && !textContent)) { skip(c,QStringLiteral("unsupported_type")); continue; }
        QString path, text, html, key, name;
        bool owned = false;
        qint64 size = 0;
        QByteArray bytes;
        if (file || folder || c.contains(QStringLiteral("path"))) {
            if (!c.value(QStringLiteral("path")).isString()) { skip(c,QStringLiteral("invalid_path")); continue; }
            path = c.value(QStringLiteral("path")).toString();
            if (!safeString(path) || !QDir::isAbsolutePath(path) || path.toUtf8().size() > 32768) { skip(c,QStringLiteral("invalid_path")); continue; }
            const auto info = inspect(path);
            if (!info.exists) { skip(c,QStringLiteral("missing")); continue; }
            // Qt's native URI conversion classifies a directory alias as a
            // folder. Verify that claim using metadata only, while retaining
            // the alias path itself for identity, persistence, and transfer.
            const bool directoryTarget = info.directory || (info.symlink && QFileInfo(path).isDir());
            if ((folder && !directoryTarget) || (file && directoryTarget) || (file && !info.regular && !info.symlink) || (file && type != QStringLiteral("file/any") && type.mid(5) != ArcadeLink::fileKindForPath(path))) {
                skip(c,QStringLiteral("type_mismatch")); continue;
            }
            if (file && type == QStringLiteral("file/any")) type = QStringLiteral("file/") + ArcadeLink::fileKindForPath(path);
            name = QFileInfo(path).fileName();
            if (name.isEmpty()) name = path;
            size = info.size;
            owned = isHandoff(c,path);
            // Some peers tag durable outputs with owner as provenance. The owner
            // field alone never permits copying: only actual handoff paths do.
            if ((c.contains(QStringLiteral("owner")) && beneath(ArcadeLink::Locations::discover().handoff,path) && !owned) ||
                (textContent && (!owned || !info.regular)) || (owned && !info.regular)) {
                skip(c,QStringLiteral("invalid_handoff")); continue;
            }
            if (!owned) key = QStringLiteral("path:") + referenceKey(path);
            if (textContent) {
                if (size > MaxText || size > quota - used) { exceeded = true; if (size > MaxText) rejectedLimit = qMin(rejectedLimit,MaxText); skip(c,QStringLiteral("quota")); continue; }
                QFile source(path);
                if (!openRegular(source)) { skip(c,QStringLiteral("unreadable")); continue; }
                bytes = source.read(MaxText + 1);
                if (source.error() != QFileDevice::NoError || bytes.size() != size) { skip(c,QStringLiteral("copy_failed")); continue; }
                if (bytes.size() > MaxText || !source.atEnd()) { exceeded = true; rejectedLimit = qMin(rejectedLimit,MaxText); skip(c,QStringLiteral("quota")); continue; }
                QStringDecoder decoder(QStringDecoder::Utf8); text = decoder(bytes);
                if (decoder.hasError() || !safeString(text)) { skip(c,QStringLiteral("invalid_text")); continue; }
                if (type == QStringLiteral("text/rich")) html = c.value(QStringLiteral("html")).toString();
                size = bytes.size() + html.toUtf8().size();
            }
        } else {
            if (!c.value(QStringLiteral("text")).isString()) { skip(c,QStringLiteral("invalid_text")); continue; }
            text = c.value(QStringLiteral("text")).toString();
            html = c.value(QStringLiteral("html")).toString();
            if (!safeString(text) || html.contains(QChar::Null) || (type == QStringLiteral("text/rich") && !safeString(html))) { skip(c,QStringLiteral("invalid_text")); continue; }
            bytes = text.toUtf8(); size = bytes.size() + html.toUtf8().size(); owned = true;
            if (size > InlineLimit) { exceeded = true; rejectedLimit = qMin(rejectedLimit,InlineLimit); skip(c,QStringLiteral("inline_limit")); continue; }
            key = QStringLiteral("text:") + QString::fromLatin1(QCryptographicHash::hash(type.toUtf8() + '\0' + bytes + '\0' + html.toUtf8(),QCryptographicHash::Sha256).toHex());
        }
        if (textContent && type == QStringLiteral("text/url")) {
            const QUrl url(text,QUrl::StrictMode);
            if (!url.isValid() || (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("mailto") && url.scheme() != QStringLiteral("ftp"))) { skip(c,QStringLiteral("invalid_url")); continue; }
        }
        if (textContent) {
            if (key.isEmpty()) key = QStringLiteral("text:") + QString::fromLatin1(QCryptographicHash::hash(type.toUtf8() + '\0' + text.toUtf8() + '\0' + html.toUtf8(),QCryptographicHash::Sha256).toHex());
            name = c.value(QStringLiteral("name")).toString(text.section(QLatin1Char('\n'),0,0).simplified().left(80));
        }
        if (!key.isEmpty()) {
            QSqlQuery exists(m_db); exists.prepare(QStringLiteral("SELECT id FROM items WHERE shelf=? AND refkey=?")); exists.addBindValue(shelf); exists.addBindValue(key);
            if (!exists.exec()) { abort(); return result; }
            if (exists.next()) { duplicate = true; skip(c,QStringLiteral("duplicate")); continue; }
        }
        if (owned && size > quota - used) { exceeded = true; skip(c,QStringLiteral("quota")); continue; }
        const QString id = uid(QStringLiteral("it_"));
        if (owned && !path.isEmpty()) {
            const QString directory = m_root + QStringLiteral("/objects/") + id;
            if (!privateOwnedDirectory(directory)) { skip(c,QStringLiteral("storage_failure")); continue; }
            // Keep the source extension: a caller's display-name hint is never
            // allowed to change the verified file kind of staged bytes.
            const QString dest = directory + QLatin1Char('/') + safeName(QFileInfo(path).fileName(),QStringLiteral("content"));
            QSaveFile out(dest);
            QFile source(path);
            bool copied = out.open(QIODevice::WriteOnly) && out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            if (copied && textContent) copied = out.write(bytes) == bytes.size();
            else if (copied) {
                copied = openRegular(source);
                qint64 total = 0;
                while (copied && !source.atEnd()) {
                    const auto chunk = source.read(64 * 1024);
                    if (chunk.isEmpty() && source.error() != QFileDevice::NoError) { copied = false; break; }
                    total += chunk.size();
                    copied = total <= quota - used && out.write(chunk) == chunk.size();
                }
                copied = copied && total == size;
            }
            if (!copied || !out.commit()) { QDir(directory).removeRecursively(); skip(c,QStringLiteral("copy_failed")); continue; }
            path = dest; staged.append(dest);
            // A large text lives once on disk; retain only a small display preview in SQLite.
            if (textContent && bytes.size() > InlineLimit) text = text.left(2048);
        }
        QSqlQuery insert(m_db);
        insert.prepare(QStringLiteral("INSERT INTO items(id,shelf,type,name,path,text,html,size,owned,position,created,refkey) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"));
        insert.addBindValue(id); insert.addBindValue(shelf); insert.addBindValue(type); insert.addBindValue(name);
        insert.addBindValue(path.isNull() ? QStringLiteral("") : path);
        insert.addBindValue(text.isNull() ? QStringLiteral("") : text);
        insert.addBindValue(html.isNull() ? QStringLiteral("") : html); insert.addBindValue(size);
        insert.addBindValue(owned); insert.addBindValue(position++); insert.addBindValue(QDateTime::currentMSecsSinceEpoch());
        insert.addBindValue(key.isEmpty() ? QVariant(QMetaType::fromType<QString>()) : QVariant(key));
        if (!insert.exec()) { abort(); return result; }
        if (owned) used += size;
        QJsonObject add{{QStringLiteral("item"),id}};
        if (c.contains(QStringLiteral("path"))) add.insert(QStringLiteral("path"),c.value(QStringLiteral("path")));
        added.append(add); result.addedIds.append(id);
    }
    if (added.isEmpty() && !duplicate) {
        m_db.rollback(); for (const auto &path : staged) removeOwnedPath(path);
        result.error = failure(exceeded ? QStringLiteral("too_large") : QStringLiteral("unsupported_input"),{},exceeded ? rejectedLimit : -1);
        result.result = QJsonObject{{QStringLiteral("data"),QJsonObject{{QStringLiteral("skipped"),skipped}}}};
        return result;
    }
    if (!m_db.commit()) { abort(); return result; }
    QSqlQuery shelfName(m_db); shelfName.prepare(QStringLiteral("SELECT name FROM shelves WHERE id=?")); shelfName.addBindValue(shelf); shelfName.exec(); shelfName.next();
    const QString name = shelfName.value(0).toString();
    QString message = added.isEmpty() ? QStringLiteral("Already in %1").arg(name) : QStringLiteral("Added %1 %2 to %3").arg(added.size()).arg(added.size() == 1 ? QStringLiteral("item") : QStringLiteral("items"),name);
    int duplicates = 0;
    for (const auto &s : skipped) if (s.toObject().value(QStringLiteral("reason")) == QStringLiteral("duplicate")) ++duplicates;
    if (duplicates && !added.isEmpty()) message += QStringLiteral(" · %1 already there").arg(duplicates);
    if (skipped.size() > duplicates) message += QStringLiteral(" · %1 skipped").arg(skipped.size() - duplicates);
    result.result = QJsonObject{{QStringLiteral("message"),message},{QStringLiteral("data"),QJsonObject{
        {QStringLiteral("shelf"),QJsonObject{{QStringLiteral("id"),shelf},{QStringLiteral("name"),name}}},
        {QStringLiteral("added"),added},{QStringLiteral("skipped"),skipped}}}};
    return result;
}

bool Store::removeOwnedPath(const QString &path)
{
    if (!ownedStorageSafe()) return false;
    const QString objects = m_root + QStringLiteral("/objects");
    if (!beneath(objects,path)) return false;
    const QString relative = QDir(objects).relativeFilePath(path);
    const QString id = relative.section(QLatin1Char('/'),0,0);
    if (!QRegularExpression(QStringLiteral("^it_[0-9a-f]{32}$")).match(id).hasMatch()) return false;
    const QString directory = objects + QLatin1Char('/') + id;
    if (QFileInfo(objects).isSymLink() || QFileInfo(directory).isSymLink()) return false;
    return QDir(directory).removeRecursively();
}

bool Store::remove(QStringList ids)
{
    if (!ownedStorageSafe()) return false;
    if (!m_db.transaction()) return false;
    QStringList paths;
    for (const auto &id : QSet<QString>(ids.begin(),ids.end())) {
        const auto i = item(id);
        if (i.isEmpty()) continue;
        if (i.value(QStringLiteral("owned")).toBool() && !i.value(QStringLiteral("path")).toString().isEmpty()) paths.append(i.value(QStringLiteral("path")).toString());
        QSqlQuery q(m_db); q.prepare(QStringLiteral("DELETE FROM items WHERE id=?")); q.addBindValue(id);
        if (!q.exec()) { m_db.rollback(); return false; }
    }
    if (!m_db.commit()) return false;
    for (const auto &path : paths) removeOwnedPath(path);
    return true;
}

QString Store::createShelf(QString name, bool temporary)
{
    name = name.trimmed(); if (!validName(name)) return {};
    const QString id = uid(QStringLiteral("sh_"));
    QSqlQuery q(m_db); q.prepare(QStringLiteral("INSERT INTO shelves(id,name,temporary,pinned,position,created) VALUES(?,?,?,0,(SELECT COALESCE(MAX(position),-1)+1 FROM shelves),?)"));
    q.addBindValue(id); q.addBindValue(name); q.addBindValue(temporary); q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    return q.exec() ? id : QString{};
}

bool Store::renameShelf(QString id, QString name)
{
    name = name.trimmed(); if (!validName(name) || !hasShelf(id)) return false;
    QSqlQuery q(m_db); q.prepare(QStringLiteral("UPDATE shelves SET name=? WHERE id=?")); q.addBindValue(name); q.addBindValue(id); return q.exec();
}

bool Store::deleteShelf(QString id)
{
    if (!ownedStorageSafe() || id == QStringLiteral("quick") || !hasShelf(id) || !m_db.transaction()) return false;
    QStringList paths;
    for (const auto &v : items(id)) { const auto i = v.toObject(); if (i.value(QStringLiteral("owned")).toBool()) paths.append(i.value(QStringLiteral("path")).toString()); }
    if (activeShelf() == id && !setActiveShelf(QStringLiteral("quick"))) { m_db.rollback(); return false; }
    QSqlQuery q(m_db); q.prepare(QStringLiteral("DELETE FROM shelves WHERE id=?")); q.addBindValue(id);
    if (!q.exec() || !m_db.commit()) { m_db.rollback(); return false; }
    for (const auto &path : paths) if (!path.isEmpty()) removeOwnedPath(path);
    return true;
}

bool Store::pinShelf(QString id, bool pin)
{
    if (!hasShelf(id)) return false;
    QSqlQuery q(m_db); q.prepare(QStringLiteral("UPDATE shelves SET pinned=? WHERE id=?")); q.addBindValue(pin); q.addBindValue(id); return q.exec();
}

bool Store::reorder(QString shelfId, QStringList ids)
{
    const auto all = items(shelfId); QSet<QString> expected, provided(ids.begin(),ids.end());
    for (const auto &i : all) expected.insert(i.toObject().value(QStringLiteral("id")).toString());
    if (!hasShelf(shelfId) || expected != provided || provided.size() != ids.size() || !m_db.transaction()) return false;
    int position = 0;
    for (const auto &id : ids) {
        QSqlQuery q(m_db); q.prepare(QStringLiteral("UPDATE items SET position=? WHERE id=? AND shelf=?")); q.addBindValue(position++); q.addBindValue(id); q.addBindValue(shelfId);
        if (!q.exec()) { m_db.rollback(); return false; }
    }
    return m_db.commit();
}

bool Store::reorderShelves(QStringList ids)
{
    QSet<QString> expected, provided(ids.begin(),ids.end());
    for (const auto &s : shelves()) expected.insert(s.toObject().value(QStringLiteral("id")).toString());
    if (expected != provided || provided.size() != ids.size() || !m_db.transaction()) return false;
    int position = 0;
    for (const auto &id : ids) {
        QSqlQuery q(m_db); q.prepare(QStringLiteral("UPDATE shelves SET position=? WHERE id=?")); q.addBindValue(position++); q.addBindValue(id);
        if (!q.exec()) { m_db.rollback(); return false; }
    }
    return m_db.commit();
}

QString Store::textHandoff(const QString &id, const QByteArray &bytes, ArcadeLink::Error *error)
{
    const QString key = id + QLatin1Char('_') + QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
    return writeHandoff(key,QStringLiteral("text.txt"),bytes,{},bytes.size(),error);
}

QString Store::fileHandoff(const QString &id, const QString &path, qint64 size, ArcadeLink::Error *error)
{
    const auto info = inspect(path);
    if (!ownedObjectSafe(path)) {
        if (error) *error = failure(QStringLiteral("unsupported_input"));
        return {};
    }
    const auto fingerprint = path.toUtf8() + '\0' + QByteArray::number(size) + '\0' + QByteArray::number(QFileInfo(path).lastModified().toMSecsSinceEpoch());
    const QString key = id + QLatin1Char('_') + QString::fromLatin1(QCryptographicHash::hash(fingerprint,QCryptographicHash::Sha256).toHex());
    return writeHandoff(key,safeName(QFileInfo(path).fileName(),QStringLiteral("content")),{},path,info.size,error);
}

QString Store::writeHandoff(const QString &key, const QString &name, const QByteArray &bytes,
                           const QString &source, qint64 size, ArcadeLink::Error *error)
{
    const auto handoff = ArcadeLink::Locations::discover().handoff;
    if (!privateOwnedDirectory(handoff)) {
        if (error) *error = failure(QStringLiteral("internal"),QStringLiteral("Could not create a private handoff"));
        return {};
    }
    const QString cached = m_handoffs.value(key);
    if (!cached.isEmpty() && beneath(handoff,cached) && inspect(cached).regular && !QFileInfo(cached).isSymLink()) {
        const QString actualRoot = QFileInfo(handoff).canonicalFilePath();
        if (!privateDirectoryBoundary(QFileInfo(cached).absolutePath()) || actualRoot.isEmpty() || !beneath(actualRoot,QFileInfo(cached).canonicalFilePath())) {
            if (error) *error = failure(QStringLiteral("internal"),QStringLiteral("An owned handoff directory boundary changed"));
            return {};
        }
        QFile marker(QFileInfo(cached).absolutePath() + QStringLiteral("/.arcade-shelf"));
        const QByteArray expected = QByteArray("arcade.shelf\n") + key.toUtf8() + '\n' + name.toUtf8() + '\n';
        if (!openRegular(marker,QIODevice::ReadWrite) || marker.read(4096) != expected ||
            !marker.setFileTime(QDateTime::currentDateTimeUtc(),QFileDevice::FileModificationTime)) {
            if (error) *error = failure(QStringLiteral("internal"),QStringLiteral("Could not renew the owned handoff lifetime"));
            return {};
        }
        return cached;
    }
    const qint64 quota = settings().value(QStringLiteral("quotaBytes")).toVariant().toLongLong();
    if (size > quota - m_handoffBytes) {
        if (error) *error = failure(QStringLiteral("too_large"),{},quota);
        return {};
    }
    if (!privateOwnedDirectory(handoff)) {
        if (error) *error = failure(QStringLiteral("internal"),QStringLiteral("Could not create a private handoff"));
        return {};
    }
    const QString directory = handoff + QLatin1Char('/') + uid();
    if (!privateOwnedDirectory(directory)) { if (error) *error = failure(QStringLiteral("internal")); return {}; }
    QSaveFile marker(directory + QStringLiteral("/.arcade-shelf"));
    const QByteArray ownership = QByteArray("arcade.shelf\n") + key.toUtf8() + '\n' + name.toUtf8() + '\n';
    const QString output = directory + QLatin1Char('/') + name;
    QSaveFile f(output);
    bool copied = f.open(QIODevice::WriteOnly) && f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (copied && source.isEmpty()) copied = f.write(bytes) == bytes.size();
    else if (copied) {
        QFile input(source); copied = openRegular(input); qint64 total = 0;
        while (copied && !input.atEnd()) {
            const auto chunk = input.read(64 * 1024);
            if (chunk.isEmpty() && input.error() != QFileDevice::NoError) { copied = false; break; }
            total += chunk.size(); copied = total <= size && f.write(chunk) == chunk.size();
        }
        copied = copied && total == size;
    }
    if (!copied || !f.commit() || !marker.open(QIODevice::WriteOnly) || !marker.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) || marker.write(ownership) != ownership.size() || !marker.commit()) {
        QDir(directory).removeRecursively(); if (error) *error = failure(QStringLiteral("internal")); return {};
    }
    m_handoffs.insert(key,output); m_handoffBytes += size;
    return output;
}

QJsonArray Store::exportItems(QStringList ids, QStringList types, bool multiple, ArcadeLink::Error *error)
{
    if (error) *error = {};
    QJsonArray outputs; QStringList files;
    qint64 fileBytes = 0;
    bool ownedFiles = false;
    qint64 inlineBytes = 0;
    if (ids.size() > MaxInputs || (!multiple && ids.size() > 1)) { if (error) *error = failure(QStringLiteral("unsupported_input")); return outputs; }
    QSet<QString> seen;
    for (const auto &id : ids) {
        if (seen.contains(id)) continue;
        seen.insert(id);
        const auto i = item(id); if (i.isEmpty() || !i.value(QStringLiteral("available")).toBool()) continue;
        const QString storedType = i.value(QStringLiteral("type")).toString();
        // shelf.pick v1 promises text/plain, not text/rich. Preserve its plain
        // fallback; the HTML stays private metadata available to the owner UI.
        const QString type = storedType == QStringLiteral("text/rich") ? QStringLiteral("text/plain") : storedType;
        bool accepts = types.isEmpty();
        for (const auto &pattern : types) accepts |= ArcadeLink::typeMatches(pattern,type);
        if (!accepts) continue;
        QString path = i.value(QStringLiteral("path")).toString();
        if (type.startsWith(QStringLiteral("file/"))) {
            if (i.value(QStringLiteral("owned")).toBool()) {
                path = fileHandoff(id,path,i.value(QStringLiteral("size")).toInteger(),error);
                if (path.isEmpty()) return {};
                ownedFiles = true;
            }
            files.append(path);
            const auto size = qMax<qint64>(0,i.value(QStringLiteral("size")).toVariant().toLongLong());
            fileBytes += qMin(size,std::numeric_limits<qint64>::max() - fileBytes);
            continue;
        }
        if (type == QStringLiteral("folder/reference")) { outputs.append(QJsonObject{{QStringLiteral("type"),type},{QStringLiteral("path"),path}}); continue; }
        QString text = i.value(QStringLiteral("text")).toString();
        if (!path.isEmpty()) {
            if (!ownedObjectSafe(path)) {
                if (error) *error = failure(QStringLiteral("unsupported_input"));
                return {};
            }
            QFile f(path); if (!openRegular(f) || f.size() > MaxText) continue;
            const auto content = f.read(MaxText + 1);
            if (f.error() != QFileDevice::NoError || content.size() > MaxText || !f.atEnd()) continue;
            QStringDecoder decoder(QStringDecoder::Utf8); text = decoder(content); if (decoder.hasError()) continue;
        }
        const QString html = type == QStringLiteral("text/rich") ? i.value(QStringLiteral("html")).toString() : QString();
        QJsonObject c{{QStringLiteral("type"),type}};
        const auto bytes = text.toUtf8();
        QJsonObject inlineContent{{QStringLiteral("type"),type},{QStringLiteral("text"),text}};
        if (type == QStringLiteral("text/rich")) inlineContent.insert(QStringLiteral("html"),html);
        const auto wireBytes = QJsonDocument(inlineContent).toJson(QJsonDocument::Compact).size();
        if (bytes.size() + html.toUtf8().size() <= InlineLimit && inlineBytes + wireBytes <= 512 * 1024) {
            c.insert(QStringLiteral("text"),text); if (type == QStringLiteral("text/rich")) c.insert(QStringLiteral("html"),html);
            inlineBytes += wireBytes;
        } else {
            const QString output = textHandoff(id,bytes,error);
            if (output.isEmpty()) return {};
            c.insert(QStringLiteral("path"),output); c.insert(QStringLiteral("owner"),QStringLiteral("arcade.shelf")); c.insert(QStringLiteral("size"),bytes.size());
            if (type == QStringLiteral("text/rich")) c.insert(QStringLiteral("html"),html);
        }
        outputs.append(c);
    }
    if (!files.isEmpty()) {
        if (files.size() == 1) {
            auto c = ArcadeLink::fileContent(files.front());
            // Link's helper follows directory symlinks; preserve our lstat classification.
            c.insert(QStringLiteral("type"),QStringLiteral("file/") + ArcadeLink::fileKindForPath(files.front()));
            c.insert(QStringLiteral("size"),fileBytes);
            if (ownedFiles) c.insert(QStringLiteral("owner"),QStringLiteral("arcade.shelf"));
            outputs.prepend(c);
        } else {
            QString kind = ArcadeLink::fileKindForPath(files.front());
            QJsonArray paths;
            for (const auto &p : files) { paths.append(p); if (ArcadeLink::fileKindForPath(p) != kind) kind = QStringLiteral("any"); }
            QJsonObject c{{QStringLiteral("type"),QStringLiteral("file/") + kind + QStringLiteral("[]")},{QStringLiteral("paths"),paths},{QStringLiteral("size"),fileBytes}};
            if (ownedFiles) c.insert(QStringLiteral("owner"),QStringLiteral("arcade.shelf"));
            outputs.prepend(c);
        }
    }
    if (QJsonDocument(QJsonObject{{QStringLiteral("outputs"),outputs}}).toJson(QJsonDocument::Compact).size() > 900 * 1024) {
        if (error) *error = failure(QStringLiteral("too_large"),{},ArcadeLink::MaxLineBytes);
        return {};
    }
    if (outputs.isEmpty() && error) *error = failure(QStringLiteral("unsupported_input"));
    return outputs;
}

bool Store::cleanup()
{
    // One startup listing, never idle polling. Remove only Shelf's generated object IDs.
    const QString objects = m_root + QStringLiteral("/objects");
    if (!ownedStorageSafe()) return false;
    QSet<QString> referenced;
    QSqlQuery q(m_db); q.prepare(QStringLiteral("SELECT path FROM items WHERE owned=1 AND path<>''"));
    if (!q.exec()) return false;
    while (q.next()) referenced.insert(QDir(objects).relativeFilePath(q.value(0).toString()).section(QLatin1Char('/'),0,0));
    for (const auto &d : QDir(objects).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!d.isSymLink() && QRegularExpression(QStringLiteral("^it_[0-9a-f]{32}$")).match(d.fileName()).hasMatch() && !referenced.contains(d.fileName())) QDir(d.absoluteFilePath()).removeRecursively();
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int retention = settings().value(QStringLiteral("retentionDays")).toInt(30);
    if (retention > 0) {
        QStringList expired;
        QSqlQuery old(m_db); old.prepare(QStringLiteral("SELECT i.id FROM items i JOIN shelves s ON s.id=i.shelf WHERE s.temporary=1 AND s.pinned=0 AND i.created<?"));
        old.addBindValue(now - static_cast<qint64>(retention) * 24 * 60 * 60 * 1000);
        if (!old.exec()) return false;
        while (old.next()) expired.append(old.value(0).toString());
        if (!expired.isEmpty() && !remove(expired)) return false;
    }
    const auto handoff = ArcadeLink::Locations::discover().handoff;
    m_handoffs.clear(); m_handoffBytes = 0;
    if (QFileInfo(handoff).isSymLink()) return false;
    for (const auto &d : QDir(handoff).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (d.isSymLink() || !QRegularExpression(QStringLiteral("^[0-9a-f]{32}$")).match(d.fileName()).hasMatch()) continue;
        QFile marker(d.absoluteFilePath() + QStringLiteral("/.arcade-shelf"));
        if (!openRegular(marker)) continue;
        const auto ownership = marker.read(4096);
        const auto fields = ownership.split('\n');
        if (fields.value(0) != QByteArray("arcade.shelf")) continue;
        const auto id = QString::fromUtf8(fields.value(1));
        const QString name = QString::fromUtf8(fields.value(2));
        if (!QRegularExpression(QStringLiteral("^it_[0-9a-f]{32}_[0-9a-f]{64}$")).match(id).hasMatch() ||
            !safeString(name) || safeName(name,{}) != name) continue;
        if (QFileInfo(marker).lastModified().toMSecsSinceEpoch() < now - 24LL * 60 * 60 * 1000) {
            QDir(d.absoluteFilePath()).removeRecursively();
            continue;
        }
        const QString output = d.absoluteFilePath() + QLatin1Char('/') + name;
        const auto info = inspect(output);
        if (!info.regular || info.symlink) continue;
        m_handoffBytes += qMin(info.size,std::numeric_limits<qint64>::max() - m_handoffBytes);
        m_handoffs.insert(id,output);
    }
    return true;
}

} // namespace Shelf
