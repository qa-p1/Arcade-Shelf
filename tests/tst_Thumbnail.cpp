#include "ThumbnailProvider.h"
#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QElapsedTimer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QUrl>
#include <memory>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

using Shelf::ThumbnailProvider;

namespace {
// The engine owns this adapter; the fixture owns the real provider so its
// bounded cache can be exercised across separate cache:false image requests.
class BorrowedProvider final : public QQuickAsyncImageProvider {
public:
    explicit BorrowedProvider(ThumbnailProvider &provider) : m_provider(provider) {}
    QQuickImageResponse *requestImageResponse(const QString &id,const QSize &size) override {
        return m_provider.requestImageResponse(id,size);
    }
private:
    ThumbnailProvider &m_provider;
};
}

class ThumbnailTest final : public QObject {
    Q_OBJECT
private:
    static QString encoded(const QString &path) {
        return QString::fromLatin1(QUrl::toPercentEncoding(path));
    }
    static bool read(ThumbnailProvider &provider, const QString &path, QSize size,
                     QImage *image, QString *error = nullptr) {
        QQmlEngine engine;
        engine.setOutputWarningsToStandardError(false);
        engine.addImageProvider("thumb",new BorrowedProvider(provider));
        engine.rootContext()->setContextProperty("testThumbnailSource","image://thumb/"+encoded(path));
        engine.rootContext()->setContextProperty("testThumbnailSize",size);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Window
            Window {
                width: 640; height: 480; visible: true; color: "transparent"
                Image {
                    objectName: "thumbnail"
                    source: testThumbnailSource; sourceSize: testThumbnailSize
                    asynchronous: true; cache: false
                }
            }
        )",QUrl("qrc:/ThumbnailTest.qml"));
        std::unique_ptr<QObject> root(component.create());
        auto *window=qobject_cast<QQuickWindow *>(root.get());
        auto *item=root ? root->findChild<QQuickItem *>("thumbnail") : nullptr;
        if (!window || !item) { qWarning() << component.errors(); return false; }
        // Qt's image loader checks its synchronized response completion flag,
        // including a cache hit that finishes before the engine connects.
        QElapsedTimer deadline; deadline.start();
        while (item->property("status").toInt()!=1 && item->property("status").toInt()!=3 && deadline.elapsed()<5000)
            QTest::qWait(1);
        if (item->property("status").toInt()==3) {
            *image={}; if (error) *error="Thumbnail unavailable"; return true;
        }
        if (item->property("status").toInt()!=1 || !QTest::qWaitForWindowExposed(window,5000)) return false;
        // grabWindow() synchronizes and renders the current scene itself.
        // Waiting for frameSwapped after requestUpdate() was racy: a window
        // with nothing left to repaint may not swap again.
        const QImage rendered=window->grabWindow();
        if (rendered.isNull()) return false;
        *image=rendered.copy(0,0,qRound(item->implicitWidth()),qRound(item->implicitHeight()));
        if (error) error->clear();
        return true;
    }
    static bool saveBmp(const QString &path, QColor color, QSize size = {900,600},
                        QDateTime modified = {}) {
        QImage image(size,QImage::Format_RGB32);
        image.fill(color);
        if (!image.save(path,"BMP")) return false;
        if (!modified.isValid()) return true;
        QFile file(path);
        return file.open(QIODevice::ReadWrite) && file.setFileTime(modified,QFileDevice::FileModificationTime);
    }
