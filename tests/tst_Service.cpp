#include "link/Service.h"
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QJsonDocument>
#include <QtTest>

using namespace Shelf;
using namespace ArcadeLink;
class ServiceTest : public QObject {
    Q_OBJECT
    template<class F> void worker(F f) {
        auto *thread = QThread::create(f); thread->start();
        QTRY_VERIFY_WITH_TIMEOUT(thread->isFinished(),10000); delete thread;
    }
private slots:
    void contract() {
        auto m = manifest(); QVERIFY(!m["launch"].toObject().contains("invoke"));
        QCOMPARE(m["launch"].toObject()["background"].toArray(),QJsonArray{"--background"});
        auto add = publicActions()[0].toObject(); QCOMPARE(add["effects"].toArray(),QJsonArray{"persists"}); QVERIFY(!add["interactive"].toBool());
        QVERIFY(manifest(false)["actions"].toArray().isEmpty());
        QCOMPARE(m["menu"].toArray(), trayMenu());
        QCOMPARE(m["docs"].toObject()["version"].toString(), Version);
        QVERIFY(QDir::isAbsolutePath(m["logs"].toString()));
        QVERIFY((QStringList{"dev", "manual", "appimage", "macos-bundle", "windows-installer"}.contains(m["install"].toString())));
        QCOMPARE(manifest(true, "ctrl + alt + s")["shortcuts"].toArray().first().toObject()["accelerator"].toString(), QString("Ctrl+Alt+S"));
    }
    void hubMethodsAndPortableHandoff() {
        QTemporaryDir root; QVERIFY(root.isValid());
        qputenv("ARCADE_HOME", (root.path()+"/link").toUtf8());
        qputenv("ARCADE_DEV_BUILD", "1");
        Service service(root.path()+"/shelf"); service.start();
        QSignalSpy opened(&service, &Service::showRequested);
        QSignalSpy created(&service, &Service::newCollectionRequested);
        connect(&service, &Service::shortcutRequested, &service, [&service](QString) {
            service.completeShortcutBinding({{"applied", false}, {"via", "manual"}, {"hint", "Isolated test."}});
        });
        service.setTrayState("hosted");
        QJsonObject description, status, rebound, imported;
        QJsonArray menu, exported;
        Error error;
        worker([&] {
            auto client = Client::connect(Locations::discover(), AppId, "arcade.tools", "1", 1000, &error);
            if (!client) return;
            description = client->describe(&error); if (error.isError()) return;
            status = client->call("app.status", {}, &error).toObject(); if (error.isError()) return;
            client->settings(&error); if (error.isError()) return;
            menu = client->menu(&error); if (error.isError()) return;
            client->menuInvoke("new-shelf", &error); if (error.isError()) return;
            rebound = client->shortcutsSet("show-shelf", "control+alt+t", &error);
        });
        QVERIFY2(!error.isError(), qPrintable(error.message));
        for (const auto &name : {"app.settings", "app.restart", "app.menu", "app.menu.invoke", "app.shortcuts.set", "app.settings.export", "app.settings.import"})
            QVERIFY(description["methods"].toArray().contains(QString::fromLatin1(name)));
        QCOMPARE(status["status"].toObject()["tray"].toString(), QString("hosted"));
        QCOMPARE(menu, trayMenu()); QCOMPARE(opened.size(), 1); QVERIFY(opened.first()[1].toBool());
        QCOMPARE(created.size(), 1); QCOMPARE(rebound["via"].toString(), QString("manual"));
        Store view(root.path()+"/shelf"); QVERIFY(view.open());
        QCOMPARE(view.settings()["shortcut"].toString(), QString("Ctrl+Alt+T"));
        QFile published(Locations::discover().manifestPath(AppId)); QVERIFY(published.open(QIODevice::ReadOnly));
        const auto currentManifest = QJsonDocument::fromJson(published.readAll()).object();
        QCOMPARE(currentManifest["install"].toString(), QString("dev"));
        QCOMPARE(currentManifest["menu"].toArray(), trayMenu());
        QCOMPARE(currentManifest["shortcuts"].toArray().first().toObject()["accelerator"].toString(), QString("Ctrl+Alt+T"));
        QFile original(root.path()+"/original.txt"); QVERIFY(original.open(QIODevice::WriteOnly)); original.write("keep"); original.close();
        service.add({fileContent(original.fileName()), textContent("text/plain", "owned content is opt-in")});
        service.saveSettings({{"theme", "dark"}});
        worker([&] { auto client = Client::connect(Locations::discover(), AppId, "arcade.tools", "1", 1000, &error); if (client) exported = client->settingsExport(&error); });
        QVERIFY2(!error.isError(), qPrintable(error.message)); QCOMPARE(exported.size(), 1);
        const auto output = exported.first().toObject(); QCOMPARE(output["type"].toString(), QString("file/any"));
        QVERIFY(output["path"].toString().startsWith(Locations::discover().handoff + "/"));
        QVERIFY(QFileInfo::exists(output["path"].toString()));
        service.saveSettings({{"theme", "light"}});
        service.add({textContent("text/plain", "replace me")});
        worker([&] { auto client = Client::connect(Locations::discover(), AppId, "arcade.tools", "1", 1000, &error); if (client) imported = client->settingsImport(exported, &error); });
        QVERIFY2(!error.isError(), qPrintable(error.message)); QVERIFY(imported["imported"].toBool()); QVERIFY(!imported["restartRequired"].toBool());
        QCOMPARE(view.settings()["theme"].toString(), QString("dark")); QCOMPARE(view.items("quick").size(), 1);
        QVERIFY(original.exists()); QVERIFY(!QDir(root.path()+"/shelf/backups").entryList({"*.sqlite3"}, QDir::Files).isEmpty());
        worker([&] {
            auto client = Client::connect(Locations::discover(), AppId, "arcade.tools", "1", 1000, &error);
            if (client) client->settingsImport({QJsonObject{{"type", "file/any"}, {"path", original.fileName()}}}, &error);
        });
        QCOMPARE(error.code, QString("denied"));
        worker([&] { auto client = Client::connect(Locations::discover(), AppId, "arcade.tools", "1", 1000, &error); if (client) client->menuInvoke("unknown", &error); });
        QCOMPARE(error.code, QString("denied"));
        worker([&] { auto client = Client::connect(Locations::discover(), AppId, "arcade.tools", "1", 1000, &error); if (client) client->shortcutsSet("unknown", "Ctrl+Alt+T", &error); });
        QCOMPARE(error.code, QString("denied"));
        service.retain({"active"});
        worker([&] { auto client = Client::connect(Locations::discover(), AppId, "arcade.tools", "1", 1000, &error); if (client) client->restart({}, &error); });
        QCOMPARE(error.code, QString("busy"));
        service.release(); service.setTrayState("own");
        worker([&] { auto client = Client::connect(Locations::discover(), AppId, "arcade.tools", "1", 1000, &error); if (client) status = client->call("app.status", {}, &error).toObject(); });
        QVERIFY(!error.isError()); QCOMPARE(status["status"].toObject()["tray"].toString(), QString("own"));
        service.stop(); qunsetenv("ARCADE_HOME"); qunsetenv("ARCADE_DEV_BUILD");
    }
    void restoreMayTurnOffLinkAfterReply() {
        QTemporaryDir root; qputenv("ARCADE_HOME", (root.path()+"/link").toUtf8());
        const QString handoff = Locations::discover().handoff;
        QVERIFY(ensurePrivateDir(handoff));
        Store source(root.path()+"/source"); QVERIFY(source.open()); QVERIFY(source.saveSettings({{"linkEnabled", false}}));
        const QString backup = handoff + "/disabled.arcade-shelf";
        QString message; QVERIFY2(source.exportPortable(backup, false, nullptr, nullptr, &message), qPrintable(message));
        Service service(root.path()+"/shelf"); service.start();
        Error error; QJsonObject result;
        worker([&] { auto client = Client::connect(Locations::discover(), AppId, "arcade.tools", "1", 1000, &error); if (client) result = client->settingsImport({QJsonObject{{"type", "file/any"}, {"path", backup}}}, &error); });
        QVERIFY2(!error.isError(), qPrintable(error.message)); QVERIFY(result["imported"].toBool());
        QTRY_VERIFY(!QFile::exists(Locations::discover().endpointPath(AppId)));
        QVERIFY(QFile::exists(Locations::under(root.path()+"/shelf/instance").endpointPath(AppId)));
        service.stop(); qunsetenv("ARCADE_HOME");
    }
    void realEndpointPersistenceAndPartialSuccess() {
        QTemporaryDir root; qputenv("ARCADE_HOME",(root.path()+"/link").toUtf8());
        Service service(root.path()+"/shelf"); service.start();
        QSignalSpy feedback(&service,&Service::feedback);
        QFile file(root.path()+"/a.txt"); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("original"); file.close();
        QDir().mkdir(root.path()+"/folder");
        QJsonObject result; Error error;
        worker([&] {
            auto c = Client::connect(Locations::discover(),AppId,"arcade.find","0.1",1000,&error); if (!c) return;
            c->invoke({{"action","shelf.add"},{"version",1},{"inputs",QJsonArray{QJsonObject{{"type","file/any[]"},{"paths",QJsonArray{file.fileName(),root.path()+"/missing"}}},QJsonObject{{"type","folder/reference"},{"path",root.path()+"/folder"}}}},{"options",QJsonObject{}}},&result,&error);
        });
        QVERIFY2(!error.isError(),qPrintable(error.message)); QCOMPARE(result["data"].toObject()["added"].toArray().size(),2); QCOMPARE(result["data"].toObject()["skipped"].toArray().size(),1);
        QVERIFY(!feedback.isEmpty()); QVERIFY(feedback.last()[1].toBool()); service.stop();
        Store recovered(root.path()+"/shelf"); QVERIFY(recovered.open()); QCOMPARE(recovered.items("quick").size(),2); QVERIFY(file.exists());
        qunsetenv("ARCADE_HOME");
    }
    void explicitPickerAndCancellation() {
        QTemporaryDir root; qputenv("ARCADE_HOME",(root.path()+"/link").toUtf8());
        Service service(root.path()+"/shelf"); service.start(); service.add({textContent("text/plain","private one"),textContent("text/plain","private two")});
        Store view(root.path()+"/shelf"); QVERIFY(view.open()); const QString id = view.items("quick")[0].toObject()["id"].toString();
        connect(&service,&Service::pickRequested,&service,[&](bool multiple,QStringList) { QVERIFY(multiple); service.completePick({id}); });
        QJsonObject result; Error error;
        worker([&] { auto c = Client::connect(Locations::discover(),AppId,"arcade.test","1",1000,&error); if(c) c->invoke({{"action","shelf.pick"},{"inputs",QJsonArray{}},{"options",QJsonObject{{"multiple",true},{"types",QJsonArray{"text/plain"}}}}},&result,&error); });
        QVERIFY2(!error.isError(),qPrintable(error.message)); QCOMPARE(result["outputs"].toArray().size(),1); QCOMPARE(result["outputs"].toArray()[0].toObject()["text"].toString(),"private one");
        disconnect(&service,&Service::pickRequested,&service,nullptr);
        connect(&service,&Service::pickRequested,&service,[&](bool,QStringList) { service.cancelPick(); });
        worker([&] { auto c = Client::connect(Locations::discover(),AppId,"arcade.test","1",1000,&error); if(c) c->invoke({{"action","shelf.pick"},{"inputs",QJsonArray{}},{"options",QJsonObject{}}},&result,&error); });
        QCOMPARE(error.code,"denied"); QCOMPARE(error.reason,"user_cancelled"); service.stop(); qunsetenv("ARCADE_HOME");
    }
    void disabledKeepsPrivateLifecycle() {
        QTemporaryDir root; qputenv("ARCADE_HOME",(root.path()+"/link").toUtf8());
        Service service(root.path()+"/shelf"); service.start(); service.saveSettings({{"linkEnabled",false}});
        QVERIFY(!QFile::exists(Locations::discover().endpointPath(AppId)));
        QFile manifestFile(Locations::discover().manifestPath(AppId)); QVERIFY(manifestFile.open(QIODevice::ReadOnly)); auto m = QJsonDocument::fromJson(manifestFile.readAll()).object(); QVERIFY(m["actions"].toArray().isEmpty());
        QSignalSpy activation(&service,&Service::showRequested); Error error;
        worker([&] { auto c = Client::connect(Locations::under(root.path()+"/shelf/instance"),AppId,AppId,"1",1000,&error); if(c) c->call("app.activate",{},&error); });
        QVERIFY(!error.isError()); QCOMPARE(activation.size(),1); service.stop(); qunsetenv("ARCADE_HOME");
    }
};
QTEST_GUILESS_MAIN(ServiceTest)
#include "tst_Service.moc"
