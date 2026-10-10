#include "Controller.h"
#include "ThumbnailProvider.h"
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
#include <QTextStream>
#include <QTimer>
#include <QSvgRenderer>
#include <QPainter>
#include <QImageReader>

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
    QApplication app(argc,argv);
    // Cap intermediate allocations even for image formats whose decoders do
    // not support native scaled reads. Thumbnail workers are separately bounded.
    QImageReader::setAllocationLimit(64);
    app.setQuitOnLastWindowClosed(false);
    app.setDesktopFileName("arcade.shelf");
    QQuickStyle::setStyle("Basic");
    QCommandLineParser parser;
    parser.setApplicationDescription("A floating workspace for desktop content."); parser.addHelpOption(); parser.addVersionOption();
    for (const auto &flag : QStringList{"background","settings","show","toggle","quit","status"}) parser.addOption(QCommandLineOption(flag,flag));
    parser.addOption(QCommandLineOption("wait-for-exit","Wait for an old resident before restarting.","pid"));
    parser.addOption(QCommandLineOption("add","Stage a local file or folder.","path")); parser.process(app);
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
        else if (parser.isSet("background")) return 0;
        else if (parser.isSet("settings") || parser.isSet("toggle") || parser.isSet("add")) {
            const QString action = parser.isSet("settings") ? "internal.settings" : parser.isSet("toggle") ? "internal.toggle" : "internal.add";
            QJsonArray inputs; if (parser.isSet("add")) inputs << ArcadeLink::fileContent(QFileInfo(parser.value("add")).absoluteFilePath());
            client->call("invoke",{{"action",action},{"inputs",inputs}},&error,5000);
        } else client->call("app.activate",{},&error,1000);
        if (error.isError()) { qCritical("%s",qPrintable(error.userMessage("Arcade Shelf"))); return 1; } return 0;
    }
    if (parser.isSet("quit") || parser.isSet("status")) { if (parser.isSet("status")) QTextStream(stdout) << "{\"running\":false}\n"; return 0; }
    Shelf::Controller controller(root,parser.isSet("background"));
    // A stopped app must accept Link requests promptly, even while QML imports.
    // A foreground launch still starts integration after creating its first surface.
    if (parser.isSet("background")) controller.start();
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
    QIcon icon(glyph); app.setWindowIcon(icon);
    QSystemTrayIcon tray(icon);
    QMenu menu;
    menu.addAction("Open Arcade Shelf",&controller,&Shelf::Controller::show);
    menu.addAction("Open Settings",&controller,&Shelf::Controller::showSettings);
    menu.addAction("Restart Arcade Shelf",&app,[&] {
        QStringList successor = parser.isSet("background") ? QStringList{"--background"} : QStringList{"--show"};
        successor << "--wait-for-exit" << QString::number(QCoreApplication::applicationPid());
        if (!QProcess::startDetached(ArcadeLink::executablePath(),successor)) {
            qWarning("Could not start the replacement Shelf process; keeping this instance running.");
            tray.showMessage("Arcade Shelf","Could not restart. This Shelf instance is still running.",QSystemTrayIcon::Warning);
            return;
        }
        app.quit();
    });
    menu.addSeparator(); menu.addAction("Quit Arcade Shelf",&app,&QCoreApplication::quit);
    tray.setContextMenu(&menu);
    QObject::connect(&tray,&QSystemTrayIcon::activated,&controller,[&](QSystemTrayIcon::ActivationReason reason) { if (reason == QSystemTrayIcon::Trigger) controller.showSettings(); });
    tray.show();
    // Off the initial QML creation path; background endpoint remains promptly available.
    QTimer::singleShot(0,&controller,[&] {
        if (!parser.isSet("background")) controller.start();
        if (parser.isSet("settings")) controller.showSettings();
        else if (parser.isSet("show") || parser.isSet("toggle")) controller.show();
        if (parser.isSet("add")) controller.acceptDrop(QVariantList{QUrl::fromLocalFile(QFileInfo(parser.value("add")).absoluteFilePath())},{},{});
    });
    return app.exec();
}
