#include "Controller.h"
#include "ThumbnailProvider.h"
#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
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
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTest>
#include <QThread>
#include <QWheelEvent>
#include <functional>
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
    QQuickItem *find(QString name) {
        if(auto *item=m_window->findChild<QQuickItem *>(name)) return item;
        // Repeater/GridView delegates and popup overlays have visual parents
        // that differ from their QObject owner. Search the actual scene too.
        std::function<QQuickItem *(QQuickItem *)> visit=[&](QQuickItem *parent) -> QQuickItem * {
            if(parent->objectName() == name) return parent;
            for(auto *child:parent->childItems()) if(auto *item=visit(child)) return item;
            return nullptr;
        };
        return visit(m_window->contentItem());
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
            settings["shortcut"] = ""; // No binding unless a case explicitly tests shortcut settings.
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
        settle();
        const QString first = m_controller->items()[0].toMap()["id"].toString();
        const QString second = m_controller->items()[1].toMap()["id"].toString();
        QPointer<QObject> delegate=find("tile-"+first);
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
        settle();
        auto *light = find("theme-light"); QVERIFY(light);
        QTest::mouseClick(m_window,Qt::LeftButton,{},point(light,QPointF(light->width()/2,light->height()/2)));
        QTRY_COMPARE(m_controller->theme(),QString("light"));
        auto *reduce = find("reducedMotionToggle"); QVERIFY(reduce);
        QTest::mouseClick(m_window,Qt::LeftButton,{},point(reduce,QPointF(reduce->width()-16,reduce->height()/2)));
        QTRY_VERIFY(m_controller->reducedMotion());
        QTest::keyClick(m_window,Qt::Key_Escape);
        QTRY_VERIFY(!m_controller->settingsOpen());
    }
    void shortcutDraftSurvivesIncomingAddsAndCommits() {
        m_controller->showSettings(); settle();
        auto *scroll=find("preferencesScroll"); QVERIFY(scroll);
        auto *field=find("shortcutField"); QVERIFY(field);
        // The panel grows from the capsule first; the popup lays out after it.
        QTRY_VERIFY_WITH_TIMEOUT(scroll->height()>100,3000);
        const QPoint scrollPoint=point(scroll,QPointF(scroll->width()/2,scroll->height()/2));
        const auto inViewport=[&] {
            const QPointF position=field->mapToItem(scroll,QPointF(field->width()/2,field->height()/2));
            return position.y()>0 && position.y()<scroll->height();
        };
        // Wheel scrolling animates over several frames (and can overshoot and
        // rebound). A press on a moving Flickable only stops it, so wait for
        // the view to come to rest with the target inside the clipped viewport.
        auto *flick=scroll->property("contentItem").value<QQuickItem *>(); QVERIFY(flick);
        for(int attempt=0;attempt<4 && !inViewport();++attempt) {
            QTest::mouseMove(m_window,scrollPoint);
            QWheelEvent wheel(scrollPoint,m_window->mapToGlobal(scrollPoint),{},{0,-480},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(m_window,&wheel);
            QTRY_VERIFY_WITH_TIMEOUT(!flick->property("moving").toBool(),3000);
        }
        QVERIFY2(inViewport(),qPrintable(QStringLiteral("Shortcut center y=%1, preferences viewport height=%2")
            .arg(field->mapToItem(scroll,QPointF(field->width()/2,field->height()/2)).y()).arg(scroll->height())));
        settle();
        QTest::mouseClick(m_window,Qt::LeftButton,{},point(field,QPointF(field->width()/2,field->height()/2)));
        QTRY_VERIFY(field->hasActiveFocus());
        QTest::keyClick(m_window,Qt::Key_A,Qt::ControlModifier);
        const QByteArray prefix="Ctrl+Alt+";
        for(char key:prefix) QTest::keyClick(m_window,key);
        QCOMPARE(field->property("text").toString(),QString::fromLatin1(prefix));
        QCOMPARE(m_controller->shortcut(),QString());
        m_controller->acceptDrop({},"Arrived while configuring the shortcut","");
        QTRY_COMPARE(m_controller->count(),1);
        settle();
        QCOMPARE(field->property("text").toString(),QString::fromLatin1(prefix));
        QVERIFY(field->hasActiveFocus());
        QTest::keyClick(m_window,'J');
        QTest::keyClick(m_window,Qt::Key_Return);
        QTRY_COMPARE(m_controller->shortcut(),QString("Ctrl+Alt+J"));
        m_controller->attachWindow(nullptr); m_engine.reset(); m_controller.reset(); m_window=nullptr;
        Store recovered(m_profile->path()+"/shelf"); QVERIFY(recovered.open());
        QCOMPARE(recovered.settings()["shortcut"].toString(),QString("Ctrl+Alt+J"));
        QCOMPARE(recovered.items("quick").size(),1);
    }
    void rapidPreferenceEditsSurviveWorkerSnapshots() {
        // All writes race queued imports/refreshes, then are verified through
        // a fresh SQLite connection after the resident has fully stopped.
        for(int index=0;index<12;++index) {
            m_controller->acceptDrop({},QStringLiteral("Concurrent preference item %1").arg(index),"");
            m_controller->setTheme(index%2 ? "light" : "dark");
            m_controller->setReducedMotion(index%2);
            m_controller->setRetention(30+index);
            m_controller->setQuota(128+index);
            m_controller->setPeerEnabled("arcade.look",index%2);
            m_controller->show();
        }
        QTRY_COMPARE(m_controller->count(),12);
        QTRY_COMPARE(m_controller->theme(),QString("light"));
        QTRY_VERIFY(m_controller->reducedMotion());
        m_controller->attachWindow(nullptr); m_engine.reset(); m_controller.reset(); m_window=nullptr;
        Store recovered(m_profile->path()+"/shelf"); QVERIFY(recovered.open());
        const auto settings=recovered.settings();
        QCOMPARE(settings["theme"].toString(),QString("light"));
        QVERIFY(settings["reducedMotion"].toBool());
        QCOMPARE(settings["retentionDays"].toInt(),41);
        QCOMPARE(settings["quotaBytes"].toInteger(),qint64(139)*1048576);
        QVERIFY(settings["peers"].toObject()["arcade.look"].toBool());
        QCOMPARE(recovered.items("quick").size(),12);
    }
    void dockedPanelsExpandInwardAndRestoreEveryEdge() {
        if(QGuiApplication::platformName()=="wayland") QSKIP("Ordinary Wayland geometry is compositor managed; run native compositor validation separately.");
        const QStringList edges={"left","right","top","bottom"};
        for(const auto &edge:edges) {
            m_controller->collapse(); m_controller->dock(edge); settle();
            QTRY_COMPARE(m_controller->dockEdge(),edge);
            const bool vertical=edge=="left"||edge=="right";
            QTRY_COMPARE(m_window->width(),vertical ? 38 : 112);
            QTRY_COMPARE(m_window->height(),vertical ? 112 : 38);
            const QRect available=m_window->screen()->availableGeometry();
            auto aligned=[&] {
                const auto geometry=m_window->geometry();
                if(edge=="left") return geometry.left()==available.left();
                if(edge=="right") return geometry.right()==available.right();
                if(edge=="top") return geometry.top()==available.top();
                return geometry.bottom()==available.bottom();
            };
            QTRY_VERIFY(aligned());
            m_controller->show(); settle();
            QVERIFY(m_controller->focusPanel());
            QCOMPARE(m_controller->dockEdge(),QString());
            QTRY_VERIFY(available.contains(m_window->geometry()));
            QTest::keyClick(m_window,Qt::Key_Escape);
            QTRY_VERIFY(!m_controller->expanded());
            QTRY_COMPARE(m_controller->dockEdge(),edge);
            QTRY_COMPARE(m_window->width(),vertical ? 38 : 112);
            QTRY_COMPARE(m_window->height(),vertical ? 112 : 38);
            QTRY_VERIFY(aligned());
        }
        m_controller->dock(""); settle();
        QTRY_COMPARE(m_controller->dockEdge(),QString());
        QTRY_COMPARE(m_window->width(),216);
        QTRY_COMPARE(m_window->height(),76);
    }
    void edgeHoverPeekCancelsAndPromotesTheSameClick() {
        m_controller->acceptDrop({},"Select this after hovering","");
        QTRY_COMPARE(m_controller->count(),1);
        m_controller->dock("left"); settle();
        QTest::mouseMove(m_window,QPoint(m_window->width()/2,m_window->height()/2));
        QTest::qWait(100); QVERIFY(!m_controller->expanded());
        QTest::mouseMove(m_window,QPoint(-20,-20));
        QTest::qWait(240); QVERIFY(!m_controller->expanded());
        QTest::mouseMove(m_window,QPoint(m_window->width()/2,m_window->height()/2));
        QTRY_VERIFY_WITH_TIMEOUT(m_controller->expanded(),1200);
        QVERIFY(!m_controller->focusPanel());
        QVERIFY(m_window->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
        clickItem(0);
        QTRY_VERIFY(m_controller->focusPanel());
        QVERIFY(m_controller->selected(m_controller->items()[0].toMap()["id"].toString()));
        QVERIFY(!m_window->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
        m_controller->collapse(); settle();
        QTest::mouseMove(m_window,QPoint(-20,-20));
        // Hover delivery is synchronized with a frame. Give the real leave
        // a frame before re-entering, rather than coalescing both positions.
        settle();
        QTest::mouseMove(m_window,QPoint(m_window->width()/2,m_window->height()/2));
        QTRY_VERIFY_WITH_TIMEOUT(m_controller->expanded(),1200);
        QTest::mouseMove(m_window,QPoint(-20,-20));
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->expanded(),1200);
        QCOMPARE(m_controller->dockEdge(),QString("left"));
    }
    void nativeDragHoverPeeksWithoutTakingFocus() {
        const QString path=writeFile("hover.txt"); QVERIFY(!path.isEmpty());
        m_controller->dock("left"); settle();
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(path)});
        QDragEnterEvent enter(QPoint(19,55),Qt::CopyAction,&mime,Qt::LeftButton,{});
        QApplication::sendEvent(m_window,&enter); QVERIFY(enter.isAccepted());
        QTRY_VERIFY_WITH_TIMEOUT(m_controller->expanded(),1200);
        QVERIFY(!m_controller->focusPanel());
        QVERIFY(m_window->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
        QDragLeaveEvent leave; QApplication::sendEvent(m_window,&leave);
        // Not (-20,-20): the previous case ends there, and Qt drops a mouse
        // move to the position it already has, so no hover exit would arrive.
        QTest::mouseMove(m_window,QPoint(-24,-24));
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->expanded(),1200);
        QCOMPARE(m_controller->dockEdge(),QString("left"));
    }
    void rawImageDragHoverPeeksAndImportsWithoutTakingFocus() {
        m_controller->dock("left"); settle();
        QImage image(12,10,QImage::Format_ARGB32); image.fill(QColor("#bdadfc"));
        QMimeData mime; mime.setImageData(image);
        QDragEnterEvent enter(QPoint(19,55),Qt::CopyAction,&mime,Qt::LeftButton,{});
        QApplication::sendEvent(m_window,&enter); QVERIFY(enter.isAccepted());
        QVERIFY(m_controller->nativeDragHover());
        QTRY_VERIFY_WITH_TIMEOUT(m_controller->expanded(),1200);
        QVERIFY(!m_controller->focusPanel());
        QVERIFY(m_window->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
        QDropEvent drop(QPointF(40,180),Qt::CopyAction,&mime,Qt::LeftButton,{});
        QApplication::sendEvent(m_window,&drop); QVERIFY(drop.isAccepted());
        QVERIFY(!m_controller->nativeDragHover());
        QTRY_COMPARE(m_controller->count(),1);
        const auto imported=m_controller->items().first().toMap();
        QVERIFY(imported["owned"].toBool());
        QVERIFY(QFile::exists(imported["path"].toString()));
        const QImage recovered(imported["path"].toString());
        QCOMPARE(recovered.size(),image.size());
        QCOMPARE(recovered.pixelColor(3,3),image.pixelColor(3,3));
        QVERIFY(!m_controller->focusPanel());
    }
    void noninteractiveAddsPreserveAnotherFullscreenWindowsFocus() {
        QQuickWindow other;
        other.setTitle("Fullscreen focus probe");
        other.setColor(Qt::black);
        other.showFullScreen();
        other.requestActivate();
        QTRY_COMPARE(QGuiApplication::focusWindow(),static_cast<QWindow *>(&other));
        QVERIFY(m_window->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
        m_controller->acceptDrop({},"Added while another window is fullscreen","");
        QTRY_COMPARE(m_controller->count(),1);
        settle();
        QCOMPARE(QGuiApplication::focusWindow(),static_cast<QWindow *>(&other));
        QVERIFY(!m_controller->expanded());
        QVERIFY(!m_controller->focusPanel());
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
    void untrustedMarkupStaysLiteralWithoutImageRequests() {
        QTcpServer receiver;
        QVERIFY(receiver.listen(QHostAddress::LocalHost,0));
        QSignalSpy requests(&receiver,&QTcpServer::newConnection);
        const QString url=QStringLiteral("http://127.0.0.1:%1/probe.png").arg(receiver.serverPort());
        const QString markup=QStringLiteral("<img src=\"%1\"><b>Keep this literal</b>").arg(url);
        const QString name=QStringLiteral("<img src=\"%1\">").arg(url);
        m_controller->renameShelf("quick",name);
        QTRY_COMPARE(m_controller->activeName(),name);
        m_controller->acceptDrop({},markup,"");
        QTRY_COMPARE(m_controller->count(),1);
        m_controller->show();
        settle();
        const QString id=m_controller->items()[0].toMap()["id"].toString();
        auto *preview=find("previewText-"+id);
        QVERIFY(preview); QVERIFY(preview->isVisible());
        QCOMPARE(preview->property("text").toString(),markup);
        QTest::qWait(400);
        QCOMPARE(requests.count(),0);
        // The collection name also appears in the capsule and its success
        // tooltip. Hover through the tooltip delay to exercise that surface.
        m_controller->collapse(); settle();
        QTest::mouseMove(m_window,QPoint(m_window->width()/2,m_window->height()/2));
        QTest::qWait(700);
        QCOMPARE(requests.count(),0);
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
