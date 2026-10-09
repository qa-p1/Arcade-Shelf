// Arcade Link for Qt. See ArcadeLink.h and SPEC.md.
#include "ArcadeLink.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ArcadeLink {

QList<int> supportedProtocols() { return {1}; }

QStringList Ids::apps() { return {Box, Lens, Look, Wheel, Clipboard}; }

QString appName(const QString &id)
{
    if (id == Ids::Box) return QStringLiteral("Arcade Box");
    if (id == Ids::Lens) return QStringLiteral("Arcade Lens");
    if (id == Ids::Look) return QStringLiteral("Arcade Look");
    if (id == Ids::Wheel) return QStringLiteral("Arcade Wheel");
    if (id == Ids::Clipboard) return QStringLiteral("Arcade Clipboard");
    if (id == Ids::Tools) return QStringLiteral("Arcade Tools");
    return id;
}

QString appPitch(const QString &id)
{
    if (id == Ids::Box) return QStringLiteral("Convert, compress and transform files with one click.");
    if (id == Ids::Lens) return QStringLiteral("Recognize text, codes and colors on screen, and pick a region.");
    if (id == Ids::Look) return QStringLiteral("Preview any file instantly.");
    if (id == Ids::Wheel) return QStringLiteral("Put any action on a one-gesture radial launcher.");
    if (id == Ids::Clipboard) return QStringLiteral("Send content to all your devices, end-to-end encrypted.");
    if (id == Ids::Tools) return QStringLiteral("Install and update the Arcade apps.");
    return {};
}

QString releasesUrl(const QString &id)
{
    if (id == Ids::Box) return QStringLiteral("https://github.com/qa-p1/Arcade-box/releases");
    if (id == Ids::Lens) return QStringLiteral("https://github.com/qa-p1/Arcade-lens/releases");
    if (id == Ids::Look) return QStringLiteral("https://github.com/qa-p1/arcade-look/releases");
    if (id == Ids::Wheel) return QStringLiteral("https://github.com/qa-p1/Arcade-wheel/releases");
    if (id == Ids::Clipboard) return QStringLiteral("https://github.com/qa-p1/Arcade-clipboard/releases");
    return QStringLiteral("https://github.com/qa-p1/Arcade-tools/releases");
}

// ---- Locations --------------------------------------------------------------

static QString envDir(const char *name)
{
    const QString v = qEnvironmentVariable(name);
    return !v.isEmpty() && QDir::isAbsolutePath(v) ? v : QString();
}

static QString homeDir() { return QDir::homePath(); }

Locations Locations::under(const QString &root)
{
    const QDir d(root);
    return {d.filePath(QStringLiteral("apps")), d.filePath(QStringLiteral("run")), d.filePath(QStringLiteral("handoff"))};
}

Locations Locations::discover()
{
    const QString home = qEnvironmentVariable("ARCADE_HOME");
    if (!home.isEmpty()) return under(home);
    Locations l;
#if defined(Q_OS_WIN)
    QString local = envDir("LOCALAPPDATA");
    if (local.isEmpty()) local = QDir(homeDir()).filePath(QStringLiteral("AppData/Local"));
    const QDir base(QDir(local).filePath(QStringLiteral("Arcade")));
    l.registry = base.filePath(QStringLiteral("apps"));
    l.runtime = base.filePath(QStringLiteral("run"));
    l.handoff = base.filePath(QStringLiteral("handoff"));
#elif defined(Q_OS_MACOS)
    l.registry = QDir(homeDir()).filePath(QStringLiteral("Library/Application Support/Arcade/apps"));
    l.runtime = QDir(QDir::tempPath()).filePath(QStringLiteral("arcade"));
    l.handoff = QDir(homeDir()).filePath(QStringLiteral("Library/Caches/Arcade/handoff"));
#else
    QString data = envDir("XDG_DATA_HOME");
    if (data.isEmpty()) data = QDir(homeDir()).filePath(QStringLiteral(".local/share"));
    QString cache = envDir("XDG_CACHE_HOME");
    if (cache.isEmpty()) cache = QDir(homeDir()).filePath(QStringLiteral(".cache"));
    const QString runtime = envDir("XDG_RUNTIME_DIR");
    l.registry = QDir(data).filePath(QStringLiteral("arcade/apps"));
    l.runtime = runtime.isEmpty() ? QDir(cache).filePath(QStringLiteral("arcade/run")) : QDir(runtime).filePath(QStringLiteral("arcade"));
    l.handoff = QDir(cache).filePath(QStringLiteral("arcade/handoff"));
#endif
    return l;
}

QString Locations::manifestPath(const QString &appId) const { return QDir(registry).filePath(appId + QStringLiteral(".json")); }
QString Locations::endpointPath(const QString &appId) const { return QDir(runtime).filePath(appId + QStringLiteral(".endpoint")); }

QString currentPlatform()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

