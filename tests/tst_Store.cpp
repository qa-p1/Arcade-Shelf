#include "core/Store.h"
#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#ifdef Q_OS_UNIX
#include <atomic>
#include <cstdio>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace Shelf;

namespace {
QString writeFile(const QString &path,const QByteArray &bytes = QByteArray("original"))
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path); if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) return {};
    f.close(); return path;
}
QString longDirectory(QString path,int targetBytes = 930)
{
    int level = 0;
    while (path.toUtf8().size() < targetBytes) {
        const int length = qMin(180,targetBytes - static_cast<int>(path.toUtf8().size()) - 1);
        if (length <= 0) break;
        path += "/" + QString(length,QChar('a' + (level++ % 26)));
        if (!QDir().mkdir(path)) return {};
    }
    return path;
}
QJsonObject file(const QString &path,const QString &type = QStringLiteral("file/any"))
{
    return {{QStringLiteral("type"),type},{QStringLiteral("path"),path}};
}
QJsonObject text(const QString &value,const QString &type = QStringLiteral("text/plain"))
{
    return {{QStringLiteral("type"),type},{QStringLiteral("text"),value}};
}
bool setDatabaseValue(const QString &root,const QString &sql)
{
    const QString name = QStringLiteral("test-query");
    bool ok;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),name); db.setDatabaseName(root + QStringLiteral("/shelf.sqlite3"));
        ok = db.open(); if (ok) { QSqlQuery q(db); ok = q.exec(sql); } db.close();
    }
    QSqlDatabase::removeDatabase(name); return ok;
}

#ifdef Q_OS_UNIX
int unsafeStagingProbe()
{
    QTemporaryDir d; if (!d.isValid()) return 10;
    qputenv("ARCADE_HOME",(d.path() + "/arcade").toUtf8());
    const auto handoff = ArcadeLink::Locations::discover().handoff;
    if (!ArcadeLink::ensurePrivateDir(handoff)) return 11;
    const auto foreign = handoff + "/" + QUuid::createUuid().toString(QUuid::Id128);
    if (!ArcadeLink::ensurePrivateDir(foreign) || ::mkfifo(QFile::encodeName(foreign + "/.arcade-shelf").constData(),0600) != 0) return 12;
    Store store(d.path() + "/store"); if (!store.open()) return 13;
    const auto sourceDirectory = handoff + "/" + QUuid::createUuid().toString(QUuid::Id128);
    if (!ArcadeLink::ensurePrivateDir(sourceDirectory)) return 14;
    const auto path = writeFile(sourceDirectory + "/text.txt",QByteArray(300 * 1024,'r'));
    const auto added = store.add({QJsonObject{{"type","text/plain"},{"path",path},{"owner","arcade.clipboard"}}});
    if (added.error.isError()) return 15;
    ArcadeLink::Error error; const auto selected = store.exportItems(added.addedIds,{},true,&error);
    if (error.isError()) return 16;
    const auto output = selected.first().toObject()["path"].toString();
    const auto marker = QFileInfo(output).absolutePath() + "/.arcade-shelf";
    if (!QFile::rename(marker,marker + ".saved") || ::mkfifo(QFile::encodeName(marker).constData(),0600) != 0) return 17;
    if (!store.exportItems(added.addedIds,{},true,&error).isEmpty() || !error.isError()) return 18;
    if (!QFile::remove(marker) || !QFile::rename(marker + ".saved",marker)) return 19;

    // Bound this whole probe in the parent process so an unsafe FIFO open
    // fails the test rather than hanging the runner.
    const auto changing = writeFile(sourceDirectory + "/changing.png",QByteArray(128 * 1024,'i'));
    const auto alternate = sourceDirectory + "/alternate.png", temporary = sourceDirectory + "/swap";
    if (::mkfifo(QFile::encodeName(alternate).constData(),0600) != 0) return 20;
    const auto a = QFile::encodeName(changing), b = QFile::encodeName(alternate), t = QFile::encodeName(temporary);
    std::atomic_bool stop = false;
    std::thread peer([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            if (::rename(a.constData(),t.constData()) == 0) {
                if (::rename(b.constData(),a.constData()) == 0) ::rename(t.constData(),b.constData());
                else ::rename(t.constData(),a.constData());
            }
        }
    });
    auto input = file(changing,"file/image"); input.insert("owner","arcade.lens");
    QJsonArray inputs; for (int i=0;i<200;++i) inputs.append(input);
    const auto result = store.add(inputs);
    stop.store(true,std::memory_order_relaxed); peer.join();
    if (result.error.isError() && result.error.code != "unsupported_input") return 21;
    return 0;
}
#endif
}

