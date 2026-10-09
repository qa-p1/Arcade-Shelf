#include "core/Store.h"
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#ifdef Q_OS_UNIX
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
        QString directory = d.path();
        for (int i=0;i<6;++i) { directory += "/" + QString(180,QChar('a' + i)); QVERIFY(QDir().mkdir(directory)); }
        QJsonArray paths;
        for (int i=0;i<1000;++i) { const auto path = writeFile(directory + "/item_" + QString::number(i)); QVERIFY(!path.isEmpty()); paths.append(path); }
        const auto refs = store.add({QJsonObject{{"type","file/any[]"},{"paths",paths}}}); QVERIFY(!refs.error.isError());
        QVERIFY(store.exportItems(refs.addedIds,{},true,&error).isEmpty()); QCOMPARE(error.code,QString("too_large")); QCOMPARE(error.limit,static_cast<qint64>(ArcadeLink::MaxLineBytes));
#endif
    }

#ifdef Q_OS_UNIX
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

QTEST_GUILESS_MAIN(TestStore)
#include "tst_Store.moc"