bool ensurePrivateDir(const QString &dir)
{
    if (!QDir().mkpath(dir)) return false;
#ifdef Q_OS_UNIX
    struct stat st {};
    const QByteArray path = QFile::encodeName(dir);
    if (lstat(path.constData(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != geteuid()) return false;
    if ((st.st_mode & 0777) != 0700 && chmod(path.constData(), 0700) != 0) return false;
#endif
    return true;
}

// ---- Errors ----------------------------------------------------------------

QJsonObject Error::toJson() const
{
    QJsonObject o{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}};
    if (!reason.isEmpty()) o.insert(QStringLiteral("reason"), reason);
    if (limit >= 0) o.insert(QStringLiteral("limit"), limit);
    return o;
}

static const QStringList &knownCodes()
{
    static const QStringList codes{
        QStringLiteral("not_installed"), QStringLiteral("not_running"), QStringLiteral("launch_failed"), QStringLiteral("timeout"),
        QStringLiteral("unsupported_input"), QStringLiteral("unavailable"), QStringLiteral("too_large"), QStringLiteral("denied"),
        QStringLiteral("busy"), QStringLiteral("cancelled"), QStringLiteral("version_mismatch"), QStringLiteral("internal")};
    return codes;
}

Error Error::fromJson(const QJsonObject &o)
{
    Error e;
    e.code = o.value(QStringLiteral("code")).toString();
    if (!knownCodes().contains(e.code)) e.code = QStringLiteral("internal");
    e.message = o.value(QStringLiteral("message")).toString();
    e.reason = o.value(QStringLiteral("reason")).toString();
    e.limit = o.contains(QStringLiteral("limit")) ? o.value(QStringLiteral("limit")).toInteger(-1) : -1;
    return e;
}

Error Error::make(const QString &code, const QString &message, const QString &reason, qint64 limit)
{
    Error e;
    e.code = code;
    e.message = message;
    e.reason = reason;
    e.limit = limit;
    return e;
}

QString Error::userMessage(const QString &app) const { return standardMessage(code, app, reason, limit); }

QString formatLimit(qint64 bytes)
{
    constexpr qint64 MiB = 1024 * 1024;
    if (bytes >= MiB && bytes % MiB == 0) return QStringLiteral("%1 MB").arg(bytes / MiB);
    if (bytes >= MiB) return QStringLiteral("%1 MB").arg(double(bytes) / double(MiB), 0, 'f', 1);
    if (bytes >= 1024) return QStringLiteral("%1 KB").arg(bytes / 1024);
    return QStringLiteral("%1 bytes").arg(bytes);
}

static QString sentence(QString r)
{
    r = r.trimmed();
    while (r.endsWith(QLatin1Char('.'))) r.chop(1);
    return r + QLatin1Char('.');
}

QString standardMessage(const QString &codeIn, const QString &app, const QString &reasonIn, qint64 limit)
{
    const QString code = knownCodes().contains(codeIn) ? codeIn : QStringLiteral("internal");
    const QString reason = reasonIn.trimmed();
    if (code == u"not_installed") return QStringLiteral("%1 isn't installed.").arg(app);
    if (code == u"not_running") return QStringLiteral("%1 isn't running.").arg(app);
    if (code == u"launch_failed") return QStringLiteral("%1 didn't start.").arg(app);
    if (code == u"timeout") return QStringLiteral("%1 didn't respond in time.").arg(app);
    if (code == u"unsupported_input") return QStringLiteral("%1 can't open this kind of content.").arg(app);
    if (code == u"unavailable")
        return reason.isEmpty() ? QStringLiteral("%1 can't do this right now.").arg(app)
                                : QStringLiteral("%1 can't do this yet: %2").arg(app, sentence(reason));
    if (code == u"too_large") {
        const QString l = limit >= 0 ? QStringLiteral(" (limit %1)").arg(formatLimit(limit)) : QString();
        return app == u"Arcade Clipboard" ? QStringLiteral("Too large to send to your devices%1.").arg(l)
                                          : QStringLiteral("Too large for %1%2.").arg(app, l);
    }
    if (code == u"denied") {
        if (reason == u"private_mode") return QStringLiteral("%1 is in Private mode.").arg(app);
        if (reason == u"secret") return QStringLiteral("Not sent: this looks like a password or key.");
        if (reason == u"user_cancelled") return QStringLiteral("Cancelled.");
        if (reason == u"disabled") return QStringLiteral("%1 has connections to other Arcade apps turned off.").arg(app);
        return QStringLiteral("%1 declined this request.").arg(app);
    }
    if (code == u"busy") return QStringLiteral("%1 is busy. Try again when its current job finishes.").arg(app);
    if (code == u"cancelled") return QStringLiteral("Cancelled.");
    if (code == u"version_mismatch") return QStringLiteral("%1 needs an update to work with this app.").arg(app);
    return reason.isEmpty() ? QStringLiteral("%1 ran into a problem.").arg(app)
                            : QStringLiteral("%1 ran into a problem: %2").arg(app, sentence(reason));
}

// ---- Content ---------------------------------------------------------------

static QString baseType(const QString &t) { return t.endsWith(QStringLiteral("[]")) ? t.chopped(2) : t; }

static QPair<QString, QString> splitHint(const QString &accept)
{
    const int i = accept.indexOf(QStringLiteral(";hint="));
    if (i < 0) return {accept.trimmed(), {}};
    return {accept.left(i).trimmed(), accept.mid(i + 6).trimmed()};
}

bool typeMatches(const QString &acceptIn, const QString &offered)
{
    const QString accept = splitHint(acceptIn).first;
    if (accept == u"*" || accept == offered) return true;
    const bool offeredArray = offered.endsWith(QStringLiteral("[]"));
    const bool acceptArray = accept.endsWith(QStringLiteral("[]"));
    if (offeredArray && !acceptArray) return false;
    const QString a = baseType(accept), o = baseType(offered);
    if (a == o) return true;
    const QString af = a.section(QLatin1Char('/'), 0, 0), of = o.section(QLatin1Char('/'), 0, 0);
    if (af != of) return false;
    const QString as = a.section(QLatin1Char('/'), 1), os = o.section(QLatin1Char('/'), 1);
    if (af == u"text") return as == u"*" || (as == u"plain" && (os == u"url" || os == u"rich"));
    if (af == u"file") return as == u"*" || as == u"any";
    if (af == u"structured" || af == u"screen") return as == u"*";
    return false;
}

bool contentMatches(const QString &accept, const QJsonObject &content)
{
    const QString hint = splitHint(accept).second;
    if (!typeMatches(accept, content.value(QStringLiteral("type")).toString())) return false;
    return hint.isEmpty() || content.value(QStringLiteral("hints")).toArray().contains(hint);
}

bool acceptsContent(const QJsonArray &accepts, const QJsonObject &content)
{
    for (const auto &a : accepts)
        if (contentMatches(a.toString(), content)) return true;
    return false;
}

bool acceptsType(const QJsonArray &accepts, const QString &offered)
{
    for (const auto &a : accepts)
        if (typeMatches(a.toString(), offered)) return true;
    return false;
}

QString fileKindForPath(const QString &path)
{
    static const QHash<QString, QString> kinds = [] {
        QHash<QString, QString> h;
        const auto add = [&h](const char *kind, std::initializer_list<const char *> exts) {
            for (const char *e : exts) h.insert(QString::fromLatin1(e), QString::fromLatin1(kind));
        };
        add("image", {"png", "jpg", "jpeg", "jpe", "jfif", "gif", "webp", "bmp", "tif", "tiff", "heic", "heif", "avif", "ico",
                      "svg", "jxl", "tga", "qoi", "psd", "raw", "cr2", "nef", "dng", "arw", "exr", "hdr"});
        add("video", {"mp4", "mkv", "mov", "webm", "avi", "m4v", "wmv", "flv", "mpg", "mpeg", "3gp", "ogv"});
        add("audio", {"mp3", "wav", "flac", "ogg", "oga", "opus", "m4a", "aac", "wma", "aiff", "aif", "alac", "mid", "midi"});
        add("pdf", {"pdf"});
        add("document", {"doc", "docx", "odt", "rtf", "pages", "epub"});
        add("spreadsheet", {"xls", "xlsx", "ods", "csv", "tsv", "numbers"});
        add("presentation", {"ppt", "pptx", "odp", "key"});
        add("archive", {"zip", "tar", "gz", "tgz", "bz2", "xz", "7z", "rar", "zst", "lz", "lzma", "cab", "iso"});
        add("text", {"txt", "md", "markdown", "log", "ini", "cfg", "conf", "nfo"});
        add("code", {"rs", "c", "h", "cpp", "hpp", "cc", "py", "js", "mjs", "ts", "tsx", "jsx", "java", "kt", "go", "rb", "php",
                     "swift", "cs", "sh", "bash", "zsh", "fish", "ps1", "json", "yaml", "yml", "toml", "xml", "html", "htm",
                     "css", "scss", "sql", "lua", "dart", "qml", "vue", "svelte"});
        add("font", {"ttf", "otf", "woff", "woff2", "ttc"});
        add("model", {"obj", "stl", "gltf", "glb", "fbx", "3mf", "dae", "ply"});
        return h;
    }();
    const QString name = QFileInfo(path).fileName();
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0) return QStringLiteral("any");
    return kinds.value(name.mid(dot + 1).toLower(), QStringLiteral("any"));
}

