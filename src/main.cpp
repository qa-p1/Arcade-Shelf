#include "Controller.h"
#include "ThumbnailProvider.h"
#include "platform/AppInstaller.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QFileInfo>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QProcess>
#include <QThread>
#include <QTextStream>
#include <QTimer>
#include <QSvgRenderer>
#include <QPainter>
#include <QImageReader>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSGRendererInterface>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>

namespace {
ArcadeApp::InstallSpec installSpec()
{
    ArcadeApp::InstallSpec spec;
    spec.app = {Shelf::AppId, "Arcade Shelf", "Arcade-Shelf.AppImage", "arcade.shelf", "arcade-shelf",
                {"--background"}, {"io.github.qa_p1.ArcadeShelf"}, "arcade.shelf"};
    spec.version = Shelf::Version;
    spec.bundleName = "Arcade Shelf.app";
    spec.stateDir = Shelf::dataRoot();
    spec.dataFolders = {Shelf::dataRoot()};
    spec.writeIcons = [](const QString &directory) {
        QList<ArcadeLink::ArcadeInstall::Icon> icons;
        const QString source = QStringLiteral(":/qt/qml/ArcadeShelf/assets/shelf.svg");
        const QString svg = directory + "/arcade-shelf.svg";
        if (!QFile::copy(source, svg)) return icons;
        icons.append({svg, 0});
        QSvgRenderer renderer(source);
        if (!renderer.isValid()) return QList<ArcadeLink::ArcadeInstall::Icon>{};
        for (const int pixels : {16, 24, 32, 48, 64, 128, 256, 512}) {
            QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            { QPainter painter(&image); renderer.render(&painter); }
            const QString path = directory + "/arcade-shelf-" + QString::number(pixels) + ".png";
            if (!image.save(path)) return QList<ArcadeLink::ArcadeInstall::Icon>{};
            icons.append({path, pixels});
        }
        return icons;
    };
    spec.quitRunning = [](QString *message) {
        QLockFile lock(Shelf::dataRoot() + "/instance.lock");
        lock.setStaleLockTime(0);
        if (lock.tryLock()) return true;
        ArcadeLink::Error error;
        auto client = ArcadeLink::Client::connect(ArcadeLink::Locations::under(Shelf::dataRoot() + "/instance"),
                                                 Shelf::AppId, Shelf::AppId, Shelf::Version, 1000, &error);
        if (!client || !client->call("app.quit", {}, &error, 3000).toObject()["quitting"].toBool()) {
            if (message) *message = error.isError() ? error.userMessage("Arcade Shelf") : QStringLiteral("Shelf is busy; finish the current action first.");
            return false;
        }
        QElapsedTimer deadline; deadline.start();
        while (deadline.elapsed() < 3000) {
            if (lock.tryLock()) return true;
            QThread::msleep(25);
        }
        if (message) *message = QStringLiteral("The running Shelf instance did not stop.");
        return false;
    };
    return spec;
}
}

