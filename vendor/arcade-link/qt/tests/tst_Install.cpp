#include "ArcadeInstall.h"
#include <QJsonDocument>
#include <QSaveFile>
#include <QProcess>
#include <QStandardPaths>
#include <QtTest>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif
#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif
using namespace ArcadeLink;
static void put(const QString &path, const QByteArray &bytes) {
  QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  QCOMPARE(f.write(bytes), bytes.size());
}
static QByteArray get(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return {};
  return f.readAll();
}
static ArcadeInstall::AppInfo appInfo() {
  return {
      QStringLiteral("arcade.find"),          QStringLiteral("Arcade Find"),
      QStringLiteral("Arcade-Find.AppImage"), QStringLiteral("arcade-find"),
      QStringLiteral("arcade-find"),          {QStringLiteral("--background")},
      {QStringLiteral("old-find")},           QStringLiteral("arcade-find")};
}
static ArcadeInstall::InstallOptions options(const QString &source,
                                             const QString &version) {
  ArcadeInstall::InstallOptions opts;
  opts.app = appInfo();
  opts.runtime.executable = source;
  opts.version = version;
  opts.refreshCaches = false;
  return opts;
}
class InstallTest : public QObject {
  Q_OBJECT
private slots:
#ifdef Q_OS_UNIX
  void stderrGoesToARotatedLogOnlyWhenDiscarded() {
    QTemporaryDir dir;
    const int saved = dup(2);
    const auto pointStderrAt = [](const char *path) {
      const int fd = ::open(path, O_WRONLY | O_APPEND);
      dup2(fd, 2);
      ::close(fd);
    };
    pointStderrAt("/dev/null");
    const bool discarded = ArcadeLink::stderrGoesNowhere();
    const QString reader = dir.filePath("someone-reads.txt");
    QFile(reader).open(QIODevice::WriteOnly);
    pointStderrAt(QFile::encodeName(reader).constData());
    const bool read = ArcadeLink::stderrGoesNowhere();
    const QString untouched = ArcadeLink::captureStderr(dir.path(), "arcade-test");
    // An old, oversized log is kept once as .log.1.
    QFile big(dir.filePath("arcade-test.log"));
    QVERIFY(big.open(QIODevice::WriteOnly));
    big.write(QByteArray(1024 * 1024 + 1, 'x'));
    big.close();
    const QString log = ArcadeLink::captureStderrTo(dir.path(), "arcade-test");
    fputs("a Qt warning\n", stderr);
    fflush(stderr);
    dup2(saved, 2);
    ::close(saved);
    QVERIFY(discarded);
    QVERIFY(!read);
    QVERIFY(untouched.isEmpty());
    QCOMPARE(log, dir.filePath("arcade-test.log"));
    QFile written(log);
    QVERIFY(written.open(QIODevice::ReadOnly));
    const QByteArray text = written.readAll();
    QVERIFY(text.startsWith("--- arcade-test started (pid "));
    QVERIFY(text.endsWith("a Qt warning\n"));
    QCOMPARE(QFileInfo(dir.filePath("arcade-test.log.1")).size(), 1024 * 1024 + 1);
  }
#endif
  void sharedStringsAndWindowsIdentity() {
    QFile strings(QStringLiteral(ARCADE_LINK_INSTALL_STRINGS));
    QVERIFY(strings.open(QIODevice::ReadOnly));
    QCOMPARE(ArcadeInstall::strings(),
             QJsonDocument::fromJson(strings.readAll()).object());
    QCOMPARE(ArcadeInstall::formatString(
                 QStringLiteral("Install {app} {version}?"),
                 QStringLiteral("{version}"), QStringLiteral("3"), {}, {}),
             QStringLiteral("Install {version} 3?"));
    QFile vectors(
        QStringLiteral(ARCADE_LINK_VECTORS "/windows-path-identity.json"));
    QVERIFY(vectors.open(QIODevice::ReadOnly));
    for (const auto &value : QJsonDocument::fromJson(vectors.readAll())
                                 .object()
                                 .value(QStringLiteral("cases"))
                                 .toArray()) {
      const auto item = value.toObject();
      QCOMPARE(ArcadeInstall::Environment::under(
                   item.value(QStringLiteral("root")).toString())
                   .windowsEnvironmentKey,
               item.value(QStringLiteral("key")).toString());
    }
  }
  void receiptsAreAtomicPrivateAndPortable() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto loc = Locations::under(root.path());
    ReceiptStore store(loc);
    QFile vectors(QStringLiteral(ARCADE_LINK_VECTORS "/receipts.json"));
    QVERIFY(vectors.open(QIODevice::ReadOnly));
    auto receipt = QJsonDocument::fromJson(vectors.readAll())
                       .object()
                       .value(QStringLiteral("valid"))
                       .toArray()
                       .first()
                       .toObject()
                       .value(QStringLiteral("receipt"))
                       .toObject();
    QString error;
    QVERIFY2(store.write(receipt, &error), qPrintable(error));
    QJsonObject roundtrip;
    QVERIFY(
        store.read(receipt.value(QStringLiteral("id")).toString(), &roundtrip));
    QCOMPARE(roundtrip, receipt);
    auto invalid = receipt;
    invalid.insert(QStringLiteral("schema"), 2);
    QVERIFY(!store.write(invalid));
    QVERIFY(
        store.read(receipt.value(QStringLiteral("id")).toString(), &roundtrip));
    QCOMPARE(roundtrip, receipt);
    for (int i = 0; i < 12; ++i) {
      receipt.insert(QStringLiteral("version"), QString::number(i));
      QVERIFY(store.write(receipt));
    }
    QCOMPARE(QDir(store.directory()).entryList(QDir::Files).size(), 1);
#ifdef Q_OS_UNIX
    const auto permissions = QFile::permissions(
        store.path(receipt.value(QStringLiteral("id")).toString()));
    QVERIFY(
        !(permissions & (QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                         QFileDevice::ReadOther | QFileDevice::WriteOther)));
#endif
    QVERIFY(store.path(QStringLiteral("../escape")).isEmpty());
    QVERIFY(!ReceiptStore(Locations::under(QStringLiteral("relative-root")))
                 .write(receipt));
    QVERIFY(!ArcadeInstall::Environment::under(QStringLiteral("relative-root"))
                 .validate());
  }
  void environmentChild() {
    if (qEnvironmentVariable("ARCADE_INSTALL_CHILD") != u"1")
      QSKIP("invoked by isolatedEnvironment");
    ArcadeInstall::Environment e;
    QVERIFY(e.discover(&e));
    QCOMPARE(e.locations.registry, QDir(qEnvironmentVariable("ARCADE_HOME"))
                                       .filePath(QStringLiteral("apps")));
    QVERIFY(e.receipts().directory().startsWith(
        qEnvironmentVariable("ARCADE_HOME")));
    QVERIFY(e.validate());
  }
  void isolatedEnvironment() {
    QTemporaryDir root;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("ARCADE_HOME"), root.path());
    env.insert(QStringLiteral("ARCADE_INSTALL_CHILD"), QStringLiteral("1"));
    QProcess child;
    child.setProcessEnvironment(env);
    child.start(QCoreApplication::applicationFilePath(),
                {QStringLiteral("environmentChild"), QStringLiteral("-o"), QStringLiteral("-,txt")});
    QVERIFY(child.waitForFinished(10000));
    QVERIFY2(child.exitCode() == 0, child.readAllStandardOutput().constData());
  }
  void devDetectionAndSuppression() {
    QTemporaryDir root;
    const auto e = ArcadeInstall::Environment::under(root.path());
    ArcadeInstall installer(e);
    const auto source = root.filePath(QStringLiteral("download.AppImage"));
    put(source, "app");
    ArcadeInstall::Runtime runtime{
        root.filePath(QStringLiteral("mount/usr/bin/find")), source,
        root.filePath(QStringLiteral("mount")), false};
    ArcadeInstall::Detection d;
    QVERIFY(installer.detect(QStringLiteral("arcade.find"), runtime, &d));
    QVERIFY(d.isAppImage);
    QVERIFY(ArcadeInstall::shouldPrompt(d, {}));
    for (const auto &arg :
         {"--background", "--arcade-invoke", "--arcade-manifest", "--version",
          "--install", "--repair", "--integration-status"})
      QVERIFY(!ArcadeInstall::shouldPrompt(d, {QString::fromLatin1(arg)}));
    QVERIFY(!ArcadeInstall::shouldPrompt(d, {}, true));
    QVERIFY(!ArcadeInstall::shouldPrompt(d, {}, false, true));
    for (const auto &tree : {QStringLiteral("source/target/debug"),
                             QStringLiteral("cmake/build/bin")}) {
      const auto exe = root.filePath(tree + QStringLiteral("/app"));
      put(exe, "binary");
      put(root.filePath(
              tree.section(QLatin1Char('/'), 0, 0) + QStringLiteral("/") +
              (tree.startsWith(u"source") ? QStringLiteral("Cargo.toml")
                                          : QStringLiteral("CMakeLists.txt"))),
          "source");
      QVERIFY(ArcadeInstall::isDevBuild(exe));
    }
    runtime.devBuild = true;
    QVERIFY(installer.detect(QStringLiteral("arcade.find"), runtime, &d));
    QCOMPARE(d.state, QStringLiteral("devBuild"));
    QVERIFY(!ArcadeInstall::shouldPrompt(d, {}));
    QVERIFY(
        !QFileInfo::exists(e.receipts().path(QStringLiteral("arcade.find"))));
    QVERIFY(ArcadeInstall::isTranslocated(
        QStringLiteral("/private/var/AppTranslocation/a/Find.app")));
  }
