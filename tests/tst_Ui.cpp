#include "Controller.h"
#include "ThumbnailProvider.h"
#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QImage>
#include <QMimeData>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <memory>

using namespace Shelf;

class UiTest : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_suite;
    std::unique_ptr<QTemporaryDir> m_profile;
    std::unique_ptr<Controller> m_controller;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;
    QByteArray m_oldHome, m_oldShelfHome;

    QString writeFile(QString name, QByteArray contents = "original content") {
        const QString path = m_profile->path() + "/" + name;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return {};
        if (f.write(contents) != contents.size()) return {};
        return path;
    }
    QPoint point(QQuickItem *item, QPointF offset) {
        return item->mapToScene(offset).toPoint();
    }
    QQuickItem *find(const char *name) {
        return m_window->findChild<QQuickItem *>(QLatin1String(name));
    }
    void settle() {
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        QSignalSpy frames(m_window,&QQuickWindow::frameSwapped);
        m_window->requestUpdate();
        QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(),2000);
    }
    void clickItem(int index, Qt::KeyboardModifiers modifiers = {}) {
        settle();
        auto *grid = find("shelfGrid");
        QVERIFY(grid);
        const int columns = qMax(2,int(grid->width()/grid->property("cellWidth").toReal()));
        QTest::mouseClick(m_window,Qt::LeftButton,modifiers,
            point(grid,QPointF((index%columns)*grid->property("cellWidth").toReal()+40,(index/columns)*152+40)));
    }
    void capture(QString name) {
        const QString dir = qEnvironmentVariable("ARCADE_SHELF_CAPTURE_DIR");
        if (dir.isEmpty()) return;
        QVERIFY(QDir().mkpath(dir));
        QTRY_VERIFY_WITH_TIMEOUT(!m_window->property("pulsing").toBool(),1500);
        QTest::mouseMove(m_window,QPoint(m_window->width()/2,20));
        QTest::qWait(250);
        const QImage image = m_window->grabWindow();
        QVERIFY2(!image.isNull(),"The window did not produce a rendered image");
        QVERIFY(image.save(dir + "/" + name + ".png"));
    }