int main(int argc, char **argv)
{
    QStringList arguments;
    for (int i = 1; i < argc; ++i) arguments << QString::fromLocal8Bit(argv[i]);
    QCoreApplication::setOrganizationName("qa-p1");
    QCoreApplication::setApplicationName("ArcadeShelf");
    QCoreApplication::setApplicationVersion(Shelf::Version);
    // Cheap CLI paths do not connect to a compositor, create directories, or register anything.
    if (arguments.contains("--version") || arguments.contains("--arcade-manifest")) {
        QCoreApplication app(argc,argv);
        if (arguments.contains("--version")) QTextStream(stdout) << "Arcade Shelf " << Shelf::Version << '\n';
        else QTextStream(stdout) << QJsonDocument(Shelf::manifest()).toJson(QJsonDocument::Indented);
        return 0;
    }
    if (arguments.contains("--arcade-invoke")) {
        QTextStream(stderr) << "Shelf uses resident background handling; --arcade-invoke is not supported.\n"; return 2;
    }
    const bool headless = ArcadeApp::isHeadlessIntegrationCommand(arguments) || arguments.contains("--silent")
        || arguments.contains("--quit") || arguments.contains("--status") || arguments.contains("--restart")
        || arguments.contains("--help") || arguments.contains("-h");
    std::unique_ptr<QCoreApplication> application;
    if (headless) application = std::make_unique<QCoreApplication>(argc, argv);
    else application = std::make_unique<QApplication>(argc, argv);
    auto &app = *application;
    QCommandLineParser parser;
    parser.setApplicationDescription("A floating workspace for desktop content."); parser.addHelpOption(); parser.addVersionOption();
    for (const auto &flag : QStringList{"background","settings","show","toggle","quit","status"}) parser.addOption(QCommandLineOption(flag,flag));
    parser.addOption(QCommandLineOption("wait-for-exit","Wait for an old resident before restarting.","pid"));
    parser.addOption(QCommandLineOption("restart","Restart the running Arcade Shelf instance."));
    parser.addOption(QCommandLineOption("install","Install this package."));
    parser.addOption(QCommandLineOption("uninstall","Uninstall this app."));
    parser.addOption(QCommandLineOption("repair","Repair this installation's integration."));
    parser.addOption(QCommandLineOption("integration-status","Print installation integration status as JSON."));
    parser.addOption(QCommandLineOption("silent","Suppress installer UI."));
    parser.addOption(QCommandLineOption("no-launch","Do not launch the app after installation."));
    parser.addOption(QCommandLineOption("channel","Select the installation channel.","channel"));
    parser.addOption(QCommandLineOption("remove-data","Remove app data while uninstalling."));
    parser.addOption(QCommandLineOption("add","Stage a local file or folder.","path")); parser.process(app);
    const QStringList installActions{"install", "uninstall", "repair", "integration-status"};
    int installActionCount = 0;
    for (const auto &action : installActions) if (parser.isSet(action)) ++installActionCount;
    const auto invalidCli = [&](const QString &message) {
        QTextStream(stderr) << "arcade-shelf: " << message << '\n';
        return 2;
    };
    if (installActionCount > 1) return invalidCli("choose only one of --install, --uninstall, --repair, or --integration-status.");
    if (parser.isSet("silent") && !parser.isSet("install") && !parser.isSet("uninstall")) return invalidCli("--silent requires --install or --uninstall.");
    if (parser.isSet("no-launch") && !parser.isSet("install")) return invalidCli("--no-launch requires --install.");
    if (parser.isSet("remove-data") && !parser.isSet("uninstall")) return invalidCli("--remove-data requires --uninstall.");
    if (parser.isSet("channel") && (!parser.isSet("install") || !QStringList{"stable", "nightly"}.contains(parser.value("channel"))))
        return invalidCli("--channel requires --install and a value of stable or nightly.");
    if (installActionCount) return ArcadeApp::runIntegrationCommand(installSpec(), arguments);
    if (!headless && ArcadeApp::offerInstall(installSpec(), arguments)) return 0;
    const QString root = Shelf::dataRoot();
    if (!ArcadeLink::ensurePrivateDir(root)) { qCritical("Shelf data directory is unsafe or unavailable."); return 1; }
    QLockFile lock(root + "/instance.lock"); lock.setStaleLockTime(0);
    bool locked = lock.tryLock(0);
    if (!locked && parser.isSet("wait-for-exit")) {
        for (int attempt = 0; attempt < 60 && !locked; ++attempt) { QThread::msleep(50); locked = lock.tryLock(0); }
        if (!locked) { qCritical("The previous instance did not stop."); return 1; }
    }
    if (!locked) {
        ArcadeLink::Error error;
        std::unique_ptr<ArcadeLink::Client> client;
        // A concurrent first launch may not have created its private endpoint yet.
        for (int attempt = 0; attempt < 30 && !client; ++attempt) {
            client = ArcadeLink::Client::connect(ArcadeLink::Locations::under(root + "/instance"),Shelf::AppId,Shelf::AppId,Shelf::Version,100,&error);
            if (!client) QThread::msleep(50);
        }
        if (!client) { qCritical("Could not contact the resident Shelf instance: %s",qPrintable(error.userMessage("Arcade Shelf"))); return 1; }
        if (parser.isSet("quit")) client->call("app.quit",{},&error,1000);
        else if (parser.isSet("status")) QTextStream(stdout) << QJsonDocument(client->call("app.status",{},&error,1000).toObject()).toJson(QJsonDocument::Compact) << '\n';
        else if (parser.isSet("restart")) {
            client->restart({}, &error);
        }
        else if (parser.isSet("background")) return 0;
        else if (parser.isSet("settings") || parser.isSet("toggle") || parser.isSet("add")) {
            const QString action = parser.isSet("settings") ? "internal.settings" : parser.isSet("toggle") ? "internal.toggle" : "internal.add";
            QJsonArray inputs; if (parser.isSet("add")) inputs << ArcadeLink::fileContent(QFileInfo(parser.value("add")).absoluteFilePath());
            client->call("invoke",{{"action",action},{"inputs",inputs}},&error,5000);
        } else client->call("app.activate",{},&error,1000);
        if (error.isError()) { qCritical("%s",qPrintable(error.userMessage("Arcade Shelf"))); return 1; } return 0;
    }
    if (headless) { if (parser.isSet("status")) QTextStream(stdout) << "{\"running\":false}\n"; return 0; }
    // Started at login or by Tools, stderr often goes to /dev/null: keep
    // Qt's warnings in the log folder the manifest advertises instead.
    ArcadeLink::captureStderr(root + "/logs", "arcade-shelf");
    auto *gui = qobject_cast<QApplication *>(&app);
    QImageReader::setAllocationLimit(64);
    gui->setQuitOnLastWindowClosed(false);
    gui->setDesktopFileName("arcade.shelf");
    QQuickStyle::setStyle("Basic");
    Shelf::Controller controller(root,parser.isSet("background"));
    // A stopped app must accept Link requests promptly, even while QML imports.
    // A foreground launch still starts integration after creating its first surface.
    if (parser.isSet("background")) controller.start();
#ifdef Q_OS_LINUX
    // A host whose GL driver cannot start (missing EGL, mismatched libraries)
    // would make Qt Quick abort. Probe first and fall back to software
    // rendering unless the user already chose a backend.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND") && qEnvironmentVariableIsEmpty("QSG_RHI_BACKEND")
        && QGuiApplication::platformName() != QStringLiteral("offscreen")) {
        QOffscreenSurface surface; surface.create();
        QOpenGLContext context;
        if (!context.create() || !context.makeCurrent(&surface)) {
            qWarning("OpenGL is unavailable; Arcade Shelf is using software rendering.");
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        } else context.doneCurrent();
    }
