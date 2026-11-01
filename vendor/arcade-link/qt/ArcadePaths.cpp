#include "ArcadePaths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QOperatingSystemVersion>
#include <QSysInfo>

namespace ArcadeLink {

#include "ArcadeAppDirsTable.inc"

static QString envPath(const char *name, const QString &fallback)
{
    const QString v = qEnvironmentVariable(name);
    return !v.isEmpty() && QDir::isAbsolutePath(v) ? v : fallback;
}

UserDirs UserDirs::discover()
{
    const QString home = QDir::homePath();
    return {home,
            envPath("XDG_CONFIG_HOME", home + QStringLiteral("/.config")),
            envPath("XDG_DATA_HOME", home + QStringLiteral("/.local/share")),
            envPath("XDG_CACHE_HOME", home + QStringLiteral("/.cache")),
            envPath("APPDATA", home + QStringLiteral("/AppData/Roaming")),
            envPath("LOCALAPPDATA", home + QStringLiteral("/AppData/Local"))};
}

UserDirs UserDirs::under(const QString &root)
{
    return {root, root + QStringLiteral("/.config"), root + QStringLiteral("/.local/share"), root + QStringLiteral("/.cache"),
            root + QStringLiteral("/AppData/Roaming"), root + QStringLiteral("/AppData/Local")};
}

QString UserDirs::base(const QString &name) const
{
    const QString library = home + QStringLiteral("/Library/");
    if (name == u"config") return config;
    if (name == u"data") return data;
    if (name == u"cache") return cache;
    if (name == u"roaming") return roaming;
    if (name == u"local") return local;
    if (name == u"support") return library + QStringLiteral("Application Support");
    if (name == u"caches") return library + QStringLiteral("Caches");
    if (name == u"preferences") return library + QStringLiteral("Preferences");
    if (name == u"webkit") return library + QStringLiteral("WebKit");
    return {};
}

QString AppDirs::get(const QString &kind) const
{
    return kind == u"config" ? config : kind == u"cache" ? cache : data;
}

static void folderAndName(const QString &appId, QString *folder, QString *name)
{
    for (const ArcadeAppDirsRow &row : kArcadeAppDirs) {
        if (appId == QLatin1String(row.id)) {
            *folder = QString::fromLatin1(row.folder);
            *name = QString::fromLatin1(row.name);
            return;
        }
    }
    *folder = appId.startsWith(QStringLiteral("arcade.")) ? appId.mid(7) : appId;
    *name = folder->isEmpty() ? *folder : folder->left(1).toUpper() + folder->mid(1);
}

AppDirs appDirs(const QString &appId) { return appDirs(UserDirs::discover(), currentPlatform(), appId); }

AppDirs appDirs(const UserDirs &user, const QString &platform, const QString &appId)
{
    QString folder, name;
    folderAndName(appId, &folder, &name);
    if (platform == u"windows") {
        const QString roaming = user.roaming + QStringLiteral("/Arcade/") + name;
        return {roaming, roaming, user.local + QStringLiteral("/Arcade/") + name};
    }
    if (platform == u"macos") {
        const QString support = user.base(QStringLiteral("support")) + QStringLiteral("/Arcade/") + name;
        return {support, support, user.base(QStringLiteral("caches")) + QStringLiteral("/Arcade/") + name};
    }
    return {user.config + QStringLiteral("/arcade/") + folder, user.data + QStringLiteral("/arcade/") + folder,
            user.cache + QStringLiteral("/arcade/") + folder};
}

QString LegacyDir::target(const AppDirs &dirs) const
{
    const QString base = dirs.get(kind);
    return into.isEmpty() ? base : base + QLatin1Char('/') + into;
}

QList<LegacyDir> legacyDirs(const QString &appId) { return legacyDirs(UserDirs::discover(), currentPlatform(), appId); }

QList<LegacyDir> legacyDirs(const UserDirs &user, const QString &platform, const QString &appId)
{
    QList<LegacyDir> out;
    for (const ArcadeAppDirsRow &row : kArcadeAppDirs) {
        if (appId != QLatin1String(row.id) || platform != QLatin1String(row.platform)) continue;
        for (int i = 0; i < row.count; ++i) {
            const ArcadeLegacyEntry &e = row.entries[i];
            out.append({QString::fromLatin1(e.kind), user.base(QString::fromLatin1(e.base)) + QLatin1Char('/') + QString::fromUtf8(e.path), QString::fromUtf8(e.into)});
        }
    }
    return out;
}

static bool sameContents(const QString &a, const QString &b)
{
    QFile fa(a), fb(b);
    if (fa.size() != fb.size() || !fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly)) return false;
    while (!fa.atEnd()) {
        if (fa.read(1 << 16) != fb.read(1 << 16)) return false;
    }
    return true;
}

