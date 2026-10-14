#include "Store.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QUrl>
#include <cmath>
#include <limits>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Shelf {
namespace {
const QByteArray PortableMagic("ARCADE-SHELF-PORTABLE\n");
constexpr quint64 MaxManifestBytes = 32ULL * 1024 * 1024;
constexpr quint64 MaxPortableBytes = 64ULL * 1024 * 1024 * 1024 + MaxManifestBytes + 16;
constexpr int MaxPortableItems = 10000;
constexpr int MaxPortableShelves = 256;
constexpr qint64 MaxInlineBytes = 256 * 1024;
constexpr qint64 MaxOwnedBytes = 64LL * 1024 * 1024 * 1024;
constexpr qint64 MaxJsonInteger = 9007199254740991LL;

bool writeAll(QIODevice &device, const char *data, qint64 length)
{
    qint64 written = 0;
    while (written < length) {
        const qint64 count = device.write(data + written, length - written);
        if (count <= 0) return false;
        written += count;
    }
    return true;
}

bool readAll(QIODevice &device, char *data, qint64 length)
{
    qint64 read = 0;
    while (read < length) {
        const qint64 count = device.read(data + read, length - read);
        if (count <= 0) return false;
        read += count;
    }
    return true;
}

bool openRegular(QFile &file, QIODevice::OpenMode mode = QIODevice::ReadOnly)
{
#ifdef Q_OS_UNIX
    const int access = mode.testFlag(QIODevice::WriteOnly) ? O_RDWR : O_RDONLY;
    const int fd = ::open(QFile::encodeName(file.fileName()).constData(), access | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
    if (fd < 0) return false;
    struct stat st {};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || !file.open(fd, mode, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        return false;
    }
    return true;
#else
    const QFileInfo info(file.fileName());
    return !info.isSymLink() && info.isFile() && file.open(mode);
#endif
}

bool transfer(QIODevice &source, QIODevice *destination, qint64 length, QCryptographicHash *hash = nullptr)
{
    QByteArray buffer(128 * 1024, Qt::Uninitialized);
    qint64 remaining = length;
    while (remaining > 0) {
        const qint64 want = qMin<qint64>(remaining, buffer.size());
        const qint64 count = source.read(buffer.data(), want);
        if (count <= 0) return false;
        if (hash) hash->addData(QByteArrayView(buffer.constData(), count));
        if (destination && !writeAll(*destination, buffer.constData(), count)) return false;
        remaining -= count;
    }
    return true;
}

bool hashFile(const QString &path, qint64 *size, QByteArray *digest)
{
    QFile input(path);
    if (!openRegular(input)) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 total = 0;
    while (!input.atEnd()) {
        const QByteArray block = input.read(128 * 1024);
        if (block.isEmpty() && input.error() != QFileDevice::NoError) return false;
        if (block.size() > MaxOwnedBytes - total) return false;
        total += block.size();
        hash.addData(block);
    }
    if (input.error() != QFileDevice::NoError) return false;
    *size = total;
    *digest = hash.result().toHex();
    return true;
}

QString uniqueId(const QString &prefix)
{
    return prefix + QUuid::createUuid().toString(QUuid::Id128);
}

QString safeLeaf(QString name)
{
    if (name.isEmpty()) name = QStringLiteral("content");
    name = QFileInfo(name).fileName();
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]")), QStringLiteral("_"));
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..")) return QStringLiteral("content");
    return name.left(180);
}

QString referenceKey(QString path)
{
#ifdef Q_OS_WIN
    path = QDir::fromNativeSeparators(path).toCaseFolded();
#endif
    return path;
}

bool safeText(const QString &text)
{
    return !text.isEmpty() && !text.contains(QChar::Null) && QString::fromUtf8(text.toUtf8()) == text;
}

bool validItemType(const QString &type)
{
    static const QSet<QString> fileKinds{
        QStringLiteral("image"), QStringLiteral("video"), QStringLiteral("audio"), QStringLiteral("pdf"),
        QStringLiteral("document"), QStringLiteral("spreadsheet"), QStringLiteral("presentation"),
        QStringLiteral("archive"), QStringLiteral("text"), QStringLiteral("code"), QStringLiteral("font"),
        QStringLiteral("model"), QStringLiteral("any")
    };
    return (type.startsWith(QStringLiteral("file/")) && fileKinds.contains(type.mid(5))) ||
        type == QStringLiteral("folder/reference") || type == QStringLiteral("text/plain") ||
        type == QStringLiteral("text/rich") || type == QStringLiteral("text/url");
}

bool validId(const QString &id, const QString &prefix)
{
    return QRegularExpression(QStringLiteral("^") + prefix + QStringLiteral("[0-9a-f]{32}$")).match(id).hasMatch();
}

bool numericInteger(const QJsonValue &value, qint64 minimum, qint64 maximum, qint64 *number = nullptr)
{
    if (!value.isDouble()) return false;
    const double raw = value.toDouble();
    if (!std::isfinite(raw) || raw < static_cast<double>(minimum) || raw > static_cast<double>(qMin(maximum, MaxJsonInteger)) || raw != std::floor(raw)) return false;
    if (number) *number = static_cast<qint64>(raw);
    return true;
}

QJsonObject portablePreferences(const QJsonObject &settings)
{
    QJsonObject result;
    for (const QString &key : {QStringLiteral("theme"), QStringLiteral("alwaysOnTop"), QStringLiteral("reducedMotion"),
                               QStringLiteral("quotaBytes"), QStringLiteral("retentionDays"), QStringLiteral("thumbnailQuotaBytes"),
                               QStringLiteral("linkEnabled"), QStringLiteral("shortcut"), QStringLiteral("peers")}) {
        if (settings.contains(key)) result.insert(key, settings.value(key));
    }
    return result;
}

bool validatePortablePreferences(const QJsonObject &preferences, QString *error)
{
    static const QSet<QString> allowed{
        QStringLiteral("theme"), QStringLiteral("alwaysOnTop"), QStringLiteral("reducedMotion"),
        QStringLiteral("quotaBytes"), QStringLiteral("retentionDays"), QStringLiteral("thumbnailQuotaBytes"),
        QStringLiteral("linkEnabled"), QStringLiteral("shortcut"), QStringLiteral("peers")
    };
    for (auto it = preferences.begin(); it != preferences.end(); ++it) {
        if (!allowed.contains(it.key())) { *error = QStringLiteral("The backup contains an unsupported preference."); return false; }
    }
    if (preferences.contains(QStringLiteral("theme")) &&
        (!preferences.value(QStringLiteral("theme")).isString() ||
         !QStringList{QStringLiteral("system"), QStringLiteral("dark"), QStringLiteral("light")}.contains(preferences.value(QStringLiteral("theme")).toString()))) {
        *error = QStringLiteral("The backup contains an invalid theme preference."); return false;
    }
    for (const QString &key : {QStringLiteral("alwaysOnTop"), QStringLiteral("reducedMotion"), QStringLiteral("linkEnabled")}) {
        if (preferences.contains(key) && !preferences.value(key).isBool()) { *error = QStringLiteral("The backup contains an invalid boolean preference."); return false; }
    }
    qint64 ignored = 0;
    if (preferences.contains(QStringLiteral("quotaBytes")) && !numericInteger(preferences.value(QStringLiteral("quotaBytes")), 0, MaxOwnedBytes, &ignored)) {
        *error = QStringLiteral("The backup contains an invalid storage quota."); return false;
    }
    if (preferences.contains(QStringLiteral("retentionDays")) && !numericInteger(preferences.value(QStringLiteral("retentionDays")), 0, 3650, &ignored)) {
        *error = QStringLiteral("The backup contains an invalid retention period."); return false;
    }
    if (preferences.contains(QStringLiteral("thumbnailQuotaBytes")) && !numericInteger(preferences.value(QStringLiteral("thumbnailQuotaBytes")), 0, 1024LL * 1024 * 1024, &ignored)) {
        *error = QStringLiteral("The backup contains an invalid thumbnail quota."); return false;
    }
    if (preferences.contains(QStringLiteral("shortcut"))) {
        const auto shortcut = preferences.value(QStringLiteral("shortcut"));
        if (!shortcut.isString() || shortcut.toString().size() > 100 ||
            (!shortcut.toString().isEmpty() && ArcadeLink::normalizeAccelerator(shortcut.toString()).isEmpty())) {
            *error = QStringLiteral("The backup contains an invalid shortcut preference."); return false;
        }
    }
    if (preferences.contains(QStringLiteral("peers"))) {
        const auto peers = preferences.value(QStringLiteral("peers"));
        if (!peers.isObject()) { *error = QStringLiteral("The backup contains invalid connected-app preferences."); return false; }
        const QJsonObject peerValues = peers.toObject();
        for (auto it = peerValues.begin(); it != peerValues.end(); ++it) {
            if (!QRegularExpression(QStringLiteral("^arcade\\.[a-z][a-z0-9.-]{0,63}$")).match(it.key()).hasMatch() || !it.value().isBool()) {
                *error = QStringLiteral("The backup contains invalid connected-app preferences."); return false;
            }
        }
    }
    return true;
}
}