#endif
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("shelf",&controller);
    auto *thumbnails = new Shelf::ThumbnailProvider;
    engine.addImageProvider("thumb",thumbnails);
    QObject::connect(&controller,&Shelf::Controller::changed,&engine,[thumbnails,&controller] {
        const auto preferences = controller.preferences();
        if (preferences.contains("thumbnailQuotaBytes"))
            thumbnails->setCacheLimitBytes(preferences.value("thumbnailQuotaBytes").toLongLong());
    });
    QObject::connect(&engine,&QQmlApplicationEngine::objectCreationFailed,&app,[]{ QCoreApplication::exit(1); },Qt::QueuedConnection);
    engine.loadFromModule("ArcadeShelf","Main");
    if (engine.rootObjects().isEmpty()) return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    controller.attachWindow(window);
    QPixmap glyph(64,64); glyph.fill(Qt::transparent);
    QSvgRenderer renderer(QStringLiteral(":/qt/qml/ArcadeShelf/assets/shelf.svg"));
    { QPainter painter(&glyph); renderer.render(&painter); }
    QIcon icon(glyph); gui->setWindowIcon(icon);
    QSystemTrayIcon tray(icon);
    QMenu menu;
    menu.addAction("Open Arcade Shelf",&controller,&Shelf::Controller::show);
    menu.addAction("Open Settings",&controller,&Shelf::Controller::showSettings);
    menu.addAction("New shelf…",&controller,&Shelf::Controller::newCollectionFromTray);
    menu.addAction("Show shelf",&controller,&Shelf::Controller::show);
    menu.addAction("Restart Arcade Shelf",&controller,&Shelf::Controller::restart);
    menu.addSeparator(); menu.addAction("Quit Arcade Shelf",&app,&QCoreApplication::quit);
    tray.setContextMenu(&menu);
    QObject::connect(&tray,&QSystemTrayIcon::activated,&controller,[&](QSystemTrayIcon::ActivationReason reason) { if (reason == QSystemTrayIcon::Trigger) controller.showSettings(); });
    auto *trayHost = controller.trayHost();
    QObject::connect(trayHost, &ArcadeLink::ArcadeTrayHost::startupReady, &tray, [&tray](const QString &state) {
        tray.setVisible(state == "own");
    });
    QObject::connect(trayHost, &ArcadeLink::ArcadeTrayHost::stateChanged, &tray, [&tray, trayHost](const QString &state) {
        if (trayHost->ready()) tray.setVisible(state == "own");
    });
    if (trayHost->ready()) tray.setVisible(trayHost->state() == "own");
    // Quitting while Tools hosts the tray must not flash Shelf's own icon.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, trayHost, [trayHost] { trayHost->finish(); });
    // Off the initial QML creation path; background endpoint remains promptly available.
    QTimer::singleShot(0,&controller,[&] {
        if (!parser.isSet("background")) controller.start();
        if (parser.isSet("settings")) controller.showSettings();
        else if (parser.isSet("show") || parser.isSet("toggle")) controller.show();
        if (parser.isSet("add")) controller.acceptDrop(QVariantList{QUrl::fromLocalFile(QFileInfo(parser.value("add")).absoluteFilePath())},{},{});
    });
    return app.exec();
}