QJsonObject fileContent(const QString &path)
{
    const QFileInfo info(path);
    QJsonObject c{{QStringLiteral("path"), info.absoluteFilePath()}};
    if (info.isDir()) {
        c.insert(QStringLiteral("type"), QStringLiteral("folder/reference"));
    } else {
        c.insert(QStringLiteral("type"), QStringLiteral("file/") + fileKindForPath(path));
        if (info.isFile()) c.insert(QStringLiteral("size"), info.size());
    }
    return c;
}

QJsonObject textContent(const QString &type, const QString &text, const QStringList &hints)
{
    QJsonObject c{{QStringLiteral("type"), type}, {QStringLiteral("text"), text}};
    if (!hints.isEmpty()) c.insert(QStringLiteral("hints"), QJsonArray::fromStringList(hints));
    return c;
}

// ---- Wire ------------------------------------------------------------------

Kind classify(const QByteArray &lineIn, QJsonObject *out)
{
    if (lineIn.size() > MaxLineBytes) return Kind::Invalid;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(lineIn.trimmed(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return Kind::Invalid;
    const QJsonObject o = doc.object();
    const QJsonValue v = o.value(QStringLiteral("v"));
    if (!v.isDouble() || v.toInteger(0) < 1) return Kind::Invalid;
    const bool hasId = o.contains(QStringLiteral("id")) && !o.value(QStringLiteral("id")).isNull();
    const bool hasMethod = o.contains(QStringLiteral("method"));
    const bool hasResult = o.contains(QStringLiteral("result"));
    const bool hasError = o.contains(QStringLiteral("error"));
    Kind kind;
    if (hasMethod) {
        if (hasResult || hasError || o.value(QStringLiteral("method")).toString().isEmpty()) return Kind::Invalid;
        kind = hasId ? Kind::Request : Kind::Notification;
    } else {
        if (!hasId || hasResult == hasError) return Kind::Invalid;
        kind = Kind::Response;
    }
    if (out) *out = o;
    return kind;
}

int negotiate(const QList<int> &client, const QList<int> &server)
{
    int best = -1;
    for (int v : client)
        if (server.contains(v) && v > best) best = v;
    return best;
}

static QByteArray line(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n'; }

QByteArray requestLine(qint64 id, const QString &method, const QJsonObject &params)
{
    return line({{QStringLiteral("v"), ProtocolVersion}, {QStringLiteral("id"), id}, {QStringLiteral("method"), method}, {QStringLiteral("params"), params}});
}

QByteArray notificationLine(const QString &method, const QJsonObject &params)
{
    return line({{QStringLiteral("v"), ProtocolVersion}, {QStringLiteral("method"), method}, {QStringLiteral("params"), params}});
}

QByteArray responseLine(qint64 id, const QJsonValue &result)
{
    return line({{QStringLiteral("v"), ProtocolVersion}, {QStringLiteral("id"), id}, {QStringLiteral("result"), result}});
}

QByteArray errorLine(qint64 id, const Error &error)
{
    return line({{QStringLiteral("v"), ProtocolVersion}, {QStringLiteral("id"), id}, {QStringLiteral("error"), error.toJson()}});
}

QString normalizeAccelerator(const QString &accelerator)
{
    QStringList parts;
    for (QString p : accelerator.split(QLatin1Char('+'))) {
        p = p.trimmed().toLower();
        if (p.isEmpty()) continue;
        if (p == u"control" || p == u"ctl") p = QStringLiteral("ctrl");
        else if (p == u"option" || p == u"opt") p = QStringLiteral("alt");
        else if (p == u"command" || p == u"cmd" || p == u"super" || p == u"meta" || p == u"win" || p == u"logo") p = QStringLiteral("super");
        parts << p;
    }
    if (parts.isEmpty()) return {};
    const QString key = parts.takeLast();
    parts.sort();
    parts << key;
    return parts.join(QLatin1Char('+'));
}

QString newToken()
{
    quint32 words[8];
    QRandomGenerator::system()->fillRange(words);
    return QByteArray(reinterpret_cast<const char *>(words), sizeof(words)).toHex();
}

// ---- Manifests and registry ------------------------------------------------

bool parseManifest(const QByteArray &json, QJsonObject *out, QString *error)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = err.errorString();
        return false;
    }
    const QJsonObject o = doc.object();
    if (!o.value(QStringLiteral("schema")).isDouble() || o.value(QStringLiteral("schema")).toInteger(0) < 1) {
        if (error) *error = QStringLiteral("manifest schema must be 1 or later");
        return false;
    }
    if (o.value(QStringLiteral("id")).toString().isEmpty()) {
        if (error) *error = QStringLiteral("manifest has no id");
        return false;
    }
    if (out) *out = o;
    return true;
}

QString nowRfc3339() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); }