private slots:
    void initTestCase() {
        QVERIFY(m_suite.isValid());
        m_oldHome = qgetenv("ARCADE_HOME");
        m_oldShelfHome = qgetenv("ARCADE_SHELF_HOME");
        QQuickStyle::setStyle("Basic");
        qputenv("ARCADE_HOME",(m_suite.path()+"/arcade").toUtf8());
    }
    void init() {
        m_profile = std::make_unique<QTemporaryDir>();
        QVERIFY(m_profile->isValid());
        qputenv("ARCADE_SHELF_HOME",(m_profile->path()+"/shelf").toUtf8());
        {
            Store seed(m_profile->path()+"/shelf");
            QVERIFY(seed.open());
            auto settings = seed.settings();
            settings["shortcut"] = ""; // Never install a global binding during tests.
            settings["theme"] = "dark";
            QVERIFY(seed.saveSettings(settings));
        }
        m_controller = std::make_unique<Controller>(m_profile->path()+"/shelf",false);
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->rootContext()->setContextProperty("shelf",m_controller.get());
        m_engine->addImageProvider("thumb",new ThumbnailProvider);
        m_engine->loadFromModule("ShelfUITest","Main");
        QVERIFY(!m_engine->rootObjects().isEmpty());
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
        QVERIFY(m_window);
        m_controller->attachWindow(m_window);
        m_controller->start();
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->shelves().size(),1,5000);
        QTRY_COMPARE(m_controller->activeShelf(),QString("quick"));
        QTRY_VERIFY(m_window->isVisible());
        settle();
    }
    void cleanup() {
        if(m_controller) m_controller->attachWindow(nullptr);
        m_engine.reset();
        m_controller.reset();
        m_window = nullptr;
        m_profile.reset();
    }
    void cleanupTestCase() {
        if(m_oldHome.isEmpty()) qunsetenv("ARCADE_HOME"); else qputenv("ARCADE_HOME",m_oldHome);
        if(m_oldShelfHome.isEmpty()) qunsetenv("ARCADE_SHELF_HOME"); else qputenv("ARCADE_SHELF_HOME",m_oldShelfHome);
    }
    void capsulePointerAndExpandedKeyboard() {
        QCOMPARE(m_controller->expanded(),false);
        QTRY_VERIFY(m_window->width() < 300);
        auto *capsule = find("capsule"); QVERIFY(capsule);
        QTest::mouseClick(m_window,Qt::LeftButton,{},point(capsule,QPointF(85,30)));
        QTRY_VERIFY(m_controller->expanded());
        QTRY_VERIFY(m_window->width() >= 440);
        QTest::keyClick(m_window,Qt::Key_Escape);
        QTRY_VERIFY(!m_controller->expanded());
        QTRY_VERIFY(m_window->width() < 300);
    }
    void nativeMimeDropSelectionRemoveAndUndo() {
        const QString a = writeFile("brief.txt"), b = writeFile("notes.md");
        QVERIFY(!a.isEmpty()); QVERIFY(!b.isEmpty());
        m_controller->show(); QTRY_VERIFY(m_window->width() >= 440);
        settle();
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(a),QUrl::fromLocalFile(b)});
        QDragEnterEvent enter(QPoint(85,180),Qt::CopyAction,&mime,Qt::LeftButton,{});
        QApplication::sendEvent(m_window,&enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(85,180),Qt::CopyAction,&mime,Qt::LeftButton,{});
        QApplication::sendEvent(m_window,&drop);
        QVERIFY(drop.isAccepted());
        QTRY_COMPARE(m_controller->count(),2);
        const QString first = m_controller->items()[0].toMap()["id"].toString();
        const QString second = m_controller->items()[1].toMap()["id"].toString();
        QPointer<QObject> delegate=m_window->findChild<QObject *>("tile-"+first);
        QVERIFY(delegate);
        clickItem(0); QVERIFY(m_controller->selected(first));
        QCoreApplication::processEvents();
        QVERIFY2(delegate,"Selection destroyed the pressed tile delegate before a drag could start");
        clickItem(1,Qt::ControlModifier);
        QVERIFY(m_controller->selected(first)); QVERIFY(m_controller->selected(second));
        QTest::keyClick(m_window,Qt::Key_Delete);
        QTRY_COMPARE(m_controller->count(),0);
        QVERIFY(QFile::exists(a)); QVERIFY(QFile::exists(b));
        m_controller->acceptDrop(QVariantList{QUrl::fromLocalFile(a),QUrl::fromLocalFile(b)},"","");
        QTRY_COMPARE(m_controller->count(),2);
        QVERIFY(m_controller->undoAvailable());
        QTest::keyClick(m_window,Qt::Key_Z,Qt::ControlModifier);
        QTRY_COMPARE(m_controller->count(),0);
        QVERIFY(QFile::exists(a)); QVERIFY(QFile::exists(b));
    }
    void keyboardFilterAndPreferences() {
        m_controller->acceptDrop({},"A private note","");
        QTRY_COMPARE(m_controller->count(),1);
        m_controller->show(); QTRY_VERIFY(m_window->width() >= 440);
        clickItem(0);
        QTest::keyClick(m_window,Qt::Key_A,Qt::ControlModifier);
        QVERIFY(m_controller->selected(m_controller->items()[0].toMap()["id"].toString()));
        QTest::keyClick(m_window,Qt::Key_F,Qt::ControlModifier);
        QTRY_VERIFY(find("filterField")->hasActiveFocus());
        for(char key:QByteArray("no match")) QTest::keyClick(m_window,key);
        QTRY_COMPARE(m_controller->items().size(),0);
        QTest::keyClick(m_window,Qt::Key_Escape);
        QTRY_COMPARE(m_controller->items().size(),1);
        m_controller->showSettings();
        QTRY_VERIFY(m_controller->settingsOpen());
        auto *light = find("theme-light"); QVERIFY(light);
        QTest::mouseClick(m_window,Qt::LeftButton,{},point(light,QPointF(light->width()/2,light->height()/2)));
        QTRY_COMPARE(m_controller->theme(),QString("light"));
        auto *reduce = find("reducedMotionToggle"); QVERIFY(reduce);
        QTest::mouseClick(m_window,Qt::LeftButton,{},point(reduce,QPointF(reduce->width()-16,reduce->height()/2)));
        QTRY_VERIFY(m_controller->reducedMotion());
        QTest::keyClick(m_window,Qt::Key_Escape);
        QTRY_VERIFY(!m_controller->settingsOpen());
    }
    void collectionPersistence() {
        m_controller->createShelf("Reference",false);
        QTRY_COMPARE(m_controller->shelves().size(),2);
        QTRY_COMPARE(m_controller->activeName(),QString("Reference"));
        const QString id = m_controller->activeShelf();
        m_controller->acceptDrop({},"Kept across launches","");
        QTRY_COMPARE(m_controller->count(),1);
        m_controller->renameShelf(id,"Project reference");
        QTRY_COMPARE(m_controller->activeName(),QString("Project reference"));
        m_controller->pinShelf(id,true);
        m_controller->switchShelf("quick");
        QTRY_COMPARE(m_controller->activeShelf(),QString("quick"));
        QCOMPARE(m_controller->count(),0);
        m_controller->switchShelf(id);
        QTRY_COMPARE(m_controller->count(),1);
        m_controller->attachWindow(nullptr); m_engine.reset(); m_controller.reset(); m_window=nullptr;
        Store recovered(m_profile->path()+"/shelf"); QVERIFY(recovered.open());
        QCOMPARE(recovered.items(id).size(),1);
        bool found=false;
        for(const auto &value:recovered.shelves()) if(value.toObject()["id"] == id) {
            QCOMPARE(value.toObject()["name"].toString(),QString("Project reference"));
            QVERIFY(value.toObject()["pinned"].toBool()); found=true;
        }
        QVERIFY(found);
    }
    void pickerReturnsExplicitFilteredSelectionAndCancels() {
        m_controller->acceptDrop({},"Only chosen","");
        QTRY_COMPARE(m_controller->count(),1);
        const QString path = writeFile("private.txt"); QVERIFY(!path.isEmpty());
        m_controller->acceptDrop(QVariantList{QUrl::fromLocalFile(path)},"","");
        QTRY_COMPARE(m_controller->count(),2);
        QJsonObject result; ArcadeLink::Error error;
        auto *thread = QThread::create([&] {
            auto client=ArcadeLink::Client::connect(ArcadeLink::Locations::discover(),AppId,"arcade.test","1",1000,&error);
            if(client) client->invoke({{"action","shelf.pick"},{"version",1},{"inputs",QJsonArray{}},{"options",QJsonObject{{"multiple",true},{"types",QJsonArray{"text/plain"}}}}},&result,&error);
        });
        thread->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_controller->picking(),5000);
        QTRY_COMPARE(m_controller->items().size(),1);
        QVERIFY(!find("pickerConfirm")->isEnabled());
        clickItem(0);
        auto *confirm=find("pickerConfirm"); QVERIFY(confirm); QVERIFY(confirm->isEnabled());
        QTest::mouseClick(m_window,Qt::LeftButton,{},point(confirm,QPointF(confirm->width()/2,confirm->height()/2)));
        QTRY_VERIFY_WITH_TIMEOUT(thread->isFinished(),5000); delete thread;
        QVERIFY2(!error.isError(),qPrintable(error.message));
        QCOMPARE(result["outputs"].toArray().size(),1);
        QCOMPARE(result["outputs"].toArray()[0].toObject()["text"].toString(),QString("Only chosen"));
        QTRY_VERIFY(!m_controller->picking());
        thread=QThread::create([&] {
            auto client=ArcadeLink::Client::connect(ArcadeLink::Locations::discover(),AppId,"arcade.test","1",1000,&error);
            if(client) client->invoke({{"action","shelf.pick"},{"inputs",QJsonArray{}},{"options",QJsonObject{}}},&result,&error);
        });
        thread->start(); QTRY_VERIFY_WITH_TIMEOUT(m_controller->picking(),5000);
        QTest::keyClick(m_window,Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(thread->isFinished(),5000); delete thread;
        QCOMPARE(error.code,QString("denied")); QCOMPARE(error.reason,QString("user_cancelled"));
    }
    void renderedViews() {
        const QString brief=writeFile("Design brief.md","# Arcade Shelf\nA floating workspace.");
        const QString assets=m_profile->path()+"/Brand assets"; QVERIFY(QDir().mkdir(assets));
        m_controller->acceptDrop(QVariantList{QUrl::fromLocalFile(brief),QUrl::fromLocalFile(assets)},"","");
        QTRY_COMPARE(m_controller->count(),2);
        m_controller->acceptDrop({},"A place to keep what matters while you continue working.","");
        QTRY_COMPARE(m_controller->count(),3);
        m_controller->acceptDrop({},"https://github.com/qa-p1","");
        QTRY_COMPARE(m_controller->count(),4);
        m_controller->show(); QTRY_VERIFY(m_window->width() >= 440);
        clickItem(0);
        QVERIFY(m_controller->selected(m_controller->items()[0].toMap()["id"].toString()));
        capture("expanded");
        m_controller->setTheme("light"); capture("expanded-light");
        m_controller->setTheme("dark");
        m_controller->collapse(); QTRY_VERIFY(m_window->width() < 300);
        capture("capsule");
    }
};

QTEST_MAIN(UiTest)
#include "tst_Ui.moc"