static QString migrateOne(const QString &from, const QString &to)
{
    QStringList files, folders;
    QDirIterator it(from, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        if (info.isSymLink()) return info.filePath() + QStringLiteral(" is a link");
        (info.isDir() ? folders : files).append(QDir(from).relativeFilePath(info.filePath()));
    }
    for (const QString &rel : files) {
        const QString target = to + QLatin1Char('/') + rel;
        if (QFileInfo::exists(target) && !sameContents(from + QLatin1Char('/') + rel, target))
            return target + QStringLiteral(" already exists with different contents");
    }
    QStringList copied;
    auto rollback = [&copied](const QString &reason) {
        for (const QString &path : copied) QFile::remove(path);
        return reason;
    };
    if (!QDir().mkpath(to)) return QStringLiteral("couldn't create ") + to;
    for (const QString &rel : folders) {
        if (!QDir().mkpath(to + QLatin1Char('/') + rel)) return rollback(QStringLiteral("couldn't create ") + to + QLatin1Char('/') + rel);
    }
    for (const QString &rel : files) {
        const QString source = from + QLatin1Char('/') + rel, target = to + QLatin1Char('/') + rel;
        if (QFileInfo::exists(target)) continue;
        if (!QFile::copy(source, target)) return rollback(QStringLiteral("couldn't copy ") + source);
        copied.append(target);
        QFile out(target);
        if (out.open(QIODevice::ReadWrite)) out.setFileTime(QFileInfo(source).lastModified(), QFileDevice::FileModificationTime);
    }
    for (const QString &rel : files) {
        if (!sameContents(from + QLatin1Char('/') + rel, to + QLatin1Char('/') + rel))
            return rollback(to + QLatin1Char('/') + rel + QStringLiteral(" differs after copying"));
    }
    if (!QDir(from).removeRecursively()) return QStringLiteral("copied, but couldn't remove ") + from;
    return {};
}

Migration migrateAppDirs(const QString &appId) { return migrateAppDirs(UserDirs::discover(), currentPlatform(), appId); }

Migration migrateAppDirs(const UserDirs &user, const QString &platform, const QString &appId)
{
    Migration report;
    const AppDirs dirs = appDirs(user, platform, appId);
    QStringList bases;
    for (const char *b : {"config", "data", "cache", "roaming", "local", "support", "caches", "preferences", "webkit"})
        bases.append(QDir::cleanPath(user.base(QString::fromLatin1(b))));
    for (const LegacyDir &legacy : legacyDirs(user, platform, appId)) {
        const QFileInfo info(legacy.path);
        if (!info.exists() && !info.isSymLink()) continue;
        if (info.isSymLink() || !info.isDir()) {
            report.kept.append({legacy.path, QStringLiteral("not a real folder")});
            continue;
        }
        const QString to = legacy.target(dirs), from = QDir::cleanPath(legacy.path);
        if ((to + QLatin1Char('/')).startsWith(from + QLatin1Char('/')) || (from + QLatin1Char('/')).startsWith(to + QLatin1Char('/'))) {
            report.kept.append({legacy.path, QStringLiteral("overlaps ") + to});
            continue;
        }
        const QString reason = migrateOne(from, to);
        if (!reason.isEmpty()) {
            report.kept.append({legacy.path, reason});
            continue;
        }
        // Remove now-empty parents such as ~/.config/Arcade Wheel, up to the base.
        for (QString dir = QFileInfo(from).path(); !bases.contains(QDir::cleanPath(dir)) && QDir().rmdir(dir);)
            dir = QFileInfo(dir).path();
        report.moved.append({legacy.path, to});
    }
    return report;
}

// ---- Diagnostics header -----------------------------------------------------

DiagnosticsHeader DiagnosticsHeader::create(const QString &appId, const QString &version, const QString &channel)
{
    DiagnosticsHeader h;
    h.appId = appId;
    h.version = version;
    h.channel = channel;
    h.os = QSysInfo::prettyProductName() + QStringLiteral(" (") + QSysInfo::currentCpuArchitecture() + QLatin1Char(')');
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    h.desktop = QStringLiteral("native");
#else
    QString desktop = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE").isEmpty() ? qEnvironmentVariable("XDG_CURRENT_DESKTOP") : QStringLiteral("Hyprland");
    QString session = qEnvironmentVariable("XDG_SESSION_TYPE");
    if (session.isEmpty()) session = qEnvironmentVariable("WAYLAND_DISPLAY").isEmpty() ? QStringLiteral("x11") : QStringLiteral("wayland");
    h.desktop = (desktop.isEmpty() ? QStringLiteral("unknown") : desktop) + QStringLiteral(" (") + session + QLatin1Char(')');
#endif
    return h;
}

void DiagnosticsHeader::peersFrom(const Locations &locations)
{
    Registry registry(locations);
    registry.refresh();
    peers.clear();
    QStringList ids = Ids::apps();
    ids.append(Ids::Tools);
    for (const QString &id : ids) {
        if (id == appId) continue;
        QString version;
        const QString state = appState(locations, registry, id, appId, &version);
        peers.append({id, state == u"Running" ? QStringLiteral("running ") + version
                          : state == u"Installed" ? QStringLiteral("installed ") + version
                                                  : QStringLiteral("not installed")});
    }
}

QString DiagnosticsHeader::render() const
{
    QString out = appName(appId) + QLatin1Char(' ') + version + QStringLiteral(" (") + channel + QStringLiteral(")\n");
    out += QStringLiteral("OS: ") + os + QLatin1Char('\n');
    out += QStringLiteral("Desktop: ") + desktop + QLatin1Char('\n');
    out += QStringLiteral("Link: protocol %1, connections %2\n").arg(ProtocolVersion).arg(linkEnabled ? QStringLiteral("on") : QStringLiteral("off"));
    if (!peers.isEmpty()) {
        QStringList parts;
        for (const auto &p : peers) {
            QString name = appName(p.first);
            if (name.startsWith(QStringLiteral("Arcade "))) name = name.mid(7);
            parts.append(name + QLatin1Char(' ') + p.second);
        }
        out += QStringLiteral("Peers: ") + parts.join(QStringLiteral(", ")) + QLatin1Char('\n');
    }
    if (!engines.isEmpty()) {
        QStringList parts;
        for (const auto &e : engines) parts.append(e.first + QLatin1Char(' ') + e.second);
        out += QStringLiteral("Engines: ") + parts.join(QStringLiteral(", ")) + QLatin1Char('\n');
    }
    return out;
}

} // namespace ArcadeLink