bool writeManifest(const Locations &locations, QJsonObject manifest, QString *error)
{
    const QString path = locations.manifestPath(manifest.value(QStringLiteral("id")).toString());
    {
        // Closed before QSaveFile replaces it: Windows can't rename over an open file.
        QFile existing(path);
        if (existing.open(QIODevice::ReadOnly)) {
            QJsonObject old;
            if (parseManifest(existing.readAll(), &old)) {
                old.remove(QStringLiteral("writtenAt"));
                QJsonObject cmp = manifest;
                cmp.remove(QStringLiteral("writtenAt"));
                if (old == cmp) return false;
            }
        }
    }
    manifest.insert(QStringLiteral("writtenAt"), nowRfc3339());
    QDir().mkpath(locations.registry);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) < 0 || !f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

QString executablePath()
{
    const QString exe = QCoreApplication::applicationFilePath();
#ifdef Q_OS_LINUX
    // Only when this binary runs from that AppImage's mount: a process started
    // by some other AppImage inherits APPIMAGE and APPDIR too.
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    const QString appDir = qEnvironmentVariable("APPDIR");
    if (!appImage.isEmpty() && !appDir.isEmpty() && exe.startsWith(QDir(appDir).absolutePath() + QLatin1Char('/'))) return appImage;
#endif
    return exe;
}

static bool onThisPlatform(const QJsonObject &action)
{
    const QJsonArray p = action.value(QStringLiteral("platforms")).toArray();
    return p.isEmpty() || p.contains(currentPlatform());
}

bool actionUsable(const QJsonObject &manifest, const QJsonObject &action)
{
    const bool enabled = manifest.value(QStringLiteral("settings")).toObject().value(QStringLiteral("linkEnabled")).toBool(true);
    return enabled && action.value(QStringLiteral("available")).toBool(true) && onThisPlatform(action);
}

bool actionOffered(const QJsonObject &manifest, const QJsonObject &action, const QJsonObject &content)
{
    return actionUsable(manifest, action) && acceptsContent(action.value(QStringLiteral("accepts")).toArray(), content);
}

Registry::Registry(const Locations &locations, QObject *parent) : QObject(parent), m_locations(locations) {}

bool Registry::refresh()
{
    QHash<QString, Entry> seen;
    const QDir dir(m_locations.registry);
    for (const QFileInfo &info : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        const QString path = info.absoluteFilePath();
        const qint64 mtime = info.lastModified().toMSecsSinceEpoch();
        auto it = m_entries.constFind(path);
        if (it != m_entries.constEnd() && it->mtime == mtime && it->size == info.size()) {
            seen.insert(path, *it);
            continue;
        }
        Entry e;
        e.mtime = mtime;
        e.size = info.size();
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) e.valid = parseManifest(f.readAll(), &e.manifest);
        seen.insert(path, e);
    }
    QList<QJsonObject> apps;
    for (const Entry &e : std::as_const(seen)) {
        if (!e.valid) continue;
        const QString exe = e.manifest.value(QStringLiteral("executable")).toString();
        if (exe.isEmpty() || !QFileInfo(exe).isFile()) continue;
        apps << e.manifest;
    }
    std::sort(apps.begin(), apps.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value(QStringLiteral("id")).toString() < b.value(QStringLiteral("id")).toString();
    });
    m_entries = seen;
    const bool changed = apps != m_apps;
    m_apps = apps;
    return changed;
}

void Registry::watch()
{
    if (m_watcher) return;
    QDir().mkpath(m_locations.registry);
    ensurePrivateDir(m_locations.runtime);
    m_watcher = new QFileSystemWatcher(this);
    m_watcher->addPath(m_locations.registry);
    m_watcher->addPath(m_locations.runtime);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &path) {
        const bool appsChanged = refresh();
        // Endpoints appearing or disappearing change "running" state too.
        if (appsChanged || path == m_locations.runtime) emit changed();
    });
}

QJsonObject Registry::app(const QString &id) const
{
    for (const auto &m : m_apps)
        if (m.value(QStringLiteral("id")).toString() == id) return m;
    return {};
}

QString Registry::shortcutOwner(const QString &me, const QString &accelerator) const
{
    const QString want = normalizeAccelerator(accelerator);
    if (want.isEmpty()) return {};
    for (const auto &m : m_apps) {
        if (m.value(QStringLiteral("id")).toString() == me) continue;
        for (const auto &s : m.value(QStringLiteral("shortcuts")).toArray())
            if (normalizeAccelerator(s.toObject().value(QStringLiteral("accelerator")).toString()) == want)
                return m.value(QStringLiteral("name")).toString();
    }
    return {};
}

// ---- Endpoint --------------------------------------------------------------

bool Endpoint::read(const Locations &locations, const QString &appId, Endpoint *out)
{
    QFile f(locations.endpointPath(appId));
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.value(QStringLiteral("address")).toString().isEmpty()) return false;
    Endpoint e;
    for (const auto &v : o.value(QStringLiteral("protocol")).toArray()) e.protocol << v.toInt();
    e.transport = o.value(QStringLiteral("transport")).toString();
    e.address = o.value(QStringLiteral("address")).toString();
    e.pid = o.value(QStringLiteral("pid")).toInteger();
    e.startedAt = o.value(QStringLiteral("startedAt")).toString();
    e.token = o.value(QStringLiteral("token")).toString();
    if (out) *out = e;
    return true;
}

bool Endpoint::write(const Locations &locations, const QString &appId) const
{
    if (!ensurePrivateDir(locations.runtime)) return false;
    QJsonArray proto;
    for (int p : protocol) proto << p;
    const QJsonObject o{{QStringLiteral("protocol"), proto}, {QStringLiteral("transport"), transport},
                        {QStringLiteral("address"), address}, {QStringLiteral("pid"), pid},
                        {QStringLiteral("startedAt"), startedAt}, {QStringLiteral("token"), token}};
    QSaveFile f(locations.endpointPath(appId));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}

static QString shortHash(const QString &s)
{
    quint64 h = 0xcbf29ce484222325ULL;
    for (const char c : s.toUtf8()) {
        h ^= quint8(c);
        h *= 0x100000001b3ULL;
    }
    return QStringLiteral("%1").arg(h & 0xffffffffffffULL, 12, 16, QLatin1Char('0'));
}