class TestStore : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(m_ecosystem.isValid());
        m_oldHome = qgetenv("ARCADE_HOME"); qputenv("ARCADE_HOME",m_ecosystem.path().toUtf8());
        QVERIFY(ArcadeLink::ensurePrivateDir(ArcadeLink::Locations::discover().handoff));
    }
    void cleanupTestCase() { if (m_oldHome.isEmpty()) qunsetenv("ARCADE_HOME"); else qputenv("ARCADE_HOME",m_oldHome); }

    void fileArrayMixedFoldersAndPartialSuccess()
    {
        QTemporaryDir d; Store store(d.path() + "/store"); QString error; QVERIFY2(store.open(&error),qPrintable(error));
        const auto a = writeFile(d.path() + "/alpha.png"); const auto b = writeFile(d.path() + "/beta.pdf");
        const auto folder = d.path() + "/assets"; QVERIFY(QDir().mkpath(folder));
        auto r = store.add(QJsonArray{QJsonObject{{"type","file/any[]"},{"paths",QJsonArray{a,b,d.path() + "/missing"}}},file(folder,"folder/reference")});
        QVERIFY2(!r.error.isError(),qPrintable(r.error.message)); QCOMPARE(r.addedIds.size(),3);
        QCOMPARE(store.items("quick").size(),3);
        QCOMPARE(r.result["data"].toObject()["skipped"].toArray().size(),1);
        QCOMPARE(r.result["data"].toObject()["skipped"].toArray().first().toObject()["reason"].toString(),QString("missing"));
        QCOMPARE(r.result["message"].toString(),QString("Added 3 items to Quick Shelf · 1 skipped"));
        QCOMPARE(store.ownedBytes(),0);
        QCOMPARE(store.items("quick").first().toObject()["path"].toString(),a);
        QCOMPARE(store.items("quick").first().toObject()["type"].toString(),QString("file/image"));
        QCOMPARE(store.items("quick").first().toObject()["owned"].toBool(),false);
    }

    void duplicatesAreSuccessfulAndScopedToShelf()
    {
        QTemporaryDir d; Store store(d.path() + "/store"); QVERIFY(store.open()); const auto p = writeFile(d.path() + "/file.txt");
        auto r = store.add({file(p),file(p)}); QVERIFY(!r.error.isError()); QCOMPARE(r.addedIds.size(),1);
        QCOMPARE(r.result["message"].toString(),QString("Added 1 item to Quick Shelf · 1 already there"));
        r = store.add({file(p)}); QVERIFY(!r.error.isError()); QVERIFY(r.addedIds.isEmpty()); QCOMPARE(store.items("quick").size(),1);
        const auto shelf = store.createShelf("Project"); QVERIFY(!shelf.isEmpty());
        r = store.add({file(p)},{{"shelf",shelf}}); QVERIFY(!r.error.isError()); QCOMPARE(r.addedIds.size(),1);
    }

    void malformedInputAndCountBounds()
    {
        QTemporaryDir d; Store store(d.path() + "/store"); QVERIFY(store.open());
        QCOMPARE(store.add({}).error.code,QString("unsupported_input"));
        QCOMPARE(store.add({file("relative.txt")}).error.code,QString("unsupported_input"));
        QCOMPARE(store.add({file(d.path() + "/missing")}).error.code,QString("unsupported_input"));
        const auto pdf = writeFile(d.path() + "/brief.pdf");
        QCOMPARE(store.add({file(pdf,"file/image")}).error.code,QString("unsupported_input"));
        QCOMPARE(store.add({file(pdf,"folder/reference")}).error.code,QString("unsupported_input"));
        QCOMPARE(store.add({text(QString("bad") + QChar::Null + "text")}).error.code,QString("unsupported_input"));
        QCOMPARE(store.add({text("javascript:alert(1)","text/url")}).error.code,QString("unsupported_input"));
        QCOMPARE(store.add({QJsonObject{{"type","file/any[]"},{"paths",QJsonArray{123,QString()}}}}).error.code,QString("unsupported_input"));
        QCOMPARE(store.add({file(pdf)},{{"shelf","missing"}}).error.code,QString("unsupported_input"));
        QCOMPARE(store.add({file(pdf)},{{"reveal","yes"}}).error.code,QString("unsupported_input"));
        QJsonArray paths; for (int i=0;i<1001;++i) paths.append(pdf);
        QCOMPARE(store.add({QJsonObject{{"type","file/any[]"},{"paths",paths}}}).error.code,QString("unsupported_input"));
        QVERIFY(store.items("quick").isEmpty());
    }

    void restartPersistenceAndNeverDeleteOriginal()
    {
        QTemporaryDir d; const QString root = d.path() + "/store", p = writeFile(d.path() + "/original.md"); QString id;
        { Store store(root); QVERIFY(store.open()); auto r = store.add({file(p)}); QVERIFY(!r.error.isError()); id = r.addedIds.first(); QVERIFY(store.saveSettings({{"theme","light"}})); }
        { Store store(root); QVERIFY(store.open()); QCOMPARE(store.items("quick").size(),1); QCOMPARE(store.settings()["theme"].toString(),QString("light")); QVERIFY(store.remove({id})); }
        QVERIFY(QFileInfo::exists(p)); QFile f(p); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(),QByteArray("original"));
        { Store store(root); QVERIFY(store.open()); QVERIFY(store.items("quick").isEmpty()); }
    }

    void collectionsReorderingAndRecovery()
    {
        QTemporaryDir d; const QString root = d.path() + "/store"; QString named;
        {
            Store store(root); QVERIFY(store.open()); named = store.createShelf("Design"); QVERIFY(!named.isEmpty());
            const auto temp = store.createShelf("Session",true); QVERIFY(!temp.isEmpty());
            QVERIFY(store.renameShelf(named,"Design assets")); QVERIFY(store.pinShelf(named,true)); QVERIFY(store.setActiveShelf(named));
            QVERIFY(store.reorderShelves({named,"quick",temp})); QCOMPARE(store.shelves().first().toObject()["id"].toString(),named);
            const auto a = writeFile(d.path() + "/one"), b = writeFile(d.path() + "/two");
            const auto r = store.add({file(a),file(b)}); QVERIFY(!r.error.isError());
            QVERIFY(store.reorder(named,{r.addedIds[1],r.addedIds[0]})); QCOMPARE(store.items(named).first().toObject()["path"].toString(),b);
            QVERIFY(!store.reorder(named,{r.addedIds[0],r.addedIds[0]})); QVERIFY(!store.renameShelf(named,QString()));
            QVERIFY(!store.deleteShelf("quick"));
        }
        Store store(root); QVERIFY(store.open()); QCOMPARE(store.activeShelf(),named); QCOMPARE(store.items(named).size(),2);
        QVERIFY(store.deleteShelf(named)); QCOMPARE(store.activeShelf(),QString("quick"));
        QVERIFY(QFileInfo::exists(d.path() + "/one")); QVERIFY(QFileInfo::exists(d.path() + "/two"));
    }

    void richTextUrlsAndQuota()
    {
        QTemporaryDir d; Store store(d.path() + "/store"); QVERIFY(store.open()); QVERIFY(store.saveSettings({{"quotaBytes",100}}));
        auto r = store.add({text("hello"),QJsonObject{{"type","text/rich"},{"text","bold"},{"html","<b>bold</b>"}},text("https://example.org","text/url")});
        QVERIFY(!r.error.isError()); QCOMPARE(r.addedIds.size(),3); QVERIFY(store.ownedBytes() > 0);
        ArcadeLink::Error error; auto output = store.exportItems({r.addedIds[1]}, {}, true, &error); QVERIFY(!error.isError());
        QCOMPARE(store.items("quick")[1].toObject()["html"].toString(),QString("<b>bold</b>"));
        QCOMPARE(output.first().toObject()["type"].toString(),QString("text/plain"));
        QCOMPARE(output.first().toObject()["text"].toString(),QString("bold"));
        r = store.add({text(QString(101,'x'))}); QCOMPARE(r.error.code,QString("too_large")); QCOMPARE(r.error.limit,100);
        const auto p = writeFile(d.path() + "/large.bin",QByteArray(1000,'x'));
        r = store.add({file(p),text(QString(101,'x'))}); QVERIFY(!r.error.isError()); QCOMPARE(r.addedIds.size(),1);
        QCOMPARE(store.items("quick").size(),4); QVERIFY(QFileInfo::exists(p));
        QVERIFY(!store.saveSettings({{"quotaBytes",-1}})); QVERIFY(!store.saveSettings({{"retentionDays",-1}}));
    }

    void largeTextHandoffCopiedBeforeSuccess()
    {
        QTemporaryDir d; Store store(d.path() + "/store"); QVERIFY(store.open());
        const auto handoff = ArcadeLink::Locations::discover().handoff + "/" + QUuid::createUuid().toString(QUuid::Id128);
        QVERIFY(ArcadeLink::ensurePrivateDir(handoff));
        const QByteArray bytes(300 * 1024,'z'); const auto original = writeFile(handoff + "/note.txt",bytes);
        auto r = store.add({QJsonObject{{"type","text/plain"},{"path",original},{"owner","arcade.clipboard"}}});
        QVERIFY2(!r.error.isError(),qPrintable(r.error.message)); QCOMPARE(r.addedIds.size(),1);
        const auto i = store.items("quick").first().toObject(); QVERIFY(i["owned"].toBool()); QVERIFY(i["path"].toString() != original);
        QVERIFY(i["text"].toString().size() <= 2048); QCOMPARE(store.ownedBytes(),static_cast<qint64>(bytes.size()));
        QVERIFY(QDir(handoff).removeRecursively());
        QFile staged(i["path"].toString()); QVERIFY(staged.open(QIODevice::ReadOnly)); QCOMPARE(staged.readAll(),bytes);
        staged.close();
        ArcadeLink::Error error; const auto out = store.exportItems(r.addedIds,{},true,&error);
        QVERIFY(!error.isError()); QCOMPARE(out.size(),1); const auto c = out.first().toObject(); QCOMPARE(c["owner"].toString(),QString("arcade.shelf"));
        QFile exported(c["path"].toString()); QVERIFY(exported.open(QIODevice::ReadOnly)); QCOMPARE(exported.readAll(),bytes);
        exported.close();
        QVERIFY(store.remove(r.addedIds)); QVERIFY(QFileInfo::exists(c["path"].toString()));
        QCOMPARE(store.ownedBytes(),0); QVERIFY(!QFileInfo::exists(i["path"].toString()));
        QVERIFY(QDir(QFileInfo(c["path"].toString()).absolutePath()).removeRecursively());
    }

    void handoffImagesAndSpoofedOwnership()
    {
        QTemporaryDir d; Store store(d.path() + "/store"); QVERIFY(store.open());
        const auto own = writeFile(d.path() + "/private.png"); auto claimed = file(own,"file/image"); claimed.insert("owner","arcade.lens");
        const auto reference = store.add({claimed}); QVERIFY(!reference.error.isError());
        const auto storedReference = store.items("quick").first().toObject();
        QCOMPARE(storedReference["path"].toString(),own); QVERIFY(!storedReference["owned"].toBool());
        QVERIFY(store.remove(reference.addedIds)); QVERIFY(QFileInfo::exists(own));
        const auto handoff = ArcadeLink::Locations::discover().handoff + "/" + QUuid::createUuid().toString(QUuid::Id128);
        QVERIFY(ArcadeLink::ensurePrivateDir(handoff)); const auto image = writeFile(handoff + "/capture.png",QByteArray("image bytes"));
        auto capture = file(image,"file/image"); capture.insert("owner","arcade.lens");
        const auto r = store.add({capture}); QVERIFY(!r.error.isError()); const auto staged = store.items("quick").first().toObject()["path"].toString();
        QVERIFY(staged != image); QVERIFY(QFileInfo::exists(staged));
        ArcadeLink::Error error; const auto output = store.exportItems(r.addedIds,{},true,&error); QVERIFY(!error.isError());
        const auto exported = output.first().toObject()["path"].toString(); QVERIFY(exported != staged);
        QCOMPARE(output.first().toObject()["owner"].toString(),QString("arcade.shelf"));
        const auto repeated = store.exportItems(r.addedIds,{},true,&error); QVERIFY(!error.isError()); QCOMPARE(repeated.first().toObject()["path"].toString(),exported);
        QVERIFY(store.remove(r.addedIds)); QVERIFY(QFileInfo::exists(exported));
        QFile exportFile(exported); QVERIFY(exportFile.open(QIODevice::ReadOnly)); QCOMPARE(exportFile.readAll(),QByteArray("image bytes"));
        exportFile.close();
        QVERIFY(!QFileInfo::exists(staged)); QVERIFY(QFileInfo::exists(image)); QVERIFY(QDir(handoff).removeRecursively());
        QVERIFY(QDir(QFileInfo(exported).absolutePath()).removeRecursively());
    }

    void malformedUtf8AndInlineBounds()
    {
        QTemporaryDir d; Store store(d.path() + "/store"); QVERIFY(store.open());
        const auto inlineError = store.add({text(QString(256 * 1024 + 1,'x'))}).error;
        QCOMPARE(inlineError.code,QString("too_large")); QCOMPARE(inlineError.limit,static_cast<qint64>(256 * 1024));
        const auto handoff = ArcadeLink::Locations::discover().handoff + "/" + QUuid::createUuid().toString(QUuid::Id128);
        QVERIFY(ArcadeLink::ensurePrivateDir(handoff)); const auto p = writeFile(handoff + "/bad.txt",QByteArray::fromHex("c328"));
        QCOMPARE(store.add({QJsonObject{{"type","text/plain"},{"path",p},{"owner","arcade.clipboard"}}}).error.code,QString("unsupported_input"));
        QVERIFY(store.items("quick").isEmpty()); QVERIFY(QDir(handoff).removeRecursively());
    }

    void missingReferenceAndExplicitPickFiltering()
    {
        QTemporaryDir d; Store store(d.path() + "/store"); QVERIFY(store.open());
        const auto image = writeFile(d.path() + "/image.png"), pdf = writeFile(d.path() + "/book.pdf");
        auto r = store.add({file(image,"file/image"),file(pdf,"file/pdf"),text("private note")}); QVERIFY(!r.error.isError());
        ArcadeLink::Error error; auto out = store.exportItems({r.addedIds[0]}, {"file/image"}, false, &error);
        QVERIFY(!error.isError()); QCOMPARE(out.size(),1); QCOMPARE(out.first().toObject()["path"].toString(),image);
        out = store.exportItems({r.addedIds[0],r.addedIds[1]}, {"file/*[]"}, true, &error); QVERIFY(!error.isError());
        QCOMPARE(out.first().toObject()["type"].toString(),QString("file/any[]")); QCOMPARE(out.first().toObject()["paths"].toArray().size(),2);
        QCOMPARE(out.first().toObject()["size"].toInteger(),static_cast<qint64>(16));
        out = store.exportItems({r.addedIds[0],r.addedIds[1]}, {}, false, &error); QCOMPARE(error.code,QString("unsupported_input")); QVERIFY(out.isEmpty());
        out = store.exportItems({r.addedIds[0]}, {"text/plain"}, true, &error); QCOMPARE(error.code,QString("unsupported_input")); QVERIFY(out.isEmpty());
        QVERIFY(QFile::remove(image)); QVERIFY(!store.items("quick").first().toObject()["available"].toBool());
        out = store.exportItems({r.addedIds[0]}, {}, true, &error); QCOMPARE(error.code,QString("unsupported_input")); QVERIFY(out.isEmpty());
        QCOMPARE(store.items("quick").size(),3);
    }

    void futureSchemaAndCorruptDatabaseRejected()
    {
        QTemporaryDir d; const auto root = d.path() + "/store";
        { Store store(root); QVERIFY(store.open()); }
        QVERIFY(setDatabaseValue(root,"PRAGMA user_version=999"));
        { Store store(root); QString error; QVERIFY(!store.open(&error)); QVERIFY(error.contains("newer version")); }
        const auto badRoot = d.path() + "/bad"; QVERIFY(QDir().mkpath(badRoot)); QVERIFY(!writeFile(badRoot + "/shelf.sqlite3","not sqlite").isEmpty());
        { Store store(badRoot); QVERIFY(!store.open()); }
    }

    void startupRecoversOrphansAndRetentionHonorsPins()
    {
        QTemporaryDir d; const auto root = d.path() + "/store"; QString pin;
        {
            Store store(root); QVERIFY(store.open()); QVERIFY(store.saveSettings({{"retentionDays",1}}));
            pin = store.createShelf("Pinned",true); QVERIFY(store.pinShelf(pin,true));
            QVERIFY(!store.add({text("expiring")}).error.isError()); QVERIFY(!store.add({text("preserved")},{{"shelf",pin}}).error.isError());
        }
        QVERIFY(setDatabaseValue(root,"UPDATE items SET created=0"));
        const auto orphan = root + "/objects/it_0123456789abcdef0123456789abcdef/orphan";
        QVERIFY(!writeFile(orphan).isEmpty()); const auto unrelated = writeFile(root + "/objects/not-a-shelf-id/keep");
        Store store(root); QVERIFY(store.open()); QVERIFY(store.items("quick").isEmpty()); QCOMPARE(store.items(pin).size(),1);
        QVERIFY(!QFileInfo::exists(orphan)); QVERIFY(QFileInfo::exists(unrelated));
    }

    void largeReferenceCollectionAndBoundedPick()
    {
        QTemporaryDir d; Store store(d.path() + "/store"); QVERIFY(store.open());
        QStringList ids;
        for (int batch=0;batch<10;++batch) {
            QJsonArray paths;
            for (int i=0;i<1000;++i) {
                const auto p = writeFile(d.path() + "/files/item_" + QString::number(batch * 1000 + i) + ".txt",QByteArray("x"));
                QVERIFY(!p.isEmpty()); paths.append(p);
            }
            const auto r = store.add({QJsonObject{{"type","file/text[]"},{"paths",paths}}});
            QVERIFY(!r.error.isError()); QCOMPARE(r.addedIds.size(),1000); ids.append(r.addedIds);
        }
        QCOMPARE(store.items("quick").size(),10000); QCOMPARE(store.ownedBytes(),0);
        ArcadeLink::Error error;
        auto selected = store.exportItems(ids.mid(2000,1000),{"file/*[]"},true,&error);
        QVERIFY(!error.isError()); QCOMPARE(selected.size(),1); QCOMPARE(selected.first().toObject()["paths"].toArray().size(),1000);
        selected = store.exportItems(ids,{},true,&error); QCOMPARE(error.code,QString("unsupported_input")); QVERIFY(selected.isEmpty());
        QVERIFY(store.remove(ids.mid(0,1000))); QCOMPARE(store.items("quick").size(),9000);
        QVERIFY(QFileInfo::exists(d.path() + "/files/item_0.txt"));
    }

    void snapshotPreviewsAndAggregateWireBudget()
    {
        QTemporaryDir d; const auto root = d.path() + "/store";
        QStringList ids, outputs;
        {
            Store store(root); QVERIFY(store.open());
            QJsonArray content; for (int i=0;i<5;++i) content.append(text(QString(200 * 1024,QChar('a' + i))));
            const auto added = store.add(content); QVERIFY(!added.error.isError()); ids = added.addedIds; QCOMPARE(ids.size(),5);
            for (const auto &v : store.items("quick")) { QVERIFY(v.toObject()["textTruncated"].toBool()); QCOMPARE(v.toObject()["text"].toString().size(),2048); }
            ArcadeLink::Error error; const auto exported = store.exportItems(ids,{},true,&error);
            QVERIFY(!error.isError()); QCOMPARE(exported.size(),5);
            QVERIFY(QJsonDocument(QJsonObject{{"outputs",exported}}).toJson(QJsonDocument::Compact).size() <= 900 * 1024);
            for (int i=0;i<exported.size();++i) {
                const auto c = exported[i].toObject(); QString complete = c["text"].toString();
                if (!c["path"].toString().isEmpty()) { QFile f(c["path"].toString()); QVERIFY(f.open(QIODevice::ReadOnly)); complete = QString::fromUtf8(f.readAll()); outputs.append(c["path"].toString()); }
                QCOMPARE(complete,QString(200 * 1024,QChar('a' + i)));
            }
            QCOMPARE(outputs.size(),3);
            const auto again = store.exportItems(ids,{},true,&error); QVERIFY(!error.isError());
            int index = 0; for (const auto &v : again) if (v.toObject().contains("path")) QCOMPARE(v.toObject()["path"].toString(),outputs[index++]);
        }
        Store store(root); QVERIFY(store.open()); ArcadeLink::Error error;
        const auto again = store.exportItems(ids,{},true,&error); QVERIFY(!error.isError());
        int index = 0; for (const auto &v : again) if (v.toObject().contains("path")) QCOMPARE(v.toObject()["path"].toString(),outputs[index++]);
        const auto peerDirectory = ArcadeLink::Locations::discover().handoff + "/" + QUuid::createUuid().toString(QUuid::Id128);
        const auto peerFile = writeFile(peerDirectory + "/peer.txt","peer owns this"); QVERIFY(!peerFile.isEmpty());
        for (const auto &path : outputs) {
            QFile marker(QFileInfo(path).absolutePath() + "/.arcade-shelf"); QVERIFY(marker.open(QIODevice::ReadWrite));
            QVERIFY(marker.setFileTime(QDateTime::currentDateTimeUtc().addDays(-2),QFileDevice::FileModificationTime));
        }
        QVERIFY(store.cleanup()); for (const auto &path : outputs) QVERIFY(!QFileInfo::exists(path));
        QVERIFY(QFileInfo::exists(peerFile)); QVERIFY(QDir(peerDirectory).removeRecursively());
    }

    void handoffQuotaAndOversizedReferenceEnvelope()
    {
        QTemporaryDir d; Store store(d.path() + "/store"); QVERIFY(store.open());
        const auto handoff = ArcadeLink::Locations::discover().handoff + "/" + QUuid::createUuid().toString(QUuid::Id128);
        QVERIFY(ArcadeLink::ensurePrivateDir(handoff));
        const auto p = writeFile(handoff + "/large.txt",QByteArray(300 * 1024,'x'));
        const auto r = store.add({QJsonObject{{"type","text/plain"},{"path",p},{"owner","arcade.clipboard"}}}); QVERIFY(!r.error.isError());
        QVERIFY(store.saveSettings({{"quotaBytes",100}})); ArcadeLink::Error error;
        QVERIFY(store.exportItems(r.addedIds,{},true,&error).isEmpty()); QCOMPARE(error.code,QString("too_large")); QCOMPARE(error.limit,100);
        QVERIFY(QDir(handoff).removeRecursively());
        // OS path limits are lower than the Link's JSON limit; valid deeply nested
        // references still need an aggregate envelope bound across 1000 values.
#ifdef Q_OS_UNIX
        const QString directory = longDirectory(d.path()); QVERIFY(!directory.isEmpty());
        QJsonArray paths;
        for (int i=0;i<1000;++i) { const auto path = writeFile(directory + "/item_" + QString::number(i)); QVERIFY(!path.isEmpty()); paths.append(path); }
        QStringList referenceIds;
        for (int offset=0;offset<1000;offset+=500) {
            QJsonArray batch; for (int i=offset;i<offset+500;++i) batch.append(paths[i]);
            const auto refs = store.add({QJsonObject{{"type","file/any[]"},{"paths",batch}}}); QVERIFY(!refs.error.isError()); referenceIds.append(refs.addedIds);
        }
        QVERIFY(store.exportItems(referenceIds,{},true,&error).isEmpty()); QCOMPARE(error.code,QString("too_large")); QCOMPARE(error.limit,static_cast<qint64>(ArcadeLink::MaxLineBytes));
#endif
    }