qint64 Store::portableOwnedBytes()
{
    return ownedBytes();
}

bool Store::exportPortable(const QString &path, bool includeOwned, qint64 *includedBytes, int *omittedItems, QString *error)
{
    if (error) error->clear();
    if (includedBytes) *includedBytes = 0;
    if (omittedItems) *omittedItems = 0;
    const auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (!m_db.isOpen() || !ownedStorageSafe()) return fail(QStringLiteral("Shelf storage is unavailable or unsafe."));
    if (QFileInfo(path).isSymLink() || QFileInfo(path).isDir()) return fail(QStringLiteral("Choose a new backup file path."));
    const QFileInfo destination(path);
    const QString parent = QFileInfo(destination.absolutePath()).canonicalFilePath();
    const QString storage = QFileInfo(m_root).canonicalFilePath();
    if (parent.isEmpty() || parent == storage || parent.startsWith(storage + QLatin1Char('/')))
        return fail(QStringLiteral("Save the backup outside Shelf's active storage directory."));

    QJsonArray shelves, items, payloads;
    QSqlQuery shelfQuery(m_db);
    if (!shelfQuery.exec(QStringLiteral("SELECT id,name,temporary,pinned,position,created FROM shelves ORDER BY position,created,id")))
        return fail(QStringLiteral("Could not read Shelf collections."));
    while (shelfQuery.next()) {
        shelves.append(QJsonObject{{QStringLiteral("id"), shelfQuery.value(0).toString()},
                                   {QStringLiteral("name"), shelfQuery.value(1).toString()},
                                   {QStringLiteral("temporary"), shelfQuery.value(2).toBool()},
                                   {QStringLiteral("pinned"), shelfQuery.value(3).toBool()},
                                   {QStringLiteral("position"), shelfQuery.value(4).toInt()},
                                   {QStringLiteral("created"), shelfQuery.value(5).toLongLong()}});
    }
    QSqlQuery itemQuery(m_db);
    if (!itemQuery.exec(QStringLiteral("SELECT id,shelf,type,name,path,text,html,size,owned,position,created FROM items ORDER BY shelf,position,created,id")))
        return fail(QStringLiteral("Could not read Shelf collection items."));
    int omitted = 0;
    qint64 contentBytes = 0;
    QStringList sourcePaths;
    QList<qint64> sourceSizes;
    QList<QByteArray> sourceHashes;
    while (itemQuery.next()) {
        const bool owned = itemQuery.value(8).toBool();
        const QString sourcePath = itemQuery.value(4).toString();
        if (owned && !includeOwned) { ++omitted; continue; }
        QJsonObject item{{QStringLiteral("shelf"), itemQuery.value(1).toString()},
                         {QStringLiteral("type"), itemQuery.value(2).toString()},
                         {QStringLiteral("name"), itemQuery.value(3).toString()},
                         {QStringLiteral("size"), itemQuery.value(7).toLongLong()},
                         {QStringLiteral("owned"), owned},
                         {QStringLiteral("position"), itemQuery.value(9).toInt()},
                         {QStringLiteral("created"), itemQuery.value(10).toLongLong()}};
        if (!owned) item.insert(QStringLiteral("path"), sourcePath);
        else if (sourcePath.isEmpty()) {
            item.insert(QStringLiteral("text"), itemQuery.value(5).toString());
            item.insert(QStringLiteral("html"), itemQuery.value(6).toString());
            contentBytes += itemQuery.value(7).toLongLong();
        } else {
            if (!ownedObjectSafe(sourcePath)) return fail(QStringLiteral("Shelf could not safely read owned content for this backup."));
            qint64 size = 0;
            QByteArray digest;
            if (!hashFile(sourcePath, &size, &digest) || size != QFileInfo(sourcePath).size() || size != itemQuery.value(7).toLongLong())
                return fail(QStringLiteral("Shelf could not verify an owned file while preparing the backup."));
            if (size > MaxOwnedBytes - contentBytes) return fail(QStringLiteral("Owned content exceeds the portable backup limit."));
            contentBytes += size;
            item.insert(QStringLiteral("payload"), payloads.size());
            payloads.append(QJsonObject{{QStringLiteral("name"), QFileInfo(sourcePath).fileName()},
                                        {QStringLiteral("size"), size},
                                        {QStringLiteral("sha256"), QString::fromLatin1(digest)}});
            sourcePaths.append(sourcePath);
            sourceSizes.append(size);
            sourceHashes.append(digest);
            item.insert(QStringLiteral("text"), itemQuery.value(5).toString());
            item.insert(QStringLiteral("html"), itemQuery.value(6).toString());
        }
        items.append(item);
    }
    if (contentBytes > MaxOwnedBytes) return fail(QStringLiteral("Owned content exceeds the portable backup limit."));

    const QJsonObject prefs = portablePreferences(settings());
    QString preferenceError;
    if (!validatePortablePreferences(prefs, &preferenceError)) return fail(preferenceError);
    const QJsonObject manifest{{QStringLiteral("format"), QStringLiteral("arcade-shelf-portable")},
                               {QStringLiteral("schema"), 1},
                               {QStringLiteral("app"), QStringLiteral("arcade.shelf")},
                               {QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                               {QStringLiteral("activeShelf"), activeShelf()},
                               {QStringLiteral("preferences"), prefs},
                               {QStringLiteral("collections"), shelves},
                               {QStringLiteral("items"), items},
                               {QStringLiteral("payloads"), payloads},
                               {QStringLiteral("includedOwnedBytes"), contentBytes},
                               {QStringLiteral("omittedOwnedItems"), omitted}};
    const QByteArray json = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    if (static_cast<quint64>(json.size()) > MaxManifestBytes) return fail(QStringLiteral("Shelf metadata is too large for a portable backup."));

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || !output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return fail(QStringLiteral("Could not create a private portable backup file."));
    if (!writeAll(output, PortableMagic.constData(), PortableMagic.size())) { output.cancelWriting(); return fail(QStringLiteral("Could not write the portable backup header.")); }
    QDataStream header(&output);
    header.setByteOrder(QDataStream::BigEndian);
    header << quint64(json.size());
    if (header.status() != QDataStream::Ok || !writeAll(output, json.constData(), json.size())) {
        output.cancelWriting(); return fail(QStringLiteral("Could not write portable backup metadata."));
    }
    for (qsizetype i = 0; i < sourcePaths.size(); ++i) {
        QFile source(sourcePaths[i]);
        if (!ownedObjectSafe(sourcePaths[i]) || !openRegular(source)) { output.cancelWriting(); return fail(QStringLiteral("Owned content changed while writing the backup.")); }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!transfer(source, &output, sourceSizes[i], &hash) || !source.atEnd() || hash.result().toHex() != sourceHashes[i]) {
            output.cancelWriting(); return fail(QStringLiteral("Owned content changed while writing the backup."));
        }
    }
    if (!output.commit()) return fail(QStringLiteral("Could not finish the portable backup file."));
    if (includedBytes) *includedBytes = includeOwned ? contentBytes : 0;
    if (omittedItems) *omittedItems = omitted;
    return true;
}