QString socketAddress(const Locations &locations, const QString &appId)
{
#ifdef Q_OS_WIN
    const QString hash = shortHash(qEnvironmentVariable("USERNAME") + QLatin1Char('|') + locations.runtime);
    return QStringLiteral("\\\\.\\pipe\\arcade-%1-%2").arg(hash, appId);
#else
    const QString path = QDir(locations.runtime).filePath(appId + QStringLiteral(".sock"));
    if (path.toUtf8().size() <= 100) return path;
    const QString dir = QStringLiteral("/tmp/arcade-%1").arg(geteuid());
    ensurePrivateDir(dir);
    return QDir(dir).filePath(shortHash(locations.runtime + QLatin1Char('|') + appId) + QStringLiteral(".sock"));
#endif
}

// ---- Server ----------------------------------------------------------------

struct Responder::State {
    QPointer<Server> server;
    QPointer<QLocalSocket> socket;
    qint64 requestId = 0;
    QString peerId;
    QString jobId;
    std::shared_ptr<std::atomic_bool> cancel;
    bool answered = false;
    bool finished = false;
    ~State()
    {
        if (!answered && socket) socket->write(errorLine(requestId, Error::make(QStringLiteral("internal"), QStringLiteral("no answer"))));
        else if (!jobId.isEmpty() && !finished) {
            if (socket)
                socket->write(notificationLine(QStringLiteral("job.done"),
                    {{QStringLiteral("job"), jobId}, {QStringLiteral("status"), QStringLiteral("error")},
                     {QStringLiteral("outputs"), QJsonArray()},
                     {QStringLiteral("error"), Error::make(QStringLiteral("internal"), QStringLiteral("the job ended without a result")).toJson()}}));
            if (server) server->m_jobs.remove(jobId);
        }
    }
};

void Responder::done(const QJsonObject &result) const
{
    if (!d || d->answered) return;
    d->answered = true;
    if (d->socket) d->socket->write(responseLine(d->requestId, result));
}

void Responder::fail(const Error &error) const
{
    if (!d || d->answered) return;
    d->answered = true;
    if (d->socket) d->socket->write(errorLine(d->requestId, error));
}

QString Responder::startJob() const
{
    if (!d || d->answered || !d->server) return {};
    d->answered = true;
    d->jobId = QStringLiteral("j-%1").arg(++d->server->m_nextJob);
    d->cancel = std::make_shared<std::atomic_bool>(false);
    d->server->m_jobs.insert(d->jobId, d->cancel);
    if (d->socket) {
        d->server->m_socketJobs[d->socket.data()] << d->jobId;
        d->socket->write(responseLine(d->requestId, QJsonObject{{QStringLiteral("job"), d->jobId}}));
    }
    return d->jobId;
}

void Responder::progress(double fraction, const QString &message) const
{
    if (!d || d->jobId.isEmpty() || d->finished || !d->socket) return;
    QJsonObject p{{QStringLiteral("job"), d->jobId}, {QStringLiteral("message"), message}};
    if (fraction >= 0) p.insert(QStringLiteral("fraction"), qBound(0.0, fraction, 1.0));
    d->socket->write(notificationLine(QStringLiteral("job.progress"), p));
}

void Responder::finish(const QJsonObject &result) const
{
    if (!d || d->jobId.isEmpty() || d->finished) return;
    d->finished = true;
    QJsonObject p = result;
    p.insert(QStringLiteral("job"), d->jobId);
    if (d->cancel && d->cancel->load()) {
        p = {{QStringLiteral("job"), d->jobId}, {QStringLiteral("status"), QStringLiteral("cancelled")}, {QStringLiteral("outputs"), QJsonArray()},
             {QStringLiteral("error"), Error::make(QStringLiteral("cancelled"), QStringLiteral("cancelled")).toJson()}};
    } else {
        p.insert(QStringLiteral("status"), QStringLiteral("success"));
        if (!p.contains(QStringLiteral("outputs"))) p.insert(QStringLiteral("outputs"), QJsonArray());
    }
    if (d->socket) d->socket->write(notificationLine(QStringLiteral("job.done"), p));
    if (d->server) d->server->m_jobs.remove(d->jobId);
}

void Responder::finishError(const Error &error) const
{
    if (!d || d->jobId.isEmpty() || d->finished) return;
    d->finished = true;
    const QString status = error.code == u"cancelled" || (d->cancel && d->cancel->load()) ? QStringLiteral("cancelled") : QStringLiteral("error");
    const QJsonObject p{{QStringLiteral("job"), d->jobId}, {QStringLiteral("status"), status}, {QStringLiteral("outputs"), QJsonArray()},
                        {QStringLiteral("message"), error.message}, {QStringLiteral("error"), error.toJson()}};
    if (d->socket) d->socket->write(notificationLine(QStringLiteral("job.done"), p));
    if (d->server) d->server->m_jobs.remove(d->jobId);
}

bool Responder::cancelled() const { return d && d->cancel && d->cancel->load(); }
QString Responder::peerId() const { return d ? d->peerId : QString(); }

Server::Server(const QString &appId, const QString &version, const Locations &locations, QObject *parent)
    : QObject(parent), m_appId(appId), m_version(version), m_locations(locations)
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, &Server::onConnection);
}

Server::~Server() { stop(); }

bool Server::start(QString *error)
{
    if (m_server.isListening()) return true;
    if (!ensurePrivateDir(m_locations.runtime)) {
        if (error) *error = QStringLiteral("cannot create the private runtime directory %1").arg(m_locations.runtime);
        return false;
    }
    {
        Error e;
        if (Client::connect(m_locations, m_appId, m_appId, m_version, HelloTimeoutMs, &e)) {
            if (error) *error = QStringLiteral("%1 is already serving the Link").arg(m_appId);
            return false;
        }
    }
    const QString address = socketAddress(m_locations, m_appId);
    QLocalServer::removeServer(address); // a dead process may leave its socket behind
    if (!m_server.listen(address)) {
        if (error) *error = m_server.errorString();
        return false;
    }
    m_token = newToken();
    Endpoint ep;
    ep.protocol = supportedProtocols();
#ifdef Q_OS_WIN
    ep.transport = QStringLiteral("pipe");
#else
    ep.transport = QStringLiteral("unix");
#endif
    ep.address = m_server.fullServerName();
    ep.pid = QCoreApplication::applicationPid();
    ep.startedAt = nowRfc3339();
    ep.token = m_token;
    if (!ep.write(m_locations, m_appId)) {
        m_server.close();
        if (error) *error = QStringLiteral("cannot write the endpoint file");
        return false;
    }
    return true;
}