private slots:
    void initTestCase() {
        QImageReader::setAllocationLimit(64);
    }
    void actualEncodedFilesHaveBoundedAspectCorrectThumbnails_data() {
        QTest::addColumn<QSize>("requested");
        QTest::addColumn<QSize>("expected");
        QTest::newRow("ordinary") << QSize(256,180) << QSize(256,170);
        QTest::newRow("oversized") << QSize(30000,30000) << QSize(640,426);
        QTest::newRow("unspecified") << QSize(-1,-1) << QSize(320,213);
        QTest::newRow("zero") << QSize(0,0) << QSize(320,213);
        QTest::newRow("empty-width") << QSize(0,180) << QSize(320,213);
    }
    void actualEncodedFilesHaveBoundedAspectCorrectThumbnails() {
        QFETCH(QSize,requested); QFETCH(QSize,expected);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString path=directory.filePath(QStringLiteral("résumé #50% image.bmp"));
        QVERIFY(saveBmp(path,Qt::red));
        const auto originalSize=QFileInfo(path).size();
        ThumbnailProvider provider;
        QImage image; QString error;
        QVERIFY(read(provider,path,requested,&image,&error));
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(image.size(),expected);
        QCOMPARE(image.pixelColor(image.width()/2,image.height()/2),QColor(Qt::red));
        QCOMPARE(QFileInfo(path).size(),originalSize);
        QImage original(path); QCOMPARE(original.size(),QSize(900,600));
    }
    void unavailableAndMalformedInputsFinishWithoutTextures() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString malformed=directory.filePath("bad.png");
        QFile file(malformed); QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("This is not an image"),qint64(20)); file.close();
        ThumbnailProvider provider;
        for (const QString &path : {directory.filePath("missing.png"),malformed,directory.path()}) {
            QImage image; QString error;
            QVERIFY(read(provider,path,{256,180},&image,&error));
            QVERIFY(image.isNull()); QVERIFY(!error.isEmpty());
        }
    }
    void pixelBombHeaderIsRejectedBeforeDecode() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString path=directory.filePath("bomb.bmp");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("BM",2);
        stream << quint32(54) << quint16(0) << quint16(0) << quint32(54);
        stream << quint32(40) << qint32(20000) << qint32(10000) << quint16(1) << quint16(24);
        stream << quint32(0) << quint32(0) << qint32(0) << qint32(0) << quint32(0) << quint32(0);
        file.close();
        QImageReader header(path); QCOMPARE(header.size(),QSize(20000,10000));
        ThumbnailProvider provider;
        QImage image; QString error;
        QVERIFY(read(provider,path,{256,180},&image,&error));
        QVERIFY(image.isNull()); QVERIFY(!error.isEmpty());
    }
    void zeroQuotaDisablesAndClearsProviderCache() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString path=directory.filePath("mutable.bmp");
        const QDateTime modified=QDateTime::fromMSecsSinceEpoch(1700000000000,QTimeZone::UTC);
        QVERIFY(saveBmp(path,Qt::red,{900,600},modified));
        const auto bytes=QFileInfo(path).size();
        ThumbnailProvider provider;
        provider.setCacheLimitBytes(8*1024*1024);
        QImage image;
        QVERIFY(read(provider,path,{256,180},&image));
        QCOMPARE(image.pixelColor(0,0),QColor(Qt::red));
        // Keep identity metadata unchanged to distinguish a cache hit from a
        // decoder read. This fixture never relies on timestamp resolution.
        QVERIFY(saveBmp(path,Qt::blue,{900,600},modified));
        QCOMPARE(QFileInfo(path).size(),bytes);
        QCOMPARE(QFileInfo(path).lastModified().toMSecsSinceEpoch(),modified.toMSecsSinceEpoch());
        QVERIFY(read(provider,path,{256,180},&image));
        QCOMPARE(image.pixelColor(0,0),QColor(Qt::red));
        provider.setCacheLimitBytes(0);
        QVERIFY(read(provider,path,{256,180},&image));
        QCOMPARE(image.pixelColor(0,0),QColor(Qt::blue));
        QVERIFY(saveBmp(path,Qt::green,{900,600},modified));
        QVERIFY(read(provider,path,{256,180},&image));
        QCOMPARE(image.pixelColor(0,0),QColor(Qt::green));
    }
    void quotaEvictsOldestDecodedImage() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QDateTime modified=QDateTime::fromMSecsSinceEpoch(1700000000000,QTimeZone::UTC);
        const QString first=directory.filePath("first.bmp"), second=directory.filePath("second.bmp"), third=directory.filePath("third.bmp");
        QVERIFY(saveBmp(first,Qt::red,{900,600},modified));
        QVERIFY(saveBmp(second,Qt::blue,{900,600},modified));
        QVERIFY(saveBmp(third,Qt::green,{900,600},modified));
        ThumbnailProvider provider;
        QImage image;
        QVERIFY(read(provider,first,{256,180},&image));
        const qint64 cost=((image.sizeInBytes()+1023)/1024)*1024;
        provider.setCacheLimitBytes(2*cost);
        QVERIFY(read(provider,second,{256,180},&image));
        QVERIFY(read(provider,third,{256,180},&image));
        // A cache holding all three buffers would incorrectly return red after
        // this identity-preserving replacement. The oldest entry must be gone.
        QVERIFY(saveBmp(first,Qt::cyan,{900,600},modified));
        QVERIFY(read(provider,first,{256,180},&image));
        QCOMPARE(image.pixelColor(0,0),QColor(Qt::cyan));
    }
    void pendingRequestsFinishDuringProviderShutdown() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString path=directory.filePath("queued.bmp");
        QVERIFY(saveBmp(path,Qt::yellow,{2000,1500}));
        auto provider=std::make_unique<ThumbnailProvider>();
        QList<QQuickImageResponse *> responses;
        for (int i=0;i<24;++i) {
            auto *response=provider->requestImageResponse(encoded(path),{320+i,240});
            if (i>=2) response->cancel();
            responses << response;
        }
        provider.reset(); // Must drain workers without dangling cache/provider access.
        // Completed responses remain valid until the engine releases them;
        // waiting on the pool must also finish the uncancelled decode work.
        for (int i=0;i<2;++i) {
            std::unique_ptr<QQuickTextureFactory> texture(responses[i]->textureFactory());
            QVERIFY(texture); QVERIFY(!texture->image().isNull());
            QVERIFY(texture->textureSize().width()<=320+i);
            QVERIFY(texture->textureSize().height()<=240);
        }
        for (auto *response : responses) delete response;
    }
    void fifoSourcesDoNotBlockAWorker() {
#ifdef Q_OS_UNIX
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString path=directory.filePath("replaced.png");
        QCOMPARE(::mkfifo(QFile::encodeName(path).constData(),0600),0);
        ThumbnailProvider provider;
        QImage image; QString error;
        QVERIFY(read(provider,path,{256,180},&image,&error));
        QVERIFY(image.isNull()); QVERIFY(!error.isEmpty());
#else
        QSKIP("POSIX FIFO regression is platform-specific");
#endif
    }
};

QTEST_MAIN(ThumbnailTest)
#include "tst_Thumbnail.moc"