#ifdef Q_OS_LINUX
  void installUpdateRepairUninstall() {
    QTemporaryDir root;
    auto e = ArcadeInstall::Environment::under(root.path());
    e.applications =
        root.filePath(QStringLiteral("Applications with spaces/Arcade"));
    ArcadeInstall installer(e);
    const auto source = root.filePath(QStringLiteral("download.AppImage"));
    put(source, "v1");
    auto opts = options(source, QStringLiteral("1"));
    opts.startAtLogin = true;
    const auto png = root.filePath(QStringLiteral("icon.png"));
    put(png, "fake png");
    opts.icons = {{png, 256}};
    const auto stale =
        QDir(e.data).filePath(QStringLiteral("applications/old-find.desktop"));
    put(stale, "[Desktop Entry]\nType=Application\nExec=/old/arcade-find\n");
    const auto same =
        QDir(e.data).filePath(QStringLiteral("applications/same.desktop"));
    const auto dest = QDir(e.applications).filePath(opts.app.filename);
    put(same, QStringLiteral("[Desktop "
                             "Entry]\nType=Application\nX-Arcade-Id=arcade."
                             "find\nExec=\"%1\" %U\n")
                  .arg(QString(dest).replace(QStringLiteral(" "),
                                             QStringLiteral("\\s")))
                  .toUtf8());
    QJsonObject receipt;
    QString error;
    QVERIFY2(installer.install(opts, &receipt, &error), qPrintable(error));
    QCOMPARE(get(dest), QByteArray("v1"));
    QVERIFY(!QFileInfo::exists(stale));
    QVERIFY(QFileInfo::exists(same));
    const auto integration =
        receipt.value(QStringLiteral("integration")).toObject();
    QVERIFY(QFileInfo::exists(
        integration.value(QStringLiteral("autostart")).toString()));
    QCOMPARE(QFileInfo(integration.value(QStringLiteral("cli")).toString())
                 .symLinkTarget(),
             dest);
    put(source, "v2");
    opts.version = QStringLiteral("2");
    QVERIFY(installer.install(opts, &receipt, &error));
    const auto previous = receipt.value(QStringLiteral("previous"))
                              .toObject()
                              .value(QStringLiteral("path"))
                              .toString();
    QCOMPARE(get(previous), QByteArray("v1"));
    const auto moved =
        QDir(e.applications).filePath(QStringLiteral("Moved % $.AppImage"));
    QVERIFY(QFile::rename(dest, moved));
    QVERIFY(QFile::remove(
        integration.value(QStringLiteral("desktopEntry")).toString()));
    QVERIFY2(installer.repair(opts.app, moved, &receipt, &error),
             qPrintable(error));
    QCOMPARE(QFileInfo(integration.value(QStringLiteral("cli")).toString())
                 .symLinkTarget(),
             moved);
    auto status =
        installer.integrationStatus(opts.app.id, QStringLiteral("fish"));
    QVERIFY(!status.value(QStringLiteral("binOnPath")).toBool());
    QVERIFY(status.value(QStringLiteral("pathHint"))
                .toString()
                .startsWith(u"fish_add_path "));
    const auto data = QDir(e.data).filePath(QStringLiteral("find/data"));
    put(data + QStringLiteral("/settings"), "settings");
    bool removed = false;
    QVERIFY(installer.uninstall(
        opts.app.id, {true, {QDir(e.data).filePath(QStringLiteral("find"))}},
        &removed, &error));
    QVERIFY(removed);
    QVERIFY(!QFileInfo::exists(moved));
    QVERIFY(!QFileInfo::exists(previous));
    QVERIFY(!QFileInfo::exists(data));
    QVERIFY(!QFileInfo::exists(same)); // Repair removed the stale old path.
  }
  void repairReplacesFinalSymlinkWithoutWritingThrough() {
    QTemporaryDir root;
    const auto e = ArcadeInstall::Environment::under(root.path());
    ArcadeInstall installer(e);
    const auto source = root.filePath(QStringLiteral("download.AppImage"));
    put(source, "v1");
    auto opts = options(source, QStringLiteral("1"));
    opts.startAtLogin = true;
    QJsonObject receipt;
    QString error;
    QVERIFY2(installer.install(opts, &receipt, &error), qPrintable(error));
    const auto integration = receipt.value(QStringLiteral("integration")).toObject();
    for (const auto &key : {QStringLiteral("desktopEntry"), QStringLiteral("autostart")}) {
      const auto entry = integration.value(key).toString();
      const auto outside = root.filePath(key + QStringLiteral("-outside.desktop"));
      QVERIFY(QFile::rename(entry, outside));
      const auto before = get(outside);
      QVERIFY(QFile::link(outside, entry));
      QVERIFY2(installer.repair(opts.app, {}, &receipt, &error), qPrintable(error));
      QCOMPARE(get(outside), before);
      QVERIFY(!QFileInfo(entry).isSymLink());
      QVERIFY(get(entry).contains("X-Arcade-Id=arcade.find"));
    }
  }
  void staleReceiptCannotRewriteOrDeleteForeignIntegration() {
    for (const auto &marker : {QByteArray("X-Arcade-Id=arcade.look\n"), QByteArray()}) {
      QTemporaryDir root;
      const auto e = ArcadeInstall::Environment::under(root.path());
      ArcadeInstall installer(e);
      const auto source = root.filePath(QStringLiteral("download.AppImage"));
      put(source, "v1");
      auto opts = options(source, QStringLiteral("1"));
      opts.startAtLogin = true;
      QJsonObject receipt;
      QString error;
      QVERIFY2(installer.install(opts, &receipt, &error), qPrintable(error));
      const auto integration = receipt.value(QStringLiteral("integration")).toObject();
      const QByteArray foreign = "[Desktop Entry]\nType=Application\n" + marker + "Exec=/foreign/program\n";
      for (const auto &key : {QStringLiteral("desktopEntry"), QStringLiteral("autostart")})
        put(integration.value(key).toString(), foreign);
      const auto cli = integration.value(QStringLiteral("cli")).toString();
      QVERIFY(QFile::remove(cli));
      QVERIFY(QFile::link(QStringLiteral("/foreign/program"), cli));
      const auto original = get(e.receipts().path(opts.app.id));
      QVERIFY(!installer.uninstall(opts.app.id, {}, nullptr, &error));
      QVERIFY(error.contains(QStringLiteral("ownership conflict")));
      QCOMPARE(get(e.receipts().path(opts.app.id)), original);
      QVERIFY(QFileInfo::exists(receipt.value(QStringLiteral("path")).toString()));
      QCOMPARE(installer.integrationStatus(opts.app.id, QStringLiteral("bash"))
                   .value(QStringLiteral("skipped")).toArray().size(), 3);
      QVERIFY2(installer.repair(opts.app, {}, &receipt, &error), qPrintable(error));
      const auto repaired = receipt.value(QStringLiteral("integration")).toObject();
      for (const auto &key : {QStringLiteral("desktopEntry"), QStringLiteral("autostart"), QStringLiteral("cli")})
        QVERIFY(repaired.value(key).isNull());
      QCOMPARE(repaired.value(QStringLiteral("skipped")).toArray().size(), 3);
      QVERIFY2(installer.uninstall(opts.app.id, {}, nullptr, &error), qPrintable(error));
      for (const auto &key : {QStringLiteral("desktopEntry"), QStringLiteral("autostart")})
        QCOMPARE(get(integration.value(key).toString()), foreign);
      QCOMPARE(QFileInfo(cli).symLinkTarget(), QStringLiteral("/foreign/program"));
    }
  }
  void repairRefusesTraversalBeforeChangingEntriesOrReceipt() {
    QTemporaryDir root;
    const auto e = ArcadeInstall::Environment::under(root.path());
    ArcadeInstall installer(e);
    const auto source = root.filePath(QStringLiteral("download.AppImage"));
    put(source, "v1");
    auto opts = options(source, QStringLiteral("1"));
    QJsonObject receipt;
    QString error;
    QVERIFY2(installer.install(opts, &receipt, &error), qPrintable(error));
    const auto integration = receipt.value(QStringLiteral("integration")).toObject();
    const auto launcher = integration.value(QStringLiteral("desktopEntry")).toString();
    const auto before = get(launcher);
    const auto receiptBefore = get(e.receipts().path(opts.app.id));
    const auto moved = QDir(e.applications).filePath(QStringLiteral("Moved-Find.AppImage"));
    put(moved, "moved copy");
    const auto traversal = QDir(e.applications).filePath(QStringLiteral("../Arcade/Moved-Find.AppImage"));
    QVERIFY(QFileInfo(traversal).isFile());
    QVERIFY(!installer.repair(opts.app, traversal, &receipt, &error));
    QVERIFY(error.contains(QStringLiteral("parent traversal")));
    QCOMPARE(get(launcher), before);
    QCOMPARE(get(e.receipts().path(opts.app.id)), receiptBefore);
    QCOMPARE(QFileInfo(integration.value(QStringLiteral("cli")).toString()).symLinkTarget(),
             receipt.value(QStringLiteral("path")).toString());
    QVERIFY2(installer.repair(opts.app, moved, &receipt, &error), qPrintable(error));
    QVERIFY(validateNativeReceiptPaths(receipt));
    QVERIFY(installer.uninstall(opts.app.id, {}, nullptr, &error));
    QVERIFY(!QFileInfo::exists(moved));
  }
  void foreignIntegrationAndPreviousSurvive() {
    QTemporaryDir root;
    const auto e = ArcadeInstall::Environment::under(root.path());
    ArcadeInstall installer(e);
    const auto source = root.filePath(QStringLiteral("download.AppImage"));
    put(source, "v1");
    auto opts = options(source, QStringLiteral("1"));
    opts.startAtLogin = true;
    const auto desktop = QDir(e.data).filePath(
        QStringLiteral("applications/arcade-find.desktop"));
    const auto icon = QDir(e.data).filePath(
        QStringLiteral("icons/hicolor/256x256/apps/arcade-find.png"));
    const auto autoPath = QDir(e.config).filePath(
        QStringLiteral("autostart/arcade-find.desktop"));
    const auto cli = QDir(e.bin).filePath(QStringLiteral("arcade-find"));
    const auto foreign = root.filePath(QStringLiteral("foreign"));
    put(foreign, "foreign program");
    put(desktop, "[Desktop Entry]\nType=Application\nExec=/foreign/program\n");
    put(icon, "foreign icon");
    put(autoPath, "[Desktop Entry]\nType=Application\nExec=/foreign/program\n");
    QVERIFY(QDir().mkpath(e.bin));
    QVERIFY(QFile::link(foreign, cli));
    const auto png = root.filePath(QStringLiteral("icon.png"));
    put(png, "app icon");
    opts.icons = {{png, 256}};
    QJsonObject receipt;
    QString error;
    QVERIFY2(installer.install(opts, &receipt, &error), qPrintable(error));
    auto integration = receipt.value(QStringLiteral("integration")).toObject();
    QVERIFY(integration.value(QStringLiteral("desktopEntry")).isNull());
    QVERIFY(integration.value(QStringLiteral("cli")).isNull());
    QVERIFY(integration.value(QStringLiteral("autostart")).isNull());
    QVERIFY(integration.value(QStringLiteral("icons")).toArray().isEmpty());
    QCOMPARE(integration.value(QStringLiteral("skipped")).toArray().size(), 4);
    QCOMPARE(installer.integrationStatus(opts.app.id, QStringLiteral("bash"))
                 .value(QStringLiteral("skipped")),
             integration.value(QStringLiteral("skipped")));
    const auto previous =
        QDir(e.applications)
            .filePath(QStringLiteral(".previous/") + opts.app.filename);
    put(previous, "foreign backup");
    put(source, "v2");
    opts.version = QStringLiteral("2");
    QVERIFY(installer.install(opts, &receipt, &error));
    QVERIFY(receipt.value(QStringLiteral("previous"))
                .toObject()
                .value(QStringLiteral("path"))
                .toString() != previous);
    QVERIFY(installer.repair(opts.app, {}, &receipt, &error));
    QVERIFY(installer.uninstall(opts.app.id, {}, nullptr, &error));
    QCOMPARE(get(icon), QByteArray("foreign icon"));
    QCOMPARE(get(previous), QByteArray("foreign backup"));
    QCOMPARE(QFileInfo(cli).symLinkTarget(), foreign);
    QVERIFY(get(desktop).contains("/foreign/program"));
    QVERIFY(get(autoPath).contains("/foreign/program"));
  }
  void rollbackRestoresSymlinkAndPortablePathsCannotMutate() {
    QTemporaryDir root;
    const auto e = ArcadeInstall::Environment::under(root.path());
    ArcadeInstall installer(e);
    const auto source = root.filePath(QStringLiteral("download.AppImage"));
    put(source, "v1");
    auto opts = options(source, QStringLiteral("1"));
    QJsonObject receipt;
    QString error;
    QVERIFY(installer.install(opts, &receipt, &error));
    auto integration = receipt.value(QStringLiteral("integration")).toObject();
    const auto desktop =
        integration.value(QStringLiteral("desktopEntry")).toString();
    const auto target = root.filePath(QStringLiteral("saved.desktop"));
    QVERIFY(QFile::rename(desktop, target));
    QVERIFY(QFile::link(target, desktop));
    const auto autoPath = QDir(e.config).filePath(
        QStringLiteral("autostart/arcade-find.desktop"));
    put(autoPath, "[Desktop Entry]\nX-Arcade-Id=arcade.find\nHidden=true\n");
    integration.insert(QStringLiteral("autostart"), autoPath);
    receipt.insert(QStringLiteral("integration"), integration);
    QVERIFY(e.receipts().write(receipt));
    put(source, "v2");
    opts.version = QStringLiteral("2");
    QVERIFY(!installer.install(opts, nullptr, &error));
    QVERIFY(QFileInfo(desktop).isSymLink());
    QCOMPARE(QFileInfo(desktop).symLinkTarget(), target);
    QCOMPARE(get(receipt.value(QStringLiteral("path")).toString()),
             QByteArray("v1"));
    QJsonObject original;
    QVERIFY(e.receipts().read(opts.app.id, &original));
    QCOMPARE(original, receipt);
    receipt.insert(QStringLiteral("path"), QStringLiteral("C:\\Victim.exe"));
    QVERIFY(validateReceipt(receipt));
    QVERIFY(!validateNativeReceiptPaths(receipt));
    QVERIFY(e.receipts().write(receipt));
    QVERIFY(!installer.uninstall(opts.app.id, {}));
    QVERIFY(!installer.repair(opts.app, {}, nullptr));
    QVERIFY(!installer.install(opts, nullptr));
    QVERIFY(QFileInfo::exists(target));
  }