bool Store::importPortable(const QString &path, int *importedItems, QString *error)
{
    if (error) error->clear();
    if (importedItems) *importedItems = 0;
    const auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (!m_db.isOpen() || !ownedStorageSafe()) return fail(QStringLiteral("Shelf storage is unavailable or unsafe."));
    QFile archive(path);
    if (!openRegular(archive) || archive.size() < PortableMagic.size() + 8 || static_cast<quint64>(archive.size()) > MaxPortableBytes)
        return fail(QStringLiteral("This is not a readable Arcade Shelf backup."));
    QByteArray magic(PortableMagic.size(), Qt::Uninitialized);
    if (!readAll(archive, magic.data(), magic.size()) || magic != PortableMagic)
        return fail(QStringLiteral("This file is not an Arcade Shelf portable backup."));
    QDataStream header(&archive);
    header.setByteOrder(QDataStream::BigEndian);
    quint64 manifestLength = 0;
    header >> manifestLength;
    if (header.status() != QDataStream::Ok || manifestLength == 0 || manifestLength > MaxManifestBytes || manifestLength > static_cast<quint64>(archive.bytesAvailable()))
        return fail(QStringLiteral("The backup metadata length is invalid."));
    QByteArray manifestBytes(static_cast<qsizetype>(manifestLength), Qt::Uninitialized);
    if (!readAll(archive, manifestBytes.data(), manifestBytes.size())) return fail(QStringLiteral("The backup metadata is truncated."));
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(manifestBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return fail(QStringLiteral("The backup metadata is invalid."));
    const QJsonObject manifest = document.object();
    if (manifest.value(QStringLiteral("format")).toString() != QStringLiteral("arcade-shelf-portable") ||
        manifest.value(QStringLiteral("schema")).toInt() != 1 || manifest.value(QStringLiteral("app")).toString() != QStringLiteral("arcade.shelf"))
        return fail(QStringLiteral("This backup was created by an unsupported app or format version."));

    const QJsonArray shelves = manifest.value(QStringLiteral("collections")).toArray();
    const QJsonArray items = manifest.value(QStringLiteral("items")).toArray();
    const QJsonArray payloads = manifest.value(QStringLiteral("payloads")).toArray();
    if (shelves.isEmpty() || shelves.size() > MaxPortableShelves || items.size() > MaxPortableItems || payloads.size() > MaxPortableItems)
        return fail(QStringLiteral("The backup has too many collections or items."));
    const QJsonObject importedPreferences = manifest.value(QStringLiteral("preferences")).toObject();
    QString validationError;
    if (!validatePortablePreferences(importedPreferences, &validationError)) return fail(validationError);
    QJsonObject resultingSettings = settings();
    for (auto it = importedPreferences.begin(); it != importedPreferences.end(); ++it) resultingSettings.insert(it.key(), it.value());
    qint64 importedQuota = 0;
    if (!numericInteger(resultingSettings.value(QStringLiteral("quotaBytes")), 0, MaxOwnedBytes, &importedQuota))
        return fail(QStringLiteral("The resulting Shelf storage quota is invalid."));

    QSet<QString> shelfIds;
    bool hasQuick = false;
    for (const auto &value : shelves) {
        if (!value.isObject()) return fail(QStringLiteral("The backup contains invalid collection metadata."));
        const auto shelf = value.toObject();
        const QString id = shelf.value(QStringLiteral("id")).toString();
        const QString name = shelf.value(QStringLiteral("name")).toString();
        qint64 ignored = 0;
        if ((id != QStringLiteral("quick") && !validId(id, QStringLiteral("sh_"))) || shelfIds.contains(id) ||
            !safeText(name) || name.trimmed() != name || name.size() > 100 ||
            !shelf.value(QStringLiteral("temporary")).isBool() || !shelf.value(QStringLiteral("pinned")).isBool() ||
            !numericInteger(shelf.value(QStringLiteral("position")), 0, MaxPortableShelves, &ignored) ||
            !numericInteger(shelf.value(QStringLiteral("created")), 0, std::numeric_limits<qint64>::max(), &ignored))
            return fail(QStringLiteral("The backup contains invalid collection metadata."));
        shelfIds.insert(id);
        hasQuick = hasQuick || id == QStringLiteral("quick");
    }
    const QString active = manifest.value(QStringLiteral("activeShelf")).toString();
    if (!hasQuick || !shelfIds.contains(active)) return fail(QStringLiteral("The backup does not contain a valid active collection."));

    QVector<qint64> payloadSizes;
    QVector<QByteArray> payloadHashes;
    QVector<QString> payloadNames;
    quint64 payloadBytes = 0;
    for (const auto &value : payloads) {
        if (!value.isObject()) return fail(QStringLiteral("The backup contains an invalid owned-content record."));
        const auto payload = value.toObject();
        qint64 size = 0;
        const QString name = payload.value(QStringLiteral("name")).toString();
        const QByteArray hash = payload.value(QStringLiteral("sha256")).toString().toLatin1();
        if (!numericInteger(payload.value(QStringLiteral("size")), 0, MaxOwnedBytes, &size) ||
            !safeText(name) || safeLeaf(name) != name || !QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(QString::fromLatin1(hash)).hasMatch() ||
            static_cast<quint64>(size) > static_cast<quint64>(MaxOwnedBytes) - payloadBytes)
            return fail(QStringLiteral("The backup contains invalid owned-content metadata."));
        payloadBytes += static_cast<quint64>(size);
        payloadSizes.append(size);
        payloadHashes.append(hash);
        payloadNames.append(name);
    }

    QVector<int> payloadReferences(payloads.size(), 0);
    qint64 totalOwnedBytes = 0;
    for (const auto &value : items) {
        if (!value.isObject()) return fail(QStringLiteral("The backup contains invalid collection items."));
        const auto item = value.toObject();
        const QString shelfId = item.value(QStringLiteral("shelf")).toString();
        const QString type = item.value(QStringLiteral("type")).toString();
        const QString name = item.value(QStringLiteral("name")).toString();
        qint64 itemSize = 0, position = 0, created = 0;
        if (!shelfIds.contains(shelfId) || !validItemType(type) || !safeText(name) || name.size() > 180 ||
            !numericInteger(item.value(QStringLiteral("size")), 0, std::numeric_limits<qint64>::max(), &itemSize) ||
            !numericInteger(item.value(QStringLiteral("position")), 0, MaxPortableItems, &position) ||
            !numericInteger(item.value(QStringLiteral("created")), 0, std::numeric_limits<qint64>::max(), &created) ||
            !item.value(QStringLiteral("owned")).isBool())
            return fail(QStringLiteral("The backup contains invalid collection items."));
        const bool owned = item.value(QStringLiteral("owned")).toBool();
        if (owned) {
            if (itemSize > MaxOwnedBytes) return fail(QStringLiteral("The backup exceeds Shelf's maximum owned-content size."));
            if (item.contains(QStringLiteral("payload"))) {
                qint64 index = -1;
                if (!numericInteger(item.value(QStringLiteral("payload")), 0, payloads.size() - 1, &index) ||
                    ++payloadReferences[static_cast<int>(index)] != 1 || payloadSizes[static_cast<int>(index)] != itemSize)
                    return fail(QStringLiteral("The backup contains invalid owned-content references."));
            } else {
                const QString text = item.value(QStringLiteral("text")).toString();
                const QString html = item.value(QStringLiteral("html")).toString();
                if (!type.startsWith(QStringLiteral("text/")) || !safeText(text) || html.contains(QChar::Null) ||
                    (type == QStringLiteral("text/rich") && !safeText(html)) ||
                    text.toUtf8().size() + html.toUtf8().size() > MaxInlineBytes ||
                    text.toUtf8().size() + html.toUtf8().size() != itemSize)
                    return fail(QStringLiteral("The backup contains invalid inline owned content."));
                if (type == QStringLiteral("text/url")) {
                    const QUrl url(text, QUrl::StrictMode);
                    if (!url.isValid() || !QStringList{QStringLiteral("https"), QStringLiteral("http"), QStringLiteral("mailto"), QStringLiteral("ftp")}.contains(url.scheme()))
                        return fail(QStringLiteral("The backup contains an invalid URL item."));
                }
            }
            if (itemSize > MaxOwnedBytes - totalOwnedBytes) return fail(QStringLiteral("The backup exceeds Shelf's maximum owned-content size."));
            totalOwnedBytes += itemSize;
        } else {
            const QString refPath = item.value(QStringLiteral("path")).toString();
            if ((!type.startsWith(QStringLiteral("file/")) && type != QStringLiteral("folder/reference")) ||
                !safeText(refPath) || !QDir::isAbsolutePath(refPath) || refPath.toUtf8().size() > 32768)
                return fail(QStringLiteral("The backup contains an invalid file reference."));
        }
    }
    for (int references : payloadReferences) if (references != 1) return fail(QStringLiteral("The backup has an unreferenced owned-content record."));
    if (totalOwnedBytes > importedQuota) return fail(QStringLiteral("The backup's owned content exceeds its saved storage quota."));
    qint64 claimedOwnedBytes = 0;
    if (!numericInteger(manifest.value(QStringLiteral("includedOwnedBytes")), 0, MaxOwnedBytes, &claimedOwnedBytes) || claimedOwnedBytes != totalOwnedBytes)
        return fail(QStringLiteral("The backup's owned-content size does not match its metadata."));
    if (payloadBytes > static_cast<quint64>(archive.size() - archive.pos()) || payloadBytes != static_cast<quint64>(archive.size() - archive.pos()))
        return fail(QStringLiteral("The backup payload size does not match its metadata."));

    // Validate and stage every content record before altering the live database.
    QVector<QString> newIds(items.size());
    QVector<QString> stagedPaths(items.size());
    QStringList stagedFiles;
    for (qsizetype i = 0; i < items.size(); ++i) newIds[i] = uniqueId(QStringLiteral("it_"));
    for (int payloadIndex = 0; payloadIndex < payloads.size(); ++payloadIndex) {
        int itemIndex = -1;
        for (int i = 0; i < items.size(); ++i) {
            if (items[i].toObject().value(QStringLiteral("payload")).toInt(-1) == payloadIndex) { itemIndex = i; break; }
        }
        if (itemIndex < 0) return fail(QStringLiteral("The backup has an unreferenced owned-content record."));
        const QString directory = m_root + QStringLiteral("/objects/") + newIds[itemIndex];
        if (!privateOwnedDirectory(directory)) {
            for (const auto &created : stagedFiles) removeOwnedPath(created);
            return fail(QStringLiteral("Could not create private storage for imported content."));
        }
        const QString destination = directory + QLatin1Char('/') + safeLeaf(payloadNames[payloadIndex]);
        QSaveFile staged(destination);
        if (!staged.open(QIODevice::WriteOnly) || !staged.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
            QDir(directory).removeRecursively();
            for (const auto &created : stagedFiles) removeOwnedPath(created);
            return fail(QStringLiteral("Could not stage imported owned content."));
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!transfer(archive, &staged, payloadSizes[payloadIndex], &hash) || hash.result().toHex() != payloadHashes[payloadIndex] || !staged.commit()) {
            staged.cancelWriting();
            QDir(directory).removeRecursively();
            for (const auto &created : stagedFiles) removeOwnedPath(created);
            return fail(QStringLiteral("Owned content failed its size or SHA-256 check."));
        }
        stagedPaths[itemIndex] = destination;
        stagedFiles.append(destination);
    }
    if (!archive.atEnd()) {
        for (const auto &created : stagedFiles) removeOwnedPath(created);
        return fail(QStringLiteral("The backup has unexpected trailing data."));
    }

    QStringList oldOwnedPaths;
    QSqlQuery oldItems(m_db);
    if (!oldItems.exec(QStringLiteral("SELECT path FROM items WHERE owned=1 AND path<>''"))) {
        for (const auto &created : stagedFiles) removeOwnedPath(created);
        return fail(QStringLiteral("Could not prepare Shelf's import backup."));
    }
    while (oldItems.next()) oldOwnedPaths.append(oldItems.value(0).toString());

    const QString backups = m_root + QStringLiteral("/backups");
    if (!privateOwnedDirectory(backups)) {
        for (const auto &created : stagedFiles) removeOwnedPath(created);
        return fail(QStringLiteral("Could not create Shelf's private import backup directory."));
    }
    const QString backupPath = backups + QStringLiteral("/before-import-") + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")) +
        QLatin1Char('-') + QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".sqlite3");
    QString escapedBackup = backupPath;
    escapedBackup.replace(QLatin1Char('\''), QStringLiteral("''"));
    QSqlQuery backup(m_db);
    if (!backup.exec(QStringLiteral("VACUUM INTO '") + escapedBackup + QLatin1Char('\'')) || !QFileInfo(backupPath).isFile()) {
        for (const auto &created : stagedFiles) removeOwnedPath(created);
        return fail(QStringLiteral("Could not back up current Shelf settings before import."));
    }
    if (!QFile::setPermissions(backupPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        QFile::remove(backupPath);
        for (const auto &created : stagedFiles) removeOwnedPath(created);
        return fail(QStringLiteral("Could not protect Shelf's import backup."));
    }

    if (!m_db.transaction()) {
        for (const auto &created : stagedFiles) removeOwnedPath(created);
        return fail(QStringLiteral("Could not start the Shelf import transaction."));
    }
    const auto rollback = [&] {
        m_db.rollback();
        for (const auto &created : stagedFiles) removeOwnedPath(created);
    };
    QSqlQuery clear(m_db);
    if (!clear.exec(QStringLiteral("DELETE FROM shelves"))) { rollback(); return fail(QStringLiteral("Could not replace Shelf collections.")); }
    for (const auto &value : shelves) {
        const auto shelf = value.toObject();
        QSqlQuery insert(m_db);
        insert.prepare(QStringLiteral("INSERT INTO shelves(id,name,temporary,pinned,position,created) VALUES(?,?,?,?,?,?)"));
        insert.addBindValue(shelf.value(QStringLiteral("id")).toString());
        insert.addBindValue(shelf.value(QStringLiteral("name")).toString());
        insert.addBindValue(shelf.value(QStringLiteral("temporary")).toBool());
        insert.addBindValue(shelf.value(QStringLiteral("pinned")).toBool());
        insert.addBindValue(shelf.value(QStringLiteral("position")).toInt());
        insert.addBindValue(shelf.value(QStringLiteral("created")).toInteger());
        if (!insert.exec()) { rollback(); return fail(QStringLiteral("Could not restore Shelf collection metadata.")); }
    }
    for (qsizetype i = 0; i < items.size(); ++i) {
        const auto item = items[i].toObject();
        const bool owned = item.value(QStringLiteral("owned")).toBool();
        const QString type = item.value(QStringLiteral("type")).toString();
        QString text = item.value(QStringLiteral("text")).toString(QStringLiteral(""));
        QString html = item.value(QStringLiteral("html")).toString(QStringLiteral(""));
        QString path = owned ? stagedPaths[i] : item.value(QStringLiteral("path")).toString();
        if (path.isNull()) path = QStringLiteral("");
        QString refkey;
        if (!owned) refkey = QStringLiteral("path:") + referenceKey(path);
        else if (path.isEmpty()) {
            const QByteArray source = type.toUtf8() + '\0' + text.toUtf8() + '\0' + html.toUtf8();
            refkey = QStringLiteral("text:") + QString::fromLatin1(QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex());
        }
        QSqlQuery insert(m_db);
        insert.prepare(QStringLiteral("INSERT INTO items(id,shelf,type,name,path,text,html,size,owned,position,created,refkey) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"));
        insert.addBindValue(newIds[i]);
        insert.addBindValue(item.value(QStringLiteral("shelf")).toString());
        insert.addBindValue(type);
        insert.addBindValue(item.value(QStringLiteral("name")).toString());
        insert.addBindValue(path);
        insert.addBindValue(text);
        insert.addBindValue(html);
        insert.addBindValue(item.value(QStringLiteral("size")).toInteger());
        insert.addBindValue(owned);
        insert.addBindValue(item.value(QStringLiteral("position")).toInt());
        insert.addBindValue(item.value(QStringLiteral("created")).toInteger());
        insert.addBindValue(refkey.isEmpty() ? QVariant(QMetaType::fromType<QString>()) : QVariant(refkey));
        if (!insert.exec()) { rollback(); return fail(QStringLiteral("Could not restore Shelf collection items.")); }
    }
    if (!setActiveShelf(active) || !saveSettings(importedPreferences) || !m_db.commit()) {
        rollback(); return fail(QStringLiteral("Could not commit the Shelf backup import."));
    }
    for (const auto &oldPath : std::as_const(oldOwnedPaths)) removeOwnedPath(oldPath);
    if (importedItems) *importedItems = items.size();
    return true;
}
}
