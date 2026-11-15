#include "DesktopPrivate.h"

#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QUrl>
#include <memory>

#ifdef ARCADE_SHELF_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#endif

namespace {

bool availablePath(const QString &path)
{
    if (path.isEmpty() || path.contains(QChar::Null) || !QDir::isAbsolutePath(path)) return false;
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}

} // namespace

namespace Shelf {

using desktop::runProgram;

bool Desktop::open(const QString &pathOrUrl)
{
    if (pathOrUrl.contains(QChar::Null)) {
        setError(QStringLiteral("Invalid path"));
        return false;
    }
    QUrl url;
    if (QDir::isAbsolutePath(pathOrUrl)) {
        if (!availablePath(pathOrUrl) || !QFileInfo::exists(pathOrUrl)) {
            setError(QStringLiteral("The original item is unavailable"));
            return false;
        }
        url = QUrl::fromLocalFile(pathOrUrl);
    } else {
        url = QUrl(pathOrUrl, QUrl::StrictMode);
        if (!url.isValid() || url.host().isEmpty() || (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http"))) {
            setError(QStringLiteral("Only local files and HTTP or HTTPS URLs can be opened"));
            return false;
        }
    }
    const bool opened = QDesktopServices::openUrl(url);
    if (!opened) setError(QStringLiteral("Couldn't open this item. The desktop has no app for it."));
    return opened;
}

void Desktop::reveal(const QString &path)
{
    if (!availablePath(path)) {
        setError(QStringLiteral("The original item is unavailable"));
        return;
    }
#ifdef Q_OS_WIN
    const QString explorer = QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("explorer.exe"));
    runProgram(this, explorer, {QStringLiteral("/select,"), QDir::toNativeSeparators(path)},
               [this, path](bool ok, const QByteArray &) {
                   if (!ok && !QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())))
                       setError(QStringLiteral("Couldn't show this in its folder. File Explorer didn't open it."));
               });
#elif defined(Q_OS_MACOS)
    runProgram(this, QStringLiteral("/usr/bin/open"), {QStringLiteral("-R"), path},
               [this](bool ok, const QByteArray &) { if (!ok) setError(QStringLiteral("Couldn't show this in its folder. Finder didn't open it.")); });
#else
#ifdef ARCADE_SHELF_HAVE_DBUS
    QDBusMessage request = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"),
                                                          QStringLiteral("/org/freedesktop/FileManager1"), QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"));
    request << QStringList{QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)} << QString();
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(request, 2000), this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, path] {
        if (watcher->reply().type() == QDBusMessage::ErrorMessage && !QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())))
            setError(QStringLiteral("Couldn't show this in its folder. The file manager didn't open it."));
        watcher->deleteLater();
    });
#else
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())))
        setError(QStringLiteral("Couldn't show this in its folder. The file manager didn't open it."));
#endif
#endif
}

QMimeData *Desktop::mimeDataForContents(const QJsonArray &contents)
{
    auto *mime = new QMimeData;
    QList<QUrl> urls;
    QStringList text;
    QStringList html;
    int count = 0;
    for (const auto &entry : contents) {
        const auto content = entry.toObject();
        const QString type = content.value("type").toString();
        if (type.startsWith(QStringLiteral("file/")) || type == QStringLiteral("folder/reference")) {
            QStringList paths;
            if (content.value("path").isString() && ++count <= 1000) paths << content.value("path").toString();
            for (const auto &value : content.value("paths").toArray()) {
                if (++count > 1000) break;
                if (value.isString()) paths << value.toString();
            }
            for (const QString &path : paths)
                if (availablePath(path)) {
                    const QUrl url = QUrl::fromLocalFile(path);
                    if (!urls.contains(url)) urls << url;
                }
        } else if (type == QStringLiteral("text/plain") || type == QStringLiteral("text/rich") || type == QStringLiteral("text/url")) {
            if (++count > 1000) break;
            QString plain = content.value("text").toString();
            if (plain.isEmpty() && availablePath(content.value("path").toString())) {
                const QString path = content.value("path").toString();
                QFile file(path);
                // Large staged snippets remain transferable as files. Only
                // bounded text is read synchronously when a drag actually starts.
                if (QFileInfo(path).size() <= 256 * 1024 && file.open(QIODevice::ReadOnly)) plain = QString::fromUtf8(file.read(256 * 1024));
                else urls << QUrl::fromLocalFile(path);
            }
            if (plain.toUtf8().size() <= 256 * 1024 && !plain.isEmpty()) text << plain;
            if (type == QStringLiteral("text/url")) {
                const QUrl url(plain, QUrl::StrictMode);
                if (url.isValid() && (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http"))) urls << url;
            }
            if (type == QStringLiteral("text/rich")) {
                const QString rich = content.value("html").toString();
                if (!rich.isEmpty() && rich.toUtf8().size() <= 256 * 1024) html << rich;
            }
        } else if (type == QStringLiteral("structured/shelf-reorder")) {
            const auto data = content.value("data").toObject();
            const auto ids = data.value("ids").toArray();
            const QString profile = data.value("profile").toString();
            bool valid = !profile.isEmpty() && profile.size() <= 128 && !ids.isEmpty() && ids.size() <= 1000;
            for (const auto &id : ids)
                valid = valid && id.isString() && !id.toString().isEmpty() && id.toString().size() <= 128;
            if (valid) mime->setData(QStringLiteral("application/x-arcade-shelf-items"),
                                     QJsonDocument(QJsonObject{{"profile", profile}, {"ids", ids}}).toJson(QJsonDocument::Compact));
        }
    }
    if (!urls.isEmpty()) mime->setUrls(urls);
    if (!text.isEmpty()) mime->setText(text.join(QLatin1Char('\n')));
    else if (!urls.isEmpty()) {
        QStringList paths;
        for (const QUrl &url : urls) paths << (url.isLocalFile() ? url.toLocalFile() : url.toString());
        mime->setText(paths.join(QLatin1Char('\n')));
    }
    if (!html.isEmpty()) mime->setHtml(html.join(QStringLiteral("<br>")));
    return mime;
}

void Desktop::startDrag(QWindow *window, const QJsonArray &contents)
{
    if (!window || contents.isEmpty()) return;
    std::unique_ptr<QMimeData> mime(mimeDataForContents(contents));
    if (mime->formats().isEmpty()) {
        setError(QStringLiteral("No available items to drag"));
        return;
    }
    auto *drag = new QDrag(window);
    drag->setMimeData(mime.release());
    auto action = std::make_shared<int>(int(Qt::IgnoreAction));
    QObject::connect(drag, &QDrag::actionChanged, this,
                     [action](Qt::DropAction changed) { *action = int(changed); });
    QObject::connect(drag, &QObject::destroyed, this,
                     [this, action] { emit dragFinished(*action); });
    // Copy-only is intentional: dragging out must never move or remove an
    // original file. Native Wayland/X11/OLE/Cocoa handle the actual transfer.
    *action = int(drag->exec(Qt::CopyAction, Qt::CopyAction));
    // Qt's X11 platform can return before the receiver fetches MIME data.
    // QDrag must remain platform-owned on the heap; deleting it here would
    // erase the selection before another process finishes reading it.
}

} // namespace Shelf