#ifdef Q_OS_UNIX
    void nonRegularAndRacingHandoffsNeverBlock()
    {
        QProcess probe;
        probe.start(QCoreApplication::applicationFilePath(),{"--unsafe-staging-probe"});
        QVERIFY(probe.waitForStarted(3000));
        if (!probe.waitForFinished(10000)) { probe.kill(); probe.waitForFinished(); QFAIL("Owned staging blocked on a non-regular or concurrently replaced handoff"); }
        QCOMPARE(probe.exitStatus(),QProcess::NormalExit);
        QVERIFY2(probe.exitCode() == 0,qPrintable(QString("Staging probe exit %1: %2").arg(probe.exitCode()).arg(QString::fromUtf8(probe.readAllStandardError()))));
    }

    void swappedAncestorAndDirectoryIdentityAreRejected()
    {
        QTemporaryDir d; const auto ancestor = d.path() + "/ancestor"; const auto root = ancestor + "/store";
        Store store(root); QVERIFY(store.open());
        const auto external = d.path() + "/external-ancestor";
        QVERIFY(ArcadeLink::ensurePrivateDir(external + "/store")); QVERIFY(ArcadeLink::ensurePrivateDir(external + "/store/objects"));
        const auto sentinel = writeFile(external + "/store/objects/it_0123456789abcdef0123456789abcdef/original","must survive"); QVERIFY(!sentinel.isEmpty());
        QVERIFY(QDir(d.path()).rename("ancestor","saved-ancestor"));
        QVERIFY(::symlink(QFile::encodeName(external).constData(),QFile::encodeName(ancestor).constData()) == 0);
        QVERIFY(!QFileInfo(root).isSymLink()); QVERIFY(!QFileInfo(root + "/objects").isSymLink());
        QVERIFY(!store.cleanup()); QVERIFY(QFileInfo::exists(sentinel));
        QCOMPARE(store.add({text("refused after ancestor swap")}).error.code,QString("internal"));
        QVERIFY(QFile::remove(ancestor)); QVERIFY(QDir(d.path()).rename("saved-ancestor","ancestor"));
        QVERIFY(store.cleanup());
        QVERIFY(QDir(root).rename("objects","saved-objects")); QVERIFY(ArcadeLink::ensurePrivateDir(root + "/objects"));
        const auto replacement = writeFile(root + "/objects/it_fedcba9876543210fedcba9876543210/keep","different directory"); QVERIFY(!replacement.isEmpty());
        QVERIFY(!store.cleanup()); QVERIFY(QFileInfo::exists(replacement));
        QVERIFY(QDir(root).rename("objects","replacement-objects")); QVERIFY(QDir(root).rename("saved-objects","objects"));
        QVERIFY(store.cleanup()); QVERIFY(QFileInfo::exists(root + "/replacement-objects/it_fedcba9876543210fedcba9876543210/keep"));
        QVERIFY(QFileInfo::exists(sentinel));
    }

    void addReplyBudgetRejectsBeforePersistence()
    {
        QTemporaryDir d; Store store(d.path() + "/store"); QVERIFY(store.open());
        const QString directory = longDirectory(d.path()); QVERIFY(!directory.isEmpty());
        QJsonArray paths;
        for (int i=0;i<1000;++i) { const auto path = writeFile(directory + "/item_" + QString::number(i)); QVERIFY(!path.isEmpty()); paths.append(path); }
        const QJsonArray inputs{QJsonObject{{"type","file/any[]"},{"paths",paths}}};
        const QJsonObject request{{"v",1},{"id",1},{"method","invoke"},{"params",QJsonObject{{"action","shelf.add"},{"version",1},{"inputs",inputs},{"options",QJsonObject{}}}}};
        QVERIFY(QJsonDocument(request).toJson(QJsonDocument::Compact).size() + 1 < ArcadeLink::MaxLineBytes);
        const auto result = store.add(inputs); QCOMPARE(result.error.code,QString("too_large")); QCOMPARE(result.error.limit,static_cast<qint64>(ArcadeLink::MaxLineBytes));
        QVERIFY(result.addedIds.isEmpty()); QVERIFY(store.items("quick").isEmpty());
        QVERIFY(QFileInfo::exists(paths.first().toString())); QVERIFY(QFileInfo::exists(paths.last().toString()));
        QJsonArray batch; for (int i=0;i<500;++i) batch.append(paths[i]);
        const auto allowed = store.add({QJsonObject{{"type","file/any[]"},{"paths",batch}}}); QVERIFY(!allowed.error.isError()); QCOMPARE(allowed.addedIds.size(),500);
        QVERIFY(QJsonDocument(allowed.result).toJson(QJsonDocument::Compact).size() < 900 * 1024);
    }

    void swappedStorageBoundariesNeverDeleteExternalFiles()
    {
        QTemporaryDir d; const QString root = d.path() + "/store"; Store store(root); QVERIFY(store.open());
        const auto collection = store.createShelf("Protected"); QVERIFY(!collection.isEmpty());
        const auto sourceDirectory = ArcadeLink::Locations::discover().handoff + "/" + QUuid::createUuid().toString(QUuid::Id128);
        QVERIFY(ArcadeLink::ensurePrivateDir(sourceDirectory)); const auto source = writeFile(sourceDirectory + "/capture.png","capture");
        auto content = file(source,"file/image"); content.insert("owner","arcade.lens");
        const auto added = store.add({content},{{"shelf",collection}}); QVERIFY(!added.error.isError()); QCOMPARE(added.addedIds.size(),1);
        const auto staged = store.items(collection).first().toObject()["path"].toString();
        const auto relative = QDir(root + "/objects").relativeFilePath(staged);
        const auto external = d.path() + "/external"; QVERIFY(ArcadeLink::ensurePrivateDir(external));
        const auto externalCopy = writeFile(external + "/" + relative,"external original"); QVERIFY(!externalCopy.isEmpty());
        const auto orphan = writeFile(external + "/it_0123456789abcdef0123456789abcdef/sentinel","unrelated original"); QVERIFY(!orphan.isEmpty());
        QVERIFY(QDir(root).rename("objects","objects-saved"));
        QVERIFY(::symlink(QFile::encodeName(external).constData(),QFile::encodeName(root + "/objects").constData()) == 0);
        QVERIFY(!store.cleanup()); QVERIFY(QFileInfo::exists(orphan)); QVERIFY(QFileInfo::exists(externalCopy));
        QVERIFY(!store.remove(added.addedIds)); QVERIFY(!store.deleteShelf(collection));
        QCOMPARE(store.add({text("must not be persisted")}).error.code,QString("internal"));
        ArcadeLink::Error error; QVERIFY(store.exportItems(added.addedIds,{},true,&error).isEmpty()); QVERIFY(error.isError());
        QCOMPARE(store.items(collection).size(),1); QVERIFY(QFileInfo::exists(externalCopy));
        QVERIFY(QFile::remove(root + "/objects")); QVERIFY(QDir(root).rename("objects-saved","objects"));
        QVERIFY(store.cleanup()); QVERIFY(QFileInfo::exists(staged));

        QVERIFY(QDir(d.path()).rename("store","saved-store"));
        const auto externalRoot = d.path() + "/external-root"; QVERIFY(ArcadeLink::ensurePrivateDir(externalRoot));
        QVERIFY(ArcadeLink::ensurePrivateDir(externalRoot + "/objects"));
        const auto rootSentinel = writeFile(externalRoot + "/objects/" + relative,"other original"); QVERIFY(!rootSentinel.isEmpty());
        QVERIFY(::symlink(QFile::encodeName(externalRoot).constData(),QFile::encodeName(root).constData()) == 0);
        QVERIFY(!store.remove(added.addedIds)); QVERIFY(!store.cleanup()); QVERIFY(QFileInfo::exists(rootSentinel));
        QCOMPARE(store.add({text("still refused")}).error.code,QString("internal"));
        QVERIFY(QFile::remove(root)); QVERIFY(QDir(d.path()).rename("saved-store","store"));
        QVERIFY(store.remove(added.addedIds)); QVERIFY(!QFileInfo::exists(staged));
        QVERIFY(QFileInfo::exists(rootSentinel)); QVERIFY(QFileInfo::exists(externalCopy)); QVERIFY(QFileInfo::exists(orphan));
        QVERIFY(QFileInfo::exists(source)); QVERIFY(QDir(sourceDirectory).removeRecursively());
    }

    void swappedOwnedTextAndHandoffAliasesAreRejected()
    {
        QTemporaryDir d; Store store(d.path() + "/store"); QVERIFY(store.open());
        const auto sourceDirectory = ArcadeLink::Locations::discover().handoff + "/" + QUuid::createUuid().toString(QUuid::Id128);
        QVERIFY(ArcadeLink::ensurePrivateDir(sourceDirectory));
        const auto original = writeFile(sourceDirectory + "/text.txt",QByteArray(300 * 1024,'x'));
        const auto added = store.add({QJsonObject{{"type","text/plain"},{"path",original},{"owner","arcade.clipboard"}}}); QVERIFY(!added.error.isError());
        const auto staged = store.items("quick").first().toObject()["path"].toString();
        const auto external = writeFile(d.path() + "/external.txt","external private content"); QVERIFY(!external.isEmpty());
        QVERIFY(QFile::rename(staged,staged + ".saved")); QVERIFY(::symlink(QFile::encodeName(external).constData(),QFile::encodeName(staged).constData()) == 0);
        ArcadeLink::Error error; QVERIFY(store.exportItems(added.addedIds,{},true,&error).isEmpty()); QVERIFY(error.isError());
        QVERIFY(QFileInfo::exists(external)); QVERIFY(QFile::remove(staged)); QVERIFY(QFile::rename(staged + ".saved",staged));
        auto output = store.exportItems(added.addedIds,{},true,&error); QVERIFY(!error.isError()); const auto exported = output.first().toObject()["path"].toString();
        const auto directory = QFileInfo(exported).absolutePath(); const auto savedDirectory = directory + "-saved";
        QFile marker(directory + "/.arcade-shelf"); QVERIFY(marker.open(QIODevice::ReadOnly)); const auto ownership = marker.readAll(); marker.close();
        const auto externalDirectory = d.path() + "/external-handoff"; QVERIFY(ArcadeLink::ensurePrivateDir(externalDirectory));
        const auto sentinel = writeFile(externalDirectory + "/text.txt","other app content"); QVERIFY(!sentinel.isEmpty());
        QVERIFY(!writeFile(externalDirectory + "/.arcade-shelf",ownership).isEmpty());
        QVERIFY(QDir().rename(directory,savedDirectory));
        QVERIFY(::symlink(QFile::encodeName(externalDirectory).constData(),QFile::encodeName(directory).constData()) == 0);
        QVERIFY(store.exportItems(added.addedIds,{},true,&error).isEmpty()); QCOMPARE(error.code,QString("internal"));
        QVERIFY(QFileInfo::exists(sentinel));
        QVERIFY(QFile::remove(directory)); QVERIFY(QDir().rename(savedDirectory,directory));
        output = store.exportItems(added.addedIds,{},true,&error); QVERIFY(!error.isError()); QCOMPARE(output.first().toObject()["path"].toString(),exported);
        QVERIFY(QDir(directory).removeRecursively()); QVERIFY(QDir(sourceDirectory).removeRecursively());
    }

    void reusedHandoffsRequireRenewableOwnershipMarker()
    {
        QTemporaryDir d; Store store(d.path() + "/store"); QVERIFY(store.open());
        const auto sourceDirectory = ArcadeLink::Locations::discover().handoff + "/" + QUuid::createUuid().toString(QUuid::Id128);
        QVERIFY(ArcadeLink::ensurePrivateDir(sourceDirectory));
        const auto original = writeFile(sourceDirectory + "/text.txt",QByteArray(300 * 1024,'r'));
        const auto added = store.add({QJsonObject{{"type","text/plain"},{"path",original},{"owner","arcade.clipboard"}}}); QVERIFY(!added.error.isError());
        ArcadeLink::Error error; const auto first = store.exportItems(added.addedIds,{},true,&error); QVERIFY(!error.isError());
        const auto path = first.first().toObject()["path"].toString(); const auto markerPath = QFileInfo(path).absolutePath() + "/.arcade-shelf";
        QFile marker(markerPath); QVERIFY(marker.open(QIODevice::ReadOnly)); const auto ownership = marker.readAll(); marker.close();
        QVERIFY(!writeFile(markerPath,"not Shelf's ownership marker").isEmpty());
        QVERIFY(store.exportItems(added.addedIds,{},true,&error).isEmpty()); QCOMPARE(error.code,QString("internal"));
        QVERIFY(!writeFile(markerPath,ownership).isEmpty());
        const auto restored = store.exportItems(added.addedIds,{},true,&error); QVERIFY(!error.isError()); QCOMPARE(restored.first().toObject()["path"].toString(),path);
        QVERIFY(QDir(QFileInfo(path).absolutePath()).removeRecursively()); QVERIFY(QDir(sourceDirectory).removeRecursively());
    }

    void directorySymlinksAreReferencesAndDeduplicateByAlias()
    {
        QTemporaryDir d; Store store(d.path() + "/store"); QVERIFY(store.open());
        const auto target = d.path() + "/directory"; QVERIFY(QDir().mkpath(target));
        const auto alias = d.path() + "/directory-alias";
        QVERIFY(::symlink(QFile::encodeName(target).constData(),QFile::encodeName(alias).constData()) == 0);
        QCOMPARE(store.add({file(alias,"file/any")}).error.code,QString("unsupported_input"));
        const auto nativeContent = ArcadeLink::fileContent(alias); QCOMPARE(nativeContent["type"].toString(),QString("folder/reference"));
        auto added = store.add({nativeContent}); QVERIFY(!added.error.isError()); QCOMPARE(added.addedIds.size(),1);
        const auto id = added.addedIds.first(); const auto staged = store.items("quick").first().toObject();
        QCOMPARE(staged["path"].toString(),alias); QCOMPARE(staged["type"].toString(),QString("folder/reference")); QVERIFY(!staged["owned"].toBool());
        added = store.add({nativeContent}); QVERIFY(!added.error.isError()); QVERIFY(added.addedIds.isEmpty());
        QCOMPARE(added.result["data"].toObject()["skipped"].toArray().first().toObject()["reason"].toString(),QString("duplicate"));
        added = store.add({file(target,"folder/reference")}); QVERIFY(!added.error.isError()); QCOMPARE(added.addedIds.size(),1); QCOMPARE(store.items("quick").size(),2);
        ArcadeLink::Error error; const auto picked = store.exportItems({id},{"folder/reference"},false,&error);
        QVERIFY(!error.isError()); QCOMPARE(picked.first().toObject()["path"].toString(),alias);
        const auto handoff = ArcadeLink::Locations::discover().handoff + "/" + QUuid::createUuid().toString(QUuid::Id128);
        QVERIFY(ArcadeLink::ensurePrivateDir(handoff)); const auto temporaryAlias = handoff + "/directory";
        QVERIFY(::symlink(QFile::encodeName(target).constData(),QFile::encodeName(temporaryAlias).constData()) == 0);
        auto temporary = file(temporaryAlias,"folder/reference"); temporary.insert("owner","arcade.lens");
        QCOMPARE(store.add({temporary}).error.code,QString("unsupported_input")); QVERIFY(QFileInfo(temporaryAlias).isSymLink());
        QVERIFY(QDir(handoff).removeRecursively()); QVERIFY(store.remove({id}));
        QVERIFY(QFileInfo(alias).isSymLink()); QVERIFY(QDir(target).exists());
    }

    void unresolvedSymlinksPreservedAndStoragePrivate()
    {
        QTemporaryDir d; Store store(d.path() + "/store"); QVERIFY(store.open());
        const auto dangling = d.path() + "/dangling.txt";
        QVERIFY(::symlink("never-existed",QFile::encodeName(dangling).constData()) == 0);
        const auto r = store.add({file(dangling,"file/text")}); QVERIFY(!r.error.isError());
        const auto i = store.items("quick").first().toObject(); QCOMPARE(i["path"].toString(),dangling); QVERIFY(i["available"].toBool());
        QVERIFY(store.remove(r.addedIds)); QVERIFY(QFileInfo(dangling).isSymLink());
        struct stat st {}; QVERIFY(::lstat(QFile::encodeName(store.root()).constData(),&st) == 0); QCOMPARE(st.st_mode & 0777,static_cast<mode_t>(0700));
        const auto alias = d.path() + "/alias"; QVERIFY(::symlink(QFile::encodeName(store.root()).constData(),QFile::encodeName(alias).constData()) == 0);
        Store unsafe(alias); QVERIFY(!unsafe.open());
        const auto fifo = d.path() + "/pipe"; QVERIFY(::mkfifo(QFile::encodeName(fifo).constData(),0600) == 0);
        QCOMPARE(store.add({file(fifo)}).error.code,QString("unsupported_input"));
        const auto peerDirectory = ArcadeLink::Locations::discover().handoff + "/" + QUuid::createUuid().toString(QUuid::Id128);
        QVERIFY(ArcadeLink::ensurePrivateDir(peerDirectory)); const auto handoffLink = peerDirectory + "/escape.txt";
        QVERIFY(::symlink(QFile::encodeName(dangling).constData(),QFile::encodeName(handoffLink).constData()) == 0);
        auto input = file(handoffLink); input.insert("owner","arcade.lens");
        QCOMPARE(store.add({input}).error.code,QString("unsupported_input")); QVERIFY(QFileInfo(handoffLink).isSymLink());
        QVERIFY(QDir(peerDirectory).removeRecursively());
    }
#endif
private:
    QTemporaryDir m_ecosystem;
    QByteArray m_oldHome;
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc,argv);
#ifdef Q_OS_UNIX
    if (app.arguments().contains("--unsafe-staging-probe")) return unsafeStagingProbe();
#endif
    TestStore test;
    return QTest::qExec(&test,argc,argv);
}
#include "tst_Store.moc"