void Server::stop()
{
    if (!m_server.isListening()) return;
    Endpoint ep;
    if (Endpoint::read(m_locations, m_appId, &ep) && ep.token == m_token) QFile::remove(m_locations.endpointPath(m_appId));
    for (auto &flag : m_jobs) flag->store(true);
    m_server.close();
    const auto sockets = m_authed.keys();
    for (QLocalSocket *s : sockets) s->disconnectFromServer();
}

void Server::notifyChanged()
{
    const QByteArray n = notificationLine(QStringLiteral("app.changed"), {{QStringLiteral("app"), m_appId}});
    for (auto it = m_topics.cbegin(); it != m_topics.cend(); ++it)
        if (it.value().contains(QStringLiteral("app.changed"))) it.key()->write(n);
}

void Server::onConnection()
{
    while (QLocalSocket *socket = m_server.nextPendingConnection()) {
        m_authed.insert(socket, false);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { onReadable(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            for (const QString &job : m_socketJobs.value(socket))
                if (auto flag = m_jobs.value(job)) flag->store(true); // nobody is left to receive it
            m_socketJobs.remove(socket);
            m_authed.remove(socket);
            m_topics.remove(socket);
            socket->deleteLater();
        });
        // A connection that never says hello is dropped (a one-off timer
        // per connection, not an idle cost).
        QTimer::singleShot(2000, socket, [this, socket] {
            if (!m_authed.value(socket, true)) socket->disconnectFromServer();
        });
        if (socket->bytesAvailable()) onReadable(socket);
    }
}

void Server::onReadable(QLocalSocket *socket)
{
    while (socket->canReadLine()) {
        const QByteArray raw = socket->readLine(MaxLineBytes + 1);
        if (raw.trimmed().isEmpty()) continue;
        QJsonObject m;
        if (classify(raw, &m) != Kind::Request) continue;
        handle(socket, m);
    }
    if (socket->bytesAvailable() > MaxLineBytes) socket->disconnectFromServer();
}

void Server::handle(QLocalSocket *socket, const QJsonObject &m)
{
    const qint64 id = m.value(QStringLiteral("id")).toInteger();
    const QString method = m.value(QStringLiteral("method")).toString();
    const QJsonObject params = m.value(QStringLiteral("params")).toObject();
    if (!m_authed.value(socket)) {
        if (method != u"hello" || params.value(QStringLiteral("token")).toString() != m_token || m_token.isEmpty()) {
            socket->write(errorLine(id, Error::make(QStringLiteral("denied"), QStringLiteral("denied"), QStringLiteral("token"))));
            socket->flush();
            socket->disconnectFromServer();
            return;
        }
        QList<int> theirs;
        for (const auto &v : params.value(QStringLiteral("protocol")).toArray()) theirs << v.toInt();
        const int version = negotiate(theirs, supportedProtocols());
        if (version < 0) {
            socket->write(errorLine(id, Error::make(QStringLiteral("version_mismatch"), QStringLiteral("no common protocol version"))));
            socket->flush();
            socket->disconnectFromServer();
            return;
        }
        m_authed[socket] = true;
        socket->setProperty("arcadePeer", params.value(QStringLiteral("client")).toObject().value(QStringLiteral("id")).toString());
        socket->write(responseLine(id, QJsonObject{{QStringLiteral("server"), QJsonObject{{QStringLiteral("id"), m_appId}, {QStringLiteral("version"), m_version}}},
                                                   {QStringLiteral("protocol"), version}}));
        return;
    }
    if (method == u"describe") {
        socket->write(responseLine(id, QJsonObject{{QStringLiteral("actions"), describe ? describe() : QJsonArray()}}));
    } else if (method == u"invoke") {
        Responder r;
        r.d = std::make_shared<Responder::State>();
        r.d->server = this;
        r.d->socket = socket;
        r.d->requestId = id;
        r.d->peerId = socket->property("arcadePeer").toString();
        if (invoke) invoke(params, r);
        else r.fail(Error::make(QStringLiteral("unavailable"), QStringLiteral("no actions"), QStringLiteral("no actions")));
    } else if (method == u"job.cancel") {
        const auto flag = m_jobs.value(params.value(QStringLiteral("job")).toString());
        if (flag) flag->store(true);
        socket->write(responseLine(id, QJsonObject{{QStringLiteral("cancelled"), bool(flag)}}));
    } else if (method == u"subscribe") {
        QStringList topics;
        for (const auto &t : params.value(QStringLiteral("topics")).toArray()) topics << t.toString();
        m_topics.insert(socket, topics);
        socket->write(responseLine(id, QJsonObject{{QStringLiteral("topics"), QJsonArray::fromStringList(topics)}}));
    } else if (method == u"app.status") {
        socket->write(responseLine(id, QJsonObject{{QStringLiteral("id"), m_appId}, {QStringLiteral("version"), m_version},
                                                   {QStringLiteral("pid"), QCoreApplication::applicationPid()},
                                                   {QStringLiteral("protocol"), ProtocolVersion}, {QStringLiteral("busy"), busy()},
                                                   {QStringLiteral("jobs"), m_jobs.size()},
                                                   {QStringLiteral("status"), status ? QJsonValue(status()) : QJsonValue()}}));
    } else if (method == u"app.activate") {
        if (activate && activate()) socket->write(responseLine(id, QJsonObject{{QStringLiteral("activated"), true}}));
        else socket->write(errorLine(id, Error::make(QStringLiteral("unavailable"), QStringLiteral("cannot activate"), QStringLiteral("this app has no window to show"))));
    } else if (method == u"app.quit") {
        if (busy() && !params.value(QStringLiteral("force")).toBool()) {
            socket->write(errorLine(id, Error::make(QStringLiteral("busy"), QStringLiteral("jobs are running"))));
        } else {
            socket->write(responseLine(id, QJsonObject{{QStringLiteral("quitting"), true}}));
            socket->flush();
            if (quit) QTimer::singleShot(0, this, [this] { quit(); });
        }
    } else {
        socket->write(errorLine(id, Error::make(QStringLiteral("internal"), QStringLiteral("unknown method %1").arg(method))));
    }
}

