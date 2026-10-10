#include "link/Service.h"
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
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
