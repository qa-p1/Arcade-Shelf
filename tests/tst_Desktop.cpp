#include "platform/Desktop.h"
#include "ArcadeInstall.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMimeData>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QUrl>
#include <QWindow>
#include <QtTest>
#include <memory>

using Shelf::Desktop;

class DesktopTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qputenv("ARCADE_SHELF_TESTING", "1");
    }

    void nativeUriListPreservesPathsAndInputOrder()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString first = directory.filePath(QStringLiteral("résumé #1.png"));
        const QString second = directory.filePath(QStringLiteral("two.txt"));
        for (const QString &path : {first, second}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("original"), qint64(8));
        }
        const QJsonArray inputs{
            QJsonObject{{"type", "file/any[]"}, {"paths", QJsonArray{first, second}}},
            QJsonObject{{"type", "folder/reference"}, {"path", directory.path()}},
            QJsonObject{{"type", "file/image"}, {"path", first}}};
        std::unique_ptr<QMimeData> mime(Desktop::mimeDataForContents(inputs));
        QVERIFY(mime->hasFormat(QStringLiteral("text/uri-list")));
        QCOMPARE(mime->urls(), QList<QUrl>({QUrl::fromLocalFile(first), QUrl::fromLocalFile(second),
                                         QUrl::fromLocalFile(directory.path())}));
        QVERIFY(mime->data(QStringLiteral("text/uri-list")).contains("%23"));
        QCOMPARE(mime->text(), QStringList({first, second, directory.path()}).join(QLatin1Char('\n')));
        QFile original(first);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("original"));
    }

    void richTextAndUrlsUseNativeFormats()
    {
        std::unique_ptr<QMimeData> mime(Desktop::mimeDataForContents({
            QJsonObject{{"type", "text/rich"}, {"text", "Hello"}, {"html", "<b>Hello</b>"}},
            QJsonObject{{"type", "text/url"}, {"text", "https://example.com/a?q=one"}},
            QJsonObject{{"type", "text/plain"}, {"text", "last"}}}));
        QCOMPARE(mime->text(), QStringLiteral("Hello\nhttps://example.com/a?q=one\nlast"));
        QCOMPARE(mime->html(), QStringLiteral("<b>Hello</b>"));
        QCOMPARE(mime->urls(), QList<QUrl>({QUrl(QStringLiteral("https://example.com/a?q=one"))}));
    }

    void reorderMarkerCoexistsWithNativeExternalTransfer()
    {
        QTemporaryDir directory;
        const QJsonObject marker{{"profile", "profile-hash"}, {"ids", QJsonArray{"it_a", "it_b"}}};
        std::unique_ptr<QMimeData> mime(Desktop::mimeDataForContents({
            QJsonObject{{"type", "folder/reference"}, {"path", directory.path()}},
            QJsonObject{{"type", "structured/shelf-reorder"}, {"data", marker}}}));
        QCOMPARE(mime->urls(), QList<QUrl>({QUrl::fromLocalFile(directory.path())}));
        QCOMPARE(QJsonDocument::fromJson(mime->data(QStringLiteral("application/x-arcade-shelf-items"))).object(), marker);
        std::unique_ptr<QMimeData> invalid(Desktop::mimeDataForContents({
            QJsonObject{{"type", "structured/shelf-reorder"}, {"data", QJsonObject{{"profile", "hash"}, {"ids", QJsonArray{123}}}}}}));
        QVERIFY(invalid->formats().isEmpty());
    }

    void symlinkDragPreservesLinkPath()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir directory;
        QFile file(directory.filePath(QStringLiteral("target.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        const QString symlink = directory.filePath(QStringLiteral("reference.txt"));
        QVERIFY(QFile::link(file.fileName(), symlink));
        std::unique_ptr<QMimeData> mime(Desktop::mimeDataForContents({
            QJsonObject{{"type", "file/text"}, {"path", symlink}}}));
        QCOMPARE(mime->urls(), QList<QUrl>({QUrl::fromLocalFile(symlink)}));
#else
        QSKIP("QFile::link creates shortcuts, not POSIX symlinks, on Windows");
#endif
    }

    void invalidAndMissingReferencesAreExcluded()
    {
        QTemporaryDir directory;
        const QString embeddedNul = directory.path() + QChar::Null + QStringLiteral("suffix");
        std::unique_ptr<QMimeData> mime(Desktop::mimeDataForContents({
            QJsonObject{{"type", "file/any"}, {"path", "relative.txt"}},
            QJsonObject{{"type", "file/any"}, {"path", directory.filePath("missing.txt")}},
            QJsonObject{{"type", "file/any"}, {"path", embeddedNul}},
            QJsonObject{{"type", "structured/arcade-action"}, {"text", "execute-me"}}}));
        QVERIFY(mime->formats().isEmpty());
    }

    void stagedTextFileRemainsTransferable()
    {
        QTemporaryDir directory;
        QFile file(directory.filePath(QStringLiteral("note.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("A small staged note"), qint64(19));
        file.close();
        std::unique_ptr<QMimeData> text(Desktop::mimeDataForContents({
            QJsonObject{{"type", "text/plain"}, {"path", file.fileName()}}}));
        QCOMPARE(text->text(), QStringLiteral("A small staged note"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(QByteArray(300 * 1024, 'x')), qint64(300 * 1024));
        file.close();
        std::unique_ptr<QMimeData> large(Desktop::mimeDataForContents({
            QJsonObject{{"type", "text/plain"}, {"path", file.fileName()}}}));
        QCOMPARE(large->urls(), QList<QUrl>({QUrl::fromLocalFile(file.fileName())}));
        QCOMPARE(QFileInfo(file).size(), qint64(300 * 1024));
    }

    void geometryClampsAcrossNegativeMonitorCoordinates()
    {
        const QRect leftMonitor(-1920, -200, 1920, 1080);
        QCOMPARE(Desktop::clampGeometry(QRect(-2300, -500, 480, 400), leftMonitor),
                 QRect(-1920, -200, 480, 400));
        QCOMPARE(Desktop::clampGeometry(QRect(-100, 800, 480, 400), leftMonitor),
                 QRect(-480, 480, 480, 400));
        QCOMPARE(Desktop::clampGeometry(QRect(-3000, -3000, 6000, 6000), leftMonitor), leftMonitor);
        QCOMPARE(Desktop::clampGeometry(QRect(40, 40, 10, 10), QRect(0, 0, 100, 50)), QRect(0, 0, 100, 50));
    }

    void edgeThresholdUsesLogicalCoordinatesOnEachMonitor()
    {
        const QRect leftMonitor(-1920, -200, 1920, 1080);
        QCOMPARE(Desktop::nearestEdge(QRect(-1900, 0, 320, 160), leftMonitor, 20), QStringLiteral("left"));
        QVERIFY(Desktop::nearestEdge(QRect(-1899, 0, 320, 160), leftMonitor, 20).isEmpty());
        QCOMPARE(Desktop::nearestEdge(QRect(-340, 0, 320, 160), leftMonitor, 20), QStringLiteral("right"));
        QCOMPARE(Desktop::nearestEdge(QRect(-800, -182, 320, 160), leftMonitor), QStringLiteral("top"));
        QCOMPARE(Desktop::nearestEdge(QRect(-800, 701, 320, 160), leftMonitor), QStringLiteral("bottom"));
        QCOMPARE(Desktop::nearestEdge(QRect(-1920, -200, 320, 160), leftMonitor), QStringLiteral("left"));
        QVERIFY(Desktop::nearestEdge(QRect(-1920, 5000, 320, 160), leftMonitor).isEmpty());
        QVERIFY(Desktop::nearestEdge(QRect(200, 100, 320, 160), leftMonitor).isEmpty());
        const QRect scaledMonitor(0, 0, 1280, 720); // Qt geometry is logical, independent of physical DPI.
        QCOMPARE(Desktop::nearestEdge(QRect(945, 150, 320, 160), scaledMonitor), QStringLiteral("right"));
        QVERIFY(Desktop::nearestEdge(QRect(500, 300, 320, 160), scaledMonitor).isEmpty());
        QVERIFY(Desktop::nearestEdge(QRect(), scaledMonitor).isEmpty());
        QVERIFY(Desktop::nearestEdge(QRect(0, 0, 320, 160), scaledMonitor, -1).isEmpty());
    }

    void arrangementOpensInwardAndPlacesFirstRunAtTheRightEdge()
    {
        const QRect usable(60, 10, 2490, 1420); // A left bar and 10 px frames.
        // First run: near the right edge, vertically centred.
        QCOMPARE(Desktop::arrangedGeometry({}, usable, QSize(216, 76), {}, true, {}), QRect(2322, 682, 216, 76));
        // A capsule on the right half opens its panel leftward, keeping its right edge.
        QCOMPARE(Desktop::arrangedGeometry(QRect(2322, 300, 216, 76), usable, QSize(640, 490), {}, false, {}),
                 QRect(1898, 300, 640, 490));
        // On the bottom half it opens upward, keeping its bottom edge.
        QCOMPARE(Desktop::arrangedGeometry(QRect(200, 1200, 216, 76), usable, QSize(640, 490), {}, false, {}),
                 QRect(200, 786, 640, 490));
        // Restoring anchors on the saved frame the same way.
        QCOMPARE(Desktop::arrangedGeometry(QRect(1, 1, 30, 92), usable, QSize(216, 76), {}, true, QRect(1910, 479, 640, 490)),
                 QRect(2334, 893, 216, 76));
        // A docked tab stays flush with its edge and inside the usable area.
        QCOMPARE(Desktop::arrangedGeometry(QRect(2322, 1400, 216, 76), usable, QSize(30, 92), QStringLiteral("right"), false, {}),
                 QRect(2520, 1338, 30, 92));
        QCOMPARE(Desktop::arrangedGeometry(QRect(0, 500, 216, 76), usable, QSize(30, 92), QStringLiteral("left"), false, {}),
                 QRect(60, 500, 30, 92));
    }

    void draggingPastAnEdgeStillDocks()
    {
        const QRect monitor(0, 0, 2560, 1440);
        QCOMPARE(Desktop::nearestEdge(QRect(2428, 545, 216, 76), monitor), QStringLiteral("right"));
        QCOMPARE(Desktop::nearestEdge(QRect(-90, 545, 216, 76), monitor), QStringLiteral("left"));
        QVERIFY(Desktop::nearestEdge(QRect(2022, 545, 216, 76), monitor).isEmpty());
    }

    void snappingPreservesTabsAndClampsAvailableBounds()
    {
        const QRect available(-1920, -200, 1920, 1080);
        QCOMPARE(Desktop::snappedGeometry(QRect(-1000, -150, 28, 92), available, QStringLiteral("left")),
                 QRect(-1920, -150, 28, 92));
        QCOMPARE(Desktop::snappedGeometry(QRect(-1000, -150, 320, 160), available, QStringLiteral("right")),
                 QRect(-320, -150, 320, 160));
        QCOMPARE(Desktop::snappedGeometry(QRect(-1000, 700, 320, 160), available, QStringLiteral("top")),
                 QRect(-1000, -200, 320, 160));
        QCOMPARE(Desktop::snappedGeometry(QRect(-1000, -150, 320, 160), available, QStringLiteral("bottom")),
                 QRect(-1000, 720, 320, 160));
        QCOMPARE(Desktop::snappedGeometry(QRect(-2100, 800, 6000, 6000), available, QStringLiteral("right")), available);
        const QRect original(-1000, -150, 28, 92);
        QCOMPARE(Desktop::snappedGeometry(original, available, QStringLiteral("invalid")), original);
        QCOMPARE(Desktop::snappedGeometry(original, QRect(), QStringLiteral("left")), original);
    }

    void rejectedMoveAndSyntheticReleaseNeverComplete()
    {
        // The offscreen backend rejects native movement. A failed start and
        // Wayland-like synthetic release must not leave a pending snap.
        if (QGuiApplication::platformName() != QStringLiteral("offscreen")
            && QGuiApplication::platformName() != QStringLiteral("minimal"))
            QSKIP("This regression requires an offscreen backend that rejects native movement");
        Desktop desktop;
        QWindow window;
        window.resize(300, 180);
        window.show();
        QSignalSpy finished(&desktop, &Desktop::moveFinished);
        QVERIFY(!desktop.move(nullptr));
        QVERIFY(!desktop.move(&window));
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(20, 20), QPointF(20, 20),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &release);
        QEvent leave(QEvent::Leave), enter(QEvent::Enter);
        QCoreApplication::sendEvent(&window, &leave);
        QCoreApplication::sendEvent(&window, &enter);
        QCoreApplication::processEvents();
        QCOMPARE(finished.count(), 0);
    }

    void shortcutValidationProtectsStandardClipboardShortcuts()
    {
        for (const QString &shortcut : {QStringLiteral("Ctrl+C"), QStringLiteral("Ctrl+V"),
             QStringLiteral("Ctrl+X"), QStringLiteral("Meta+C"), QStringLiteral("Shift+Insert"),
             QStringLiteral("Ctrl+Insert")}) {
            QVERIFY2(!Desktop::validateShortcut(shortcut).isEmpty(), qPrintable(shortcut));
        }
        QVERIFY(Desktop::validateShortcut(QStringLiteral("Ctrl+Alt+S")).isEmpty());
        QVERIFY(Desktop::validateShortcut(QStringLiteral("Control+Option+S")).isEmpty());
        QVERIFY(Desktop::validateShortcut(QStringLiteral("Super+S")).isEmpty());
        QVERIFY(Desktop::validateShortcut(QStringLiteral("Cmd+Shift+S")).isEmpty());
        QVERIFY(Desktop::validateShortcut(QStringLiteral("F8")).isEmpty());
        QVERIFY(Desktop::validateShortcut(QString()).isEmpty());
        QVERIFY(!Desktop::validateShortcut(QStringLiteral("S")).isEmpty());
        QVERIFY(!Desktop::validateShortcut(QStringLiteral("Ctrl+K, Ctrl+S")).isEmpty());
        QVERIFY(!Desktop::validateShortcut(QStringLiteral("Ctrl+NotAKey")).isEmpty());
    }
    void hubShortcutRebindingUsesIsolatedManualFallback()
    {
        Desktop desktop;
        bool answered = false;
        QJsonObject result;
        ArcadeLink::Error error;
        desktop.rebindShortcut("control+alt+t", ArcadeLink::executablePath(),
            [&](QJsonObject answer, ArcadeLink::Error failure) { result = answer; error = failure; answered = true; });
        QTRY_VERIFY(answered);
        QVERIFY2(!error.isError(), qPrintable(error.message));
        QVERIFY(!result["applied"].toBool()); QCOMPARE(result["via"].toString(), QString("manual"));
        QVERIFY(!result["hint"].toString().isEmpty());
        answered = false;
        desktop.rebindShortcut("Ctrl+C", ArcadeLink::executablePath(),
            [&](QJsonObject, ArcadeLink::Error failure) { error = failure; answered = true; });
        QVERIFY(answered); QCOMPARE(error.code, QString("denied"));
    }

    void isolatedInstanceCannotInstallShortcutOrLoginEntry()
    {
        Desktop desktop;
        QSignalSpy activation(&desktop, &Desktop::activated);
        desktop.configureShortcut(QStringLiteral("Ctrl+Alt+S"), QStringLiteral("/usr/bin/arcade-shelf"));
        QVERIFY(desktop.shortcutStatus().contains(QStringLiteral("isolated")));
        QVERIFY(!desktop.setStartAtLogin(true, QStringLiteral("/usr/bin/arcade-shelf")));
        QVERIFY(desktop.lastError().contains(QStringLiteral("isolated")));
        QCOMPARE(activation.count(), 0);
    }

    void loginEntryTracksReceiptAndPreservesExtensions()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QList<QPair<QByteArray, QByteArray>> environment;
        for (const auto *key : {"ARCADE_HOME", "APPDATA", "ARCADE_SHELF_ALLOW_DESKTOP_IN_TESTS"})
            environment.append({key, qgetenv(key)});
        const auto restore = qScopeGuard([&] {
            for (const auto &item : environment) {
                if (item.second.isNull()) qunsetenv(item.first.constData());
                else qputenv(item.first.constData(), item.second);
            }
        });
        qputenv("ARCADE_HOME", directory.path().toUtf8());
        qputenv("APPDATA", directory.filePath("appdata").toUtf8());
        qputenv("ARCADE_SHELF_ALLOW_DESKTOP_IN_TESTS", "1");
        const auto env = ArcadeLink::ArcadeInstall::Environment::under(directory.path());
#ifdef Q_OS_WIN
        const QString path = QDir(env.applications).filePath("arcade-shelf.exe");
        const QString executable = path;
        const QString target = directory.filePath("appdata/Microsoft/Windows/Start Menu/Programs/Startup/Arcade Shelf.lnk");
        const QString method = "windows-installer";
#elif defined(Q_OS_MACOS)
        const QString path = QDir(env.home).filePath("Applications/Arcade Shelf.app");
        const QString executable = QDir(path).filePath("Contents/MacOS/arcade-shelf");
        const QString target = QDir(env.home).filePath("Library/LaunchAgents/arcade.shelf.plist");
        const QString method = "macos-bundle";
#else
        const QString path = QDir(env.applications).filePath("Arcade-Shelf.AppImage");
        const QString executable = path;
        const QString target = QDir(env.config).filePath("autostart/arcade.shelf.desktop");
        const QString method = "appimage";
#endif
        QVERIFY(QDir().mkpath(QFileInfo(executable).absolutePath()));
        QFile binary(executable); QVERIFY(binary.open(QIODevice::WriteOnly)); binary.write("fixture"); binary.close();
        QVERIFY(QFile::setPermissions(executable, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        const QJsonObject receipt{{"schema", 1}, {"id", "arcade.shelf"}, {"version", "0.2.0"}, {"channel", "stable"},
            {"method", method}, {"managedBy", "tools"}, {"path", path}, {"toolsExtension", QJsonObject{{"keep", true}}},
            {"integration", QJsonObject{{"icons", QJsonArray{}}, {"autostart", QJsonValue::Null}, {"futureIntegration", "keep"}}},
            {"installedAt", ArcadeLink::nowRfc3339()}, {"updatedAt", ArcadeLink::nowRfc3339()}};
        QString error; QVERIFY2(env.receipts().write(receipt, &error), qPrintable(error));
        Desktop desktop;
        QVERIFY(!desktop.startAtLogin());
#ifdef Q_OS_LINUX
        // This is the marker written by the shared Link installer, without
        // Shelf's historical comment.
        QVERIFY(QDir().mkpath(QFileInfo(target).absolutePath()));
        QFile shared(target); QVERIFY(shared.open(QIODevice::WriteOnly));
        shared.write("[Desktop Entry]\nType=Application\nX-Arcade-Id=arcade.shelf\nExec=fixture --background\n"); shared.close();
        QVERIFY(desktop.startAtLogin());
#endif
#ifdef Q_OS_WIN
        const QString legacy = QFileInfo(target).absolutePath() + "/Arcade Shelf.vbs";
        QVERIFY(QDir().mkpath(QFileInfo(legacy).absolutePath()));
        QFile script(legacy); QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("' Arcade Shelf managed login entry\r\n"); script.close();
        auto legacyReceipt = receipt;
        auto legacyIntegration = legacyReceipt.value("integration").toObject();
        legacyIntegration.insert("autostart", legacy);
        legacyReceipt.insert("integration", legacyIntegration);
        QVERIFY2(env.receipts().write(legacyReceipt, &error), qPrintable(error));
        QVERIFY(desktop.startAtLogin());
#endif
        for (const bool enabled : {true, true, false, false, true, false, true}) {
            QVERIFY2(desktop.setStartAtLogin(enabled, executable), qPrintable(desktop.lastError()));
            QCOMPARE(desktop.startAtLogin(), enabled);
            QJsonObject changed; QVERIFY2(env.receipts().read("arcade.shelf", &changed, &error), qPrintable(error));
            QCOMPARE(changed.value("toolsExtension"), receipt.value("toolsExtension"));
            QCOMPARE(changed.value("managedBy"), QJsonValue("tools"));
            const auto integration = changed.value("integration").toObject();
            QCOMPARE(integration.value("futureIntegration"), QJsonValue("keep"));
            QCOMPARE(integration.value("autostart"), enabled ? QJsonValue(target) : QJsonValue(QJsonValue::Null));
            QCOMPARE(QFileInfo::exists(target), enabled);
#ifdef Q_OS_WIN
            QVERIFY(!QFileInfo::exists(legacy));
#endif
        }
        QVERIFY(desktop.setStartAtLogin(false, executable));
        QFile foreign(target); QVERIFY(foreign.open(QIODevice::WriteOnly)); foreign.write("unrelated login entry"); foreign.close();
        QVERIFY(!desktop.startAtLogin());
        QVERIFY(!desktop.setStartAtLogin(true, executable));
        QVERIFY(!desktop.setStartAtLogin(false, executable));
        QVERIFY(foreign.open(QIODevice::ReadOnly)); QCOMPARE(foreign.readAll(), QByteArray("unrelated login entry"));
        foreign.close();
#ifdef Q_OS_UNIX
        QFile linked(directory.filePath("linked-login-entry"));
        QVERIFY(linked.open(QIODevice::WriteOnly));
        linked.write("Arcade Shelf managed login entry\n"); linked.close();
        QVERIFY(QFile::remove(target));
        QVERIFY(QFile::link(linked.fileName(), target));
        QVERIFY(!desktop.startAtLogin());
        QVERIFY(!desktop.setStartAtLogin(true, executable));
        QVERIFY(!desktop.setStartAtLogin(false, executable));
        QVERIFY(linked.open(QIODevice::ReadOnly));
        QCOMPARE(linked.readAll(), QByteArray("Arcade Shelf managed login entry\n"));
#endif
    }
};

QTEST_MAIN(DesktopTest)
#include "tst_Desktop.moc"
