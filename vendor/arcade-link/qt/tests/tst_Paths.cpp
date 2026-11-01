// App folders, legacy locations, migration and the diagnostics header: the
// same cases as the Rust crate's paths and diagnostics tests.
#include "ArcadePaths.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace ArcadeLink;

class PathsTest : public QObject {
    Q_OBJECT

    static void write(const QString &path, const QByteArray &bytes)
    {
        QDir().mkpath(QFileInfo(path).path());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(bytes);
    }
    static QByteArray read(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

private slots:
    void newLayoutPerPlatform()
    {
        const UserDirs user = UserDirs::under(QStringLiteral("/r"));
        const AppDirs onLinux = appDirs(user, QStringLiteral("linux"), Ids::Box);
        QCOMPARE(onLinux.config, QStringLiteral("/r/.config/arcade/box"));
        QCOMPARE(onLinux.data, QStringLiteral("/r/.local/share/arcade/box"));
        QCOMPARE(onLinux.settingsFile(), QStringLiteral("/r/.config/arcade/box/settings.json"));
        QCOMPARE(appDirs(user, QStringLiteral("windows"), Ids::Clipboard).cache, QStringLiteral("/r/AppData/Local/Arcade/Clipboard"));
        QCOMPARE(appDirs(user, QStringLiteral("macos"), Ids::Find).config, QStringLiteral("/r/Library/Application Support/Arcade/Find"));
        QCOMPARE(legacyDirs(user, QStringLiteral("linux"), Ids::Wheel).first().path, QStringLiteral("/r/.config/Arcade Wheel/Arcade Wheel"));
        for (const QString &platform : {QStringLiteral("linux"), QStringLiteral("windows"), QStringLiteral("macos")}) {
            QStringList ids = Ids::apps();
            ids.append(Ids::Tools);
            for (const QString &id : ids) QVERIFY2(!legacyDirs(user, platform, id).isEmpty(), qPrintable(id + platform));
        }
    }

    void migrationMovesAndIsIdempotent()
    {
        QTemporaryDir root;
        const UserDirs user = UserDirs::under(root.path());
        const QString old = root.path() + QStringLiteral("/.config/Arcade Wheel/Arcade Wheel");
        write(old + QStringLiteral("/config.json"), "{\"v\":1}");
        write(old + QStringLiteral("/decks/main.json"), "[]");
        const Migration report = migrateAppDirs(user, QStringLiteral("linux"), Ids::Wheel);
        QCOMPARE(report.moved.size(), 1);
        const QString fresh = root.path() + QStringLiteral("/.config/arcade/wheel");
        QCOMPARE(read(fresh + QStringLiteral("/config.json")), QByteArray("{\"v\":1}"));
        QCOMPARE(read(fresh + QStringLiteral("/decks/main.json")), QByteArray("[]"));
        QVERIFY(!QFileInfo::exists(old));
        QVERIFY(!QFileInfo::exists(root.path() + QStringLiteral("/.config/Arcade Wheel")));
        QVERIFY(migrateAppDirs(user, QStringLiteral("linux"), Ids::Wheel).isEmpty());
    }

    void webviewStorageMovesIntoItsSubfolder()
    {
        QTemporaryDir root;
        const UserDirs user = UserDirs::under(root.path());
        write(root.path() + QStringLiteral("/.local/share/arcade-tools/preferences.json"), "{}");
        write(root.path() + QStringLiteral("/.local/share/app.arcadetools/localstorage/file.db"), "db");
        const Migration report = migrateAppDirs(user, QStringLiteral("linux"), Ids::Tools);
        QCOMPARE(report.moved.size(), 2);
        QCOMPARE(read(root.path() + QStringLiteral("/.local/share/arcade/tools/preferences.json")), QByteArray("{}"));
        QCOMPARE(read(root.path() + QStringLiteral("/.cache/arcade/tools/webview/localstorage/file.db")), QByteArray("db"));
        QVERIFY(!QFileInfo::exists(root.path() + QStringLiteral("/.local/share/app.arcadetools")));
    }

    void conflictLeavesEverything()
    {
        QTemporaryDir root;
        const UserDirs user = UserDirs::under(root.path());
        const QString old = root.path() + QStringLiteral("/.config/arcade-find");
        write(old + QStringLiteral("/settings.json"), "old");
        write(old + QStringLiteral("/extra.json"), "x");
        const QString fresh = root.path() + QStringLiteral("/.config/arcade/find");
        write(fresh + QStringLiteral("/settings.json"), "new");
        const Migration report = migrateAppDirs(user, QStringLiteral("linux"), Ids::Find);
        QVERIFY(report.moved.isEmpty());
        QCOMPARE(report.kept.size(), 1);
        QCOMPARE(read(old + QStringLiteral("/settings.json")), QByteArray("old"));
        QCOMPARE(read(fresh + QStringLiteral("/settings.json")), QByteArray("new"));
        QVERIFY(!QFileInfo::exists(fresh + QStringLiteral("/extra.json")));
    }

    void diagnosticsHeader()
    {
        DiagnosticsHeader h = DiagnosticsHeader::create(Ids::Box, QStringLiteral("0.4.0"), QStringLiteral("stable"));
        h.os = QStringLiteral("Ubuntu 24.04 (x86_64)");
        h.desktop = QStringLiteral("GNOME (wayland)");
        h.peers = {{Ids::Look, QStringLiteral("running 0.4.1")}, {Ids::Find, QStringLiteral("not installed")}};
        h.engines = {{QStringLiteral("FFmpeg"), QStringLiteral("7.1")}};
        QCOMPARE(h.render(), QStringLiteral("Arcade Box 0.4.0 (stable)\nOS: Ubuntu 24.04 (x86_64)\nDesktop: GNOME (wayland)\n"
                                            "Link: protocol 1, connections on\nPeers: Look running 0.4.1, Find not installed\nEngines: FFmpeg 7.1\n"));
        QTemporaryDir root;
        DiagnosticsHeader peers = DiagnosticsHeader::create(Ids::Shelf, QStringLiteral("0.4.0"), QStringLiteral("beta"));
        peers.peersFrom(Locations::under(root.path()));
        QCOMPARE(peers.peers.size(), 7);
        for (const auto &p : peers.peers) QCOMPARE(p.second, QStringLiteral("not installed"));
    }
};

QTEST_GUILESS_MAIN(PathsTest)
#include "tst_Paths.moc"