// ---- Client ----------------------------------------------------------------

Client::~Client() = default;

std::unique_ptr<Client> Client::connect(const Locations &locations, const QString &appId, const QString &myId,
                                        const QString &myVersion, int timeoutMs, Error *error)
{
    const auto notRunning = [&] {
        if (error) *error = Error::make(QStringLiteral("not_running"), QStringLiteral("%1 is not running").arg(appId));
        return nullptr;
    };
    Endpoint ep;
    if (!Endpoint::read(locations, appId, &ep)) return notRunning();
    std::unique_ptr<Client> c(new Client);
    c->m_socket = std::make_unique<QLocalSocket>();
    c->m_socket->connectToServer(ep.address);
    if (!c->m_socket->waitForConnected(timeoutMs)) return notRunning();
    QJsonArray proto;
    for (int p : supportedProtocols()) proto << p;
    Error e;
    const QJsonValue r = c->call(QStringLiteral("hello"),
        {{QStringLiteral("token"), ep.token}, {QStringLiteral("client"), QJsonObject{{QStringLiteral("id"), myId}, {QStringLiteral("version"), myVersion}}},
         {QStringLiteral("protocol"), proto}}, &e, timeoutMs);
    if (e.isError()) {
        if (e.code == u"internal") return notRunning();
        if (error) *error = e;
        return nullptr;
    }
    const QJsonObject server = r.toObject().value(QStringLiteral("server")).toObject();
    c->m_serverId = server.value(QStringLiteral("id")).toString();
    c->m_serverVersion = server.value(QStringLiteral("version")).toString();
    if (!supportedProtocols().contains(r.toObject().value(QStringLiteral("protocol")).toInt())) {
        if (error) *error = Error::make(QStringLiteral("version_mismatch"), QStringLiteral("no common protocol version"));
        return nullptr;
    }
    return c;
}

bool Client::readMessage(QJsonObject *out, int timeoutMs, Error *error)
{
    QElapsedTimer t;
    t.start();
    for (;;) {
        const qsizetype nl = m_buffer.indexOf('\n');
        if (nl >= 0) {
            const QByteArray raw = m_buffer.left(nl + 1);
            m_buffer.remove(0, nl + 1);
            if (raw.trimmed().isEmpty()) continue;
            if (classify(raw, out) == Kind::Invalid) continue;
            return true;
        }
        if (m_buffer.size() > MaxLineBytes) {
            if (error) *error = Error::make(QStringLiteral("too_large"), QStringLiteral("message too large"), {}, MaxLineBytes);
            return false;
        }
        const int left = timeoutMs < 0 ? -1 : int(qMax<qint64>(0, timeoutMs - t.elapsed()));
        if (m_socket->bytesAvailable() == 0 && !m_socket->waitForReadyRead(left)) {
            if (m_socket->state() != QLocalSocket::ConnectedState) {
                if (error) *error = Error::make(QStringLiteral("internal"), QStringLiteral("connection closed"));
            } else if (error) {
                *error = Error::make(QStringLiteral("timeout"), QStringLiteral("no answer in time"));
            }
            return false;
        }
        m_buffer += m_socket->readAll();
    }
}

QJsonValue Client::call(const QString &method, const QJsonObject &params, Error *error, int timeoutMs)
{
    const qint64 id = ++m_nextId;
    m_socket->write(requestLine(id, method, params));
    m_socket->waitForBytesWritten(timeoutMs);
    QElapsedTimer t;
    t.start();
    for (;;) {
        QJsonObject m;
        const int left = int(qMax<qint64>(0, timeoutMs - t.elapsed()));
        if (!readMessage(&m, left, error)) return {};
        if (m.contains(QStringLiteral("method"))) {
            if (!m.contains(QStringLiteral("id"))) m_notifications << m;
            continue;
        }
        if (m.value(QStringLiteral("id")).toInteger() != id) continue;
        if (m.contains(QStringLiteral("error"))) {
            if (error) *error = Error::fromJson(m.value(QStringLiteral("error")).toObject());
            return {};
        }
        return m.value(QStringLiteral("result"));
    }
}

bool Client::invoke(const QJsonObject &request, QJsonObject *result, Error *error,
                    const std::function<void(double, const QString &)> &progress, const std::atomic_bool *cancel)
{
    Error e;
    const QJsonObject r = call(QStringLiteral("invoke"), request, &e).toObject();
    if (e.isError()) {
        if (error) *error = e;
        return false;
    }
    const QString job = r.value(QStringLiteral("job")).toString();
    if (job.isEmpty()) {
        if (result) *result = r;
        return true;
    }
    bool cancelSent = false;
    for (;;) {
        if (cancel && cancel->load() && !cancelSent) {
            cancelSent = true;
            m_socket->write(requestLine(++m_nextId, QStringLiteral("job.cancel"), {{QStringLiteral("job"), job}}));
            m_socket->waitForBytesWritten(1000);
        }
        QJsonObject m;
        if (!m_notifications.isEmpty()) {
            m = m_notifications.takeFirst();
        } else {
            Error re;
            // While a cancel flag is supplied, wake every 100 ms to check it.
            if (!readMessage(&m, cancel ? 100 : -1, &re)) {
                if (re.code == u"timeout") continue;
                if (error) *error = Error::make(QStringLiteral("not_running"), QStringLiteral("%1 stopped while working").arg(m_serverId));
                return false;
            }
        }
        const QJsonObject p = m.value(QStringLiteral("params")).toObject();
        if (p.value(QStringLiteral("job")).toString() != job) continue;
        const QString method = m.value(QStringLiteral("method")).toString();
        if (method == u"job.progress") {
            if (progress) progress(p.contains(QStringLiteral("fraction")) ? p.value(QStringLiteral("fraction")).toDouble() : -1.0,
                                   p.value(QStringLiteral("message")).toString());
        } else if (method == u"job.done") {
            const QString status = p.value(QStringLiteral("status")).toString();
            if (status == u"success") {
                if (result) *result = p;
                return true;
            }
            if (error) {
                *error = p.contains(QStringLiteral("error")) ? Error::fromJson(p.value(QStringLiteral("error")).toObject())
                                                             : Error::make(status == u"cancelled" ? QStringLiteral("cancelled") : QStringLiteral("internal"),
                                                                           p.value(QStringLiteral("message")).toString());
            }
            return false;
        }
    }
}