#endif
#ifdef Q_OS_WIN
  void nativeShimsUseAnIsolatedRegistryKey() {
    QTemporaryDir root;
    const auto e = ArcadeInstall::Environment::under(root.path());
    ArcadeInstall installer(e);
    QVERIFY(e.windowsEnvironmentKey != u"Environment");
    struct Cleanup {
      QString key;
      ~Cleanup() {
        RegDeleteTreeW(HKEY_CURRENT_USER,
                       reinterpret_cast<LPCWSTR>(key.utf16()));
      }
    } cleanup{e.windowsEnvironmentKey};
    QString first, error;
    const auto exe = root.filePath(QStringLiteral("app.exe"));
    put(exe, "fixture");
    QVERIFY2(installer.writeWindowsShim(QStringLiteral("arcade-first"), exe,
                                        &first, &error),
             qPrintable(error));
    QVERIFY2(installer.writeWindowsShim(QStringLiteral("arcade-first"), exe,
                                        nullptr, &error), qPrintable(error));
    QVERIFY2(installer.writeWindowsShim(QStringLiteral("arcade-second"), exe,
                                        nullptr, &error), qPrintable(error));
    QVERIFY(get(first).contains("%*"));
    const auto readPath = [&] {
      HKEY key = nullptr;
      QString value;
      if (RegOpenKeyExW(
              HKEY_CURRENT_USER,
              reinterpret_cast<LPCWSTR>(e.windowsEnvironmentKey.utf16()), 0,
              KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        wchar_t bytes[8192]{};
        DWORD size = sizeof(bytes);
        DWORD type = REG_NONE;
        const auto result = RegQueryValueExW(key, L"Path", nullptr, &type,
                                             reinterpret_cast<BYTE *>(bytes), &size);
        if (result != ERROR_SUCCESS || type != REG_EXPAND_SZ) {
          RegCloseKey(key);
          QTest::qFail("isolated Path must retain REG_EXPAND_SZ", __FILE__, __LINE__);
          return QString();
        }
        value = QString::fromWCharArray(bytes);
        RegCloseKey(key);
      }
      return value;
    };
    const auto path = readPath();
    QCOMPARE(path.split(QLatin1Char(';'), Qt::SkipEmptyParts).size(), 1);
    QVERIFY(installer.removeWindowsShim(QStringLiteral("arcade-first")));
    QCOMPARE(readPath(), path);
    QVERIFY(installer.removeWindowsShim(QStringLiteral("arcade-second")));
    QVERIFY(readPath().isEmpty());
  }
#endif
#ifdef Q_OS_MACOS
  void bundleMoveRetainsQuarantineAndOffersCli() {
    QTemporaryDir root;
    const auto e = ArcadeInstall::Environment::under(root.path());
    ArcadeInstall installer(e);
    const auto bundle = root.filePath(QStringLiteral("download/Find.app"));
    put(bundle + QStringLiteral("/Contents/MacOS/find"), "fixture");
    ArcadeInstall::Runtime runtime;
    runtime.executable = bundle + QStringLiteral("/Contents/MacOS/find");
    ArcadeInstall::Detection detection;
    QVERIFY(installer.detect(QStringLiteral("arcade.find"), runtime, &detection));
    QCOMPARE(detection.runningFrom, bundle);
    QVERIFY(!detection.isAppImage);
    QVERIFY(ArcadeInstall::shouldPrompt(detection, {}));
    for (const auto &arg : {"--background", "--arcade-invoke", "--arcade-manifest", "--version"})
      QVERIFY(!ArcadeInstall::shouldPrompt(detection, {QString::fromLatin1(arg)}));
    QVERIFY(!ArcadeInstall::shouldPrompt(detection, {}, true));
    QVERIFY(!ArcadeInstall::shouldPrompt(detection, {}, false, true));
    // A real quarantine xattr is kept by ditto; never remove it.
    QCOMPARE(QProcess::execute(
                 QStringLiteral("/usr/bin/xattr"),
                 {QStringLiteral("-w"), QStringLiteral("com.apple.quarantine"),
                  QStringLiteral("0081;00000000;ArcadeLinkTest;"), bundle}),
             0);
    QString installed, error;
    QVERIFY2(installer.moveToApplications(bundle, {}, &installed, &error),
             qPrintable(error));
    QVERIFY(installer.inApplications(installed));
    QProcess attr;
    attr.start(QStringLiteral("/usr/bin/xattr"),
               {QStringLiteral("-p"), QStringLiteral("com.apple.quarantine"),
                installed});
    QVERIFY(attr.waitForFinished());
    QCOMPARE(attr.exitCode(), 0);
    QVERIFY(attr.readAllStandardOutput().contains("ArcadeLinkTest"));
    QString cli;
    QVERIFY(installer.installCli(
        QStringLiteral("arcade-find"),
        installed + QStringLiteral("/Contents/MacOS/find"), &cli));
    QVERIFY(QFileInfo(cli).isSymLink());
    QVERIFY(!installer.moveToApplications(bundle, {}, nullptr));
  }
#endif

#ifdef Q_OS_LINUX
  void danglingDestinationSymlinkIsNotReplaced() {
    QTemporaryDir root;
    auto e = ArcadeInstall::Environment::under(root.path());
    ArcadeInstall installer(e);
    const auto source = root.filePath(QStringLiteral("download.AppImage"));
    put(source, "v1");
    QVERIFY(QDir().mkpath(e.applications));
    const auto dest = QDir(e.applications).filePath(appInfo().filename);
    QVERIFY(QFile::link(root.filePath(QStringLiteral("missing")), dest));
    QJsonObject receipt;
    QString error;
    QVERIFY(!installer.install(options(source, QStringLiteral("1")), &receipt, &error));
    QVERIFY(QFileInfo(dest).isSymLink());
    QVERIFY(!QFileInfo::exists(e.receipts().path(appInfo().id)));
  }
  void launchersEscapeNewlinesAndRoutePercentThroughEnv() {
    for (const auto &home : {QStringLiteral("home\nnewline"), QStringLiteral("home % percent")}) {
      QTemporaryDir root;
      auto e = ArcadeInstall::Environment::under(root.filePath(home));
      ArcadeInstall installer(e);
      const auto source = QDir(e.home).filePath(QStringLiteral("download.AppImage"));
      put(source, "v1");
      QJsonObject receipt;
      QString error;
      QVERIFY2(installer.install(options(source, QStringLiteral("1")), &receipt, &error), qPrintable(error));
      const auto entry = receipt.value(QStringLiteral("integration")).toObject().value(QStringLiteral("desktopEntry")).toString();
      const auto text = QString::fromUtf8(get(entry));
      for (const auto &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        QVERIFY2(line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('[')) || line.contains(QLatin1Char('=')), qPrintable(text));
      QCOMPARE(text.contains(QStringLiteral("\nExec=env \"")), home.contains(QLatin1Char('%')));
      if (!QStandardPaths::findExecutable(QStringLiteral("desktop-file-validate")).isEmpty()) {
        QProcess validate;
        validate.start(QStringLiteral("desktop-file-validate"), {entry});
        QVERIFY(validate.waitForFinished());
        QVERIFY2(!validate.readAllStandardOutput().contains("error:"), qPrintable(text));
      }
      QVERIFY2(installer.install(options(source, QStringLiteral("2")), &receipt, &error), qPrintable(error));
      QVERIFY(!receipt.value(QStringLiteral("integration")).toObject().contains(QStringLiteral("skipped")));
    }
  }
  void preInstallerLauncherAndLoginEntryAreTakenOver() {
    QTemporaryDir root;
    auto e = ArcadeInstall::Environment::under(root.path());
    ArcadeInstall installer(e);
    const auto legacy = root.filePath(QStringLiteral("Downloads/Arcade-Find-0.2.1-x86_64.AppImage"));
    put(legacy, "old");
    const auto launcher = QDir(e.data).filePath(QStringLiteral("applications/arcade-find.desktop"));
    const auto login = QDir(e.config).filePath(QStringLiteral("autostart/arcade-find.desktop"));
    for (const auto &path : {launcher, login})
      put(path, QStringLiteral("[Desktop Entry]\nType=Application\nName=Arcade Find\nExec=\"%1\" --background\n").arg(legacy).toUtf8());
    QCOMPARE(installer.legacyCopies(appInfo()), QStringList{legacy});
    const auto source = root.filePath(QStringLiteral("Downloads/Arcade-Find-x86_64.AppImage"));
    put(source, "new");
    auto opts = options(source, QStringLiteral("0.3.1"));
    opts.removeDownload = true;
    QJsonObject receipt;
    QString error;
    QVERIFY2(installer.install(opts, &receipt, &error), qPrintable(error));
    const auto integration = receipt.value(QStringLiteral("integration")).toObject();
    QVERIFY(!integration.contains(QStringLiteral("skipped")));
    QCOMPARE(integration.value(QStringLiteral("autostart")).toString(), login);
    for (const auto &path : {launcher, login}) {
      const auto text = QString::fromUtf8(get(path));
      QVERIFY2(text.contains(receipt.value(QStringLiteral("path")).toString()) && !text.contains(u"0.2.1"), qPrintable(text));
    }
    QVERIFY(!QFileInfo::exists(source));
    QVERIFY(!QFileInfo::exists(legacy));
    QVERIFY(installer.legacyCopies(appInfo()).isEmpty());
  }
  void onlyDownloadsAreRemovedAndStaleStagesCleaned() {
    QTemporaryDir root;
    auto e = ArcadeInstall::Environment::under(root.path());
    ArcadeInstall installer(e);
    const auto stale = QDir(e.applications).filePath(QStringLiteral("Arcade-Find.AppImage.0123456789abcdef0123456789abcdef.stage"));
    put(stale, "half");
    const auto kept = root.filePath(QStringLiteral("Apps/Arcade-Find-x86_64.AppImage"));
    put(kept, "v1");
    auto opts = options(kept, QStringLiteral("1"));
    opts.removeDownload = true;
    QJsonObject receipt;
    QString error;
    QVERIFY2(installer.install(opts, &receipt, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(kept));
    QVERIFY(!QFileInfo::exists(stale));
  }
  void installErrorsNameTheFolder() {
    QTemporaryDir root;
    auto e = ArcadeInstall::Environment::under(root.path());
    put(e.applications, "a file where the folder goes");
    ArcadeInstall installer(e);
    const auto source = root.filePath(QStringLiteral("download.AppImage"));
    put(source, "v1");
    QJsonObject receipt;
    QString error;
    QVERIFY(!installer.install(options(source, QStringLiteral("1")), &receipt, &error));
    QVERIFY2(error.contains(e.applications), qPrintable(error));
  }
  void explicitStartAtLoginReEnablesASwitchedOffEntry() {
    QTemporaryDir root;
    auto e = ArcadeInstall::Environment::under(root.path());
    ArcadeInstall installer(e);
    const auto source = root.filePath(QStringLiteral("download.AppImage"));
    put(source, "v1");
    const auto login = QDir(e.config).filePath(QStringLiteral("autostart/arcade-find.desktop"));
    put(login, "[Desktop Entry]\nType=Application\nX-Arcade-Id=arcade.find\nExec=/old/find --background\nHidden=true\n");
    QJsonObject receipt;
    QString error;
    QVERIFY2(installer.install(options(source, QStringLiteral("1")), &receipt, &error), qPrintable(error));
    QVERIFY(!ArcadeInstall::autostartEnabled(login));
    auto opts = options(source, QStringLiteral("1"));
    opts.startAtLogin = true;
    QVERIFY2(installer.install(opts, &receipt, &error), qPrintable(error));
    QVERIFY2(ArcadeInstall::autostartEnabled(login), get(login).constData());
  }
#endif
  void childEnvironmentDropsTheAppImageMount() {
    QProcessEnvironment env;
    env.insert(QStringLiteral("APPDIR"), QStringLiteral("/tmp/.mount_ArcadeAB"));
    env.insert(QStringLiteral("APPIMAGE"), QStringLiteral("/home/u/X.AppImage"));
    env.insert(QStringLiteral("GTK_DATA_PREFIX"), QStringLiteral("/tmp/.mount_ArcadeAB"));
    env.insert(QStringLiteral("GTK_THEME"), QStringLiteral("Adwaita:light"));
    env.insert(QStringLiteral("PATH"), QStringLiteral("/tmp/.mount_ArcadeAB/usr/bin/:/usr/bin:/bin:/usr/bin"));
    env.insert(QStringLiteral("LD_LIBRARY_PATH"), QStringLiteral("/tmp/.mount_ArcadeAB/usr/lib/"));
    env.insert(QStringLiteral("XDG_DATA_DIRS"), QStringLiteral("/tmp/.mount_ArcadeAB/usr/share/:/usr/share:"));
    env.insert(QStringLiteral("HOME"), QStringLiteral("/home/u"));
    const auto clean = ArcadeInstall::childEnvironment(env);
    QCOMPARE(clean.keys(), (QStringList{QStringLiteral("HOME"), QStringLiteral("PATH"), QStringLiteral("XDG_DATA_DIRS")}));
    QCOMPARE(clean.value(QStringLiteral("PATH")), QStringLiteral("/usr/bin:/bin"));
    QCOMPARE(clean.value(QStringLiteral("XDG_DATA_DIRS")), QStringLiteral("/usr/share"));
    QProcessEnvironment plain;
    plain.insert(QStringLiteral("GTK_THEME"), QStringLiteral("Adwaita:dark"));
    QCOMPARE(ArcadeInstall::childEnvironment(plain).value(QStringLiteral("GTK_THEME")), QStringLiteral("Adwaita:dark"));
  }
};
QTEST_GUILESS_MAIN(InstallTest)
#include "tst_Install.moc"