static QJsonObject findAction(const QJsonObject &manifest, const QJsonObject &request)
{
    const QString action = request.value(QStringLiteral("action")).toString();
    const QString preset = request.value(QStringLiteral("preset")).toString();
    QJsonObject plain;
    for (const auto &v : manifest.value(QStringLiteral("actions")).toArray()) {
        const QJsonObject a = v.toObject();
        const QString id = a.value(QStringLiteral("id")).toString();
        if (!preset.isEmpty() && id == action + QLatin1Char('#') + preset) return a;
        if (id == action) plain = a;
    }
    return plain;
}

static bool runOneShot(const QJsonObject &manifest, const QJsonObject &request, QJsonObject *result, Error *error,
                       const std::function<void(double, const QString &)> &progress, const std::atomic_bool *cancel)
{
    QStringList args;
    for (const auto &a : manifest.value(QStringLiteral("launch")).toObject().value(QStringLiteral("invoke")).toArray()) args << a.toString();
    QProcess p;
    p.setProcessChannelMode(QProcess::SeparateChannels);
    p.setStandardErrorFile(QProcess::nullDevice());
    p.start(manifest.value(QStringLiteral("executable")).toString(), args);
    if (!p.waitForStarted(LaunchTimeoutMs)) {
        if (error) *error = Error::make(QStringLiteral("launch_failed"), p.errorString());
        return false;
    }
    p.write(requestLine(1, QStringLiteral("invoke"), request));
    p.closeWriteChannel();
    QByteArray buffer;
    for (;;) {
        if (cancel && cancel->load()) {
            p.kill();
            p.waitForFinished(1000);
            if (error) *error = Error::make(QStringLiteral("cancelled"), QStringLiteral("cancelled"));
            return false;
        }
        const bool more = p.waitForReadyRead(100);
        buffer += p.readAllStandardOutput();
        qsizetype nl;
        while ((nl = buffer.indexOf('\n')) >= 0) {
            const QByteArray raw = buffer.left(nl + 1);
            buffer.remove(0, nl + 1);
            QJsonObject m;
            const Kind k = classify(raw, &m);
            if (k == Kind::Response) {
                p.waitForFinished(1000);
                if (m.contains(QStringLiteral("error"))) {
                    if (error) *error = Error::fromJson(m.value(QStringLiteral("error")).toObject());
                    return false;
                }
                if (result) *result = m.value(QStringLiteral("result")).toObject();
                return true;
            }
            if (k == Kind::Notification && m.value(QStringLiteral("method")).toString() == u"job.progress" && progress) {
                const QJsonObject pr = m.value(QStringLiteral("params")).toObject();
                progress(pr.contains(QStringLiteral("fraction")) ? pr.value(QStringLiteral("fraction")).toDouble() : -1.0,
                         pr.value(QStringLiteral("message")).toString());
            }
        }
        if (!more && p.state() == QProcess::NotRunning) {
            if (error) *error = Error::make(QStringLiteral("internal"), QStringLiteral("one-shot process ended without a result"));
            return false;
        }
    }
}

bool invokeAction(const Locations &locations, const QString &myId, const QString &myVersion, const QJsonObject &manifest,
                  const QJsonObject &request, QJsonObject *result, Error *error,
                  const std::function<void(double, const QString &)> &progress, const std::atomic_bool *cancel,
                  const std::function<void()> &onLaunching)
{
    const QString appId = manifest.value(QStringLiteral("id")).toString();
    const QString name = manifest.value(QStringLiteral("name")).toString();
    if (!manifest.value(QStringLiteral("settings")).toObject().value(QStringLiteral("linkEnabled")).toBool(true)) {
        if (error) *error = Error::make(QStringLiteral("denied"), QStringLiteral("link disabled"), QStringLiteral("disabled"));
        return false;
    }
    const QJsonObject action = findAction(manifest, request);
    if (action.isEmpty()) {
        if (error) *error = Error::make(QStringLiteral("unavailable"), QStringLiteral("no such action"));
        return false;
    }
    if (!action.value(QStringLiteral("available")).toBool(true)) {
        const QString reason = action.value(QStringLiteral("reason")).toString();
        if (error) *error = Error::make(QStringLiteral("unavailable"), reason, reason);
        return false;
    }
    if (auto c = Client::connect(locations, appId, myId, myVersion)) return c->invoke(request, result, error, progress, cancel);
    const QJsonValue invokeArgs = manifest.value(QStringLiteral("launch")).toObject().value(QStringLiteral("invoke"));
    if (!action.value(QStringLiteral("interactive")).toBool() && invokeArgs.isArray())
        return runOneShot(manifest, request, result, error, progress, cancel);
    const QString exe = manifest.value(QStringLiteral("executable")).toString();
    if (!QFileInfo(exe).isFile()) {
        if (error) *error = Error::make(QStringLiteral("not_installed"), QStringLiteral("%1 is not installed").arg(name));
        return false;
    }
    if (onLaunching) onLaunching();
    QStringList args;
    for (const auto &a : manifest.value(QStringLiteral("launch")).toObject().value(QStringLiteral("background")).toArray()) args << a.toString();
    if (!QProcess::startDetached(exe, args)) {
        if (error) *error = Error::make(QStringLiteral("launch_failed"), QStringLiteral("could not start %1").arg(name));
        return false;
    }
    QElapsedTimer t;
    t.start();
    // A bounded wait during a user-initiated launch (not idle polling).
    while (t.elapsed() < LaunchTimeoutMs) {
        if (auto c = Client::connect(locations, appId, myId, myVersion)) return c->invoke(request, result, error, progress, cancel);
        QThread::msleep(20);
    }
    if (error) *error = Error::make(QStringLiteral("launch_failed"), QStringLiteral("%1 did not start in time").arg(name));
    return false;
}

QString appState(const Locations &locations, const Registry &registry, const QString &appId, const QString &myId, QString *version)
{
    const QJsonObject m = registry.app(appId);
    if (m.isEmpty()) return QStringLiteral("NotInstalled");
    if (version) *version = m.value(QStringLiteral("version")).toString();
    if (auto c = Client::connect(locations, appId, myId, {})) {
        if (version && !c->serverVersion().isEmpty()) *version = c->serverVersion();
        return QStringLiteral("Running");
    }
    return QStringLiteral("Installed");
}

} // namespace ArcadeLink
