#include "link/PeerHub.h"
#include "link/ShelfInvoke.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <optional>

using namespace Shelf;
namespace {
QJsonObject action(QString id, QJsonArray accepts = {}, QJsonArray produces = {}, bool interactive = false)
{
    return {{"id", id}, {"version", 1}, {"title", id}, {"accepts", accepts},
        {"produces", produces}, {"effects", QJsonArray{}}, {"interactive", interactive},
        {"privacy", "local"}, {"available", true}};
}
QJsonObject manifest(QString id, QJsonArray actions)
{
    return {{"schema", 1}, {"id", id}, {"name", id}, {"version", "0.1.0"},
        {"link", QJsonObject{{"protocol", QJsonArray{1}}}},
        {"executable", QCoreApplication::applicationFilePath()},
        {"launch", QJsonObject{{"background", QJsonArray{"--background"}}}},
        {"settings", QJsonObject{{"linkEnabled", true}}}, {"actions", actions}};
}
bool containsAction(const QJsonArray &offers, QString id)
{
    for (const auto &value : offers)
        if (value.toObject().value("action").toString() == id) return true;
    return false;
}
}

class PeerHubTest : public QObject {
    Q_OBJECT
private slots:
    void cacheFiltersBatchesLimitsAndToggles()
    {
        QTemporaryDir temp;
        qputenv("ARCADE_HOME", temp.path().toUtf8());
        const auto locations = ArcadeLink::Locations::discover();
        auto single = action("mock.single", {"file/*", "folder/reference"});
        auto batch = action("mock.batch", {"file/*[]", "folder/reference"});
        auto limited = batch;
        limited.insert("id", "mock.limited");
        limited.insert("maxBytes", 4);
        auto wrongPlatform = batch;
        wrongPlatform.insert("id", "mock.platform");
        wrongPlatform.insert("platforms", QJsonArray{"unavailable-os"});
        auto unavailable = batch;
        unavailable.insert("id", "mock.unavailable");
        unavailable.insert("available", false);
        QVERIFY(ArcadeLink::writeManifest(locations, manifest("arcade.mock", {single, batch, limited, wrongPlatform, unavailable})));
        auto clipboard = action("clipboard.add", {"file/any[]", "text/*"});
        QVERIFY(ArcadeLink::writeManifest(locations, manifest("arcade.clipboard", {clipboard})));
        {
            PeerHub hub;
            hub.start();
            QTRY_COMPARE_WITH_TIMEOUT(hub.manifests().size(), 2, 3000);
            QJsonArray one{QJsonObject{{"type", "file/image"}, {"path", "/a.png"}, {"size", 5}}};
            auto offers = hub.offers(one);
            QVERIFY(containsAction(offers, "mock.single"));
            QVERIFY(!containsAction(offers, "mock.platform"));
            QVERIFY(!containsAction(offers, "mock.unavailable"));
            bool limitedDisabled = false;
            for (const auto &value : offers) {
                const auto offer = value.toObject();
                if (offer.value("action").toString() == "mock.limited")
                    limitedDisabled = !offer.value("enabled").toBool() && offer.value("reason").toString().contains("limit 4 bytes");
            }
            QVERIFY(limitedDisabled);
            auto mixed = one;
            mixed.append(QJsonObject{{"type", "folder/reference"}, {"path", "/folder"}});
            offers = hub.offers(mixed);
            QVERIFY(!containsAction(offers, "mock.single"));
            QVERIFY(containsAction(offers, "mock.batch"));
            QVERIFY(!containsAction(offers, "clipboard.add"));
            QJsonArray textAndFile = one;
            textAndFile.append(QJsonObject{{"type", "text/plain"}, {"text", "hello"}});
            QVERIFY(!containsAction(hub.offers(textAndFile), "clipboard.add"));
            hub.setPeerEnabled("arcade.mock", false);
            QVERIFY(!containsAction(hub.offers(one), "mock.batch"));
            hub.setEnabled(false);
            QVERIFY(hub.offers(one).isEmpty());
            QVERIFY(hub.sources().isEmpty());
        }
        qunsetenv("ARCADE_HOME");
    }

    // Arcade Find's real manifest (`arcade-find --arcade-manifest`, Find
    // feature/find-v0.1): what Shelf offers for each kind of selection, and
    // that the payload Shelf builds passes its own pre-flight validation.
    void findManifestOffersShowAndSearch()
    {
        QTemporaryDir temp;
        qputenv("ARCADE_HOME", temp.path().toUtf8());
        const auto locations = ArcadeLink::Locations::discover();
        QFile fixture(QStringLiteral(SHELF_FIXTURES "/arcade.find.manifest.json"));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        auto find = QJsonDocument::fromJson(fixture.readAll()).object();
        QCOMPARE(find.value("id").toString(), QString("arcade.find"));
        find.insert("executable", QCoreApplication::applicationFilePath());
        QVERIFY(ArcadeLink::writeManifest(locations, find));
        const QString file = temp.filePath("brief.pdf");
        { QFile f(file); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("pdf"); }
        {
            PeerHub hub;
            hub.start();
            QTRY_COMPARE_WITH_TIMEOUT(hub.manifests().size(), 1, 3000);
            const QJsonObject one{{"type", "file/document"}, {"path", file}, {"size", 3}};
            const QJsonObject folder{{"type", "folder/reference"}, {"path", temp.path()}};
            const QJsonObject note{{"type", "text/plain"}, {"text", "invoice 2024"}};
            QVERIFY(containsAction(hub.offers({one}), "find.show"));
            QVERIFY(containsAction(hub.offers({folder}), "find.show"));
            QVERIFY(containsAction(hub.offers({note}), "find.show"));
            QVERIFY(containsAction(hub.offers({note}), "find.search"));
            QVERIFY(!containsAction(hub.offers({one}), "find.search"));
            // Find's actions take one value: never offered for a multi-selection.
            QVERIFY(!containsAction(hub.offers({one, folder}), "find.show"));
            QVERIFY(!containsAction(hub.offers({note, note}), "find.search"));
            const QJsonObject request{{"action", "find.show"}, {"version", 1}, {"inputs", QJsonArray{one}}, {"options", QJsonObject{}}};
            QVERIFY(!ShelfInvoke::validate(find, request).isError());
        }
        qunsetenv("ARCADE_HOME");
    }

    void residentProgressAndCancellation()
    {
        QTemporaryDir temp;
        qputenv("ARCADE_HOME", temp.path().toUtf8());
        const auto locations = ArcadeLink::Locations::discover();
        const auto pick = action("mock.pick", {}, {"text/plain"}, true);
        QVERIFY(ArcadeLink::writeManifest(locations, manifest("arcade.mock", {pick})));
        ArcadeLink::Server server("arcade.mock", "0.1.0", locations);
        std::optional<ArcadeLink::Responder> pending;
        server.describe = [pick] { return QJsonArray{pick}; };
        server.invoke = [&](const QJsonObject &, const ArcadeLink::Responder &responder) {
            pending = responder;
            responder.startJob();
            responder.progress(0.25, "Choosing");
        };
        QVERIFY(server.start());
        {
            PeerHub hub;
            QSignalSpy progress(&hub, &PeerHub::progress);
            QSignalSpy finished(&hub, &PeerHub::finished);
            hub.start();
            QTRY_COMPARE_WITH_TIMEOUT(hub.manifests().size(), 1, 3000);
            QVERIFY(containsAction(hub.sources(), "mock.pick"));
            hub.invoke("arcade.mock", "mock.pick");
            QVERIFY(hub.busy());
            QTRY_VERIFY_WITH_TIMEOUT(pending.has_value(), 3000);
            QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty(), 3000);
            QCOMPARE(progress.last().at(0).toDouble(), 0.25);
            pending->finish({{"outputs", QJsonArray{QJsonObject{{"type", "text/plain"}, {"text", "selected"}}}}});
            pending.reset();
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
            QVERIFY(!qvariant_cast<ArcadeLink::Error>(finished.last().at(1)).isError());
            QCOMPARE(finished.last().at(0).toJsonObject().value("outputs").toArray().at(0).toObject().value("text").toString(), QString("selected"));
            QVERIFY(!hub.busy());
            hub.invoke("arcade.mock", "mock.pick");
            QTRY_VERIFY_WITH_TIMEOUT(pending.has_value(), 3000);
            hub.cancel();
            // The mock deliberately never sends job.done on cancellation.
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 3000);
            QCOMPARE(qvariant_cast<ArcadeLink::Error>(finished.last().at(1)).code, QString("cancelled"));
            QVERIFY(!hub.busy());
            QTRY_VERIFY_WITH_TIMEOUT(pending->cancelled(), 3000);
            pending.reset();
        }
        server.stop();
        qunsetenv("ARCADE_HOME");
    }

    void workerRechecksActualFileSize()
    {
        QTemporaryDir temp;
        qputenv("ARCADE_HOME", temp.path().toUtf8());
        const auto locations = ArcadeLink::Locations::discover();
        const auto path = temp.filePath("data.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("12345678"), qint64(8));
        file.close();
        auto limited = action("mock.limited", {"file/*"});
        limited.insert("maxBytes", 4);
        QVERIFY(ArcadeLink::writeManifest(locations, manifest("arcade.mock", {limited})));
        {
            PeerHub hub;
            QSignalSpy finished(&hub, &PeerHub::finished);
            hub.start();
            QTRY_COMPARE_WITH_TIMEOUT(hub.manifests().size(), 1, 3000);
            hub.invoke("arcade.mock", "mock.limited", {QJsonObject{{"type", "file/text"}, {"path", path}, {"size", 0}}});
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
            const auto error = qvariant_cast<ArcadeLink::Error>(finished.last().at(1));
            QCOMPARE(error.code, QString("too_large"));
            QCOMPARE(error.limit, qint64(4));
        }
        qunsetenv("ARCADE_HOME");
    }

    void stoppedPeerLaunchesInBackground()
    {
        QTemporaryDir temp;
        qputenv("ARCADE_HOME", temp.path().toUtf8());
        const auto locations = ArcadeLink::Locations::discover();
        auto peer = manifest("arcade.mock", {action("mock.add", {"text/plain"})});
        // A real subprocess serves the request; no launch.invoke is advertised.
        peer.insert("launch", QJsonObject{{"background", QJsonArray{"--peer-helper"}}});
        QVERIFY(ArcadeLink::writeManifest(locations, peer));
        {
            PeerHub hub;
            QSignalSpy finished(&hub, &PeerHub::finished);
            hub.start();
            QTRY_COMPARE_WITH_TIMEOUT(hub.manifests().size(), 1, 3000);
            hub.invoke("arcade.mock", "mock.add", {QJsonObject{{"type", "text/plain"}, {"text", "hello"}}});
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
            QVERIFY(!qvariant_cast<ArcadeLink::Error>(finished.last().at(1)).isError());
            QCOMPARE(finished.last().at(0).toJsonObject().value("message").toString(), QString("Served resident request"));
            auto client = ArcadeLink::Client::connect(locations, "arcade.mock", "arcade.shelf", "0.1.0");
            QVERIFY(client);
            ArcadeLink::Error error;
            client->call("app.quit", {}, &error);
            QVERIFY(!error.isError());
            ArcadeLink::Endpoint endpoint;
            QTRY_VERIFY_WITH_TIMEOUT(!ArcadeLink::Endpoint::read(locations, "arcade.mock", &endpoint), 3000);
        }
        qunsetenv("ARCADE_HOME");
    }

    void pipelineContractsAndEffectsAreDiscovered()
    {
        QTemporaryDir temp;
        qputenv("ARCADE_HOME", temp.path().toUtf8());
        const auto locations = ArcadeLink::Locations::discover();
        const auto list = action("box.pipelines", {}, {"structured/pipelines"});
        const auto run = action("box.pipeline.run", {"*"}, {"*"});
        QVERIFY(ArcadeLink::writeManifest(locations, manifest("arcade.box", {list, run})));
        ArcadeLink::Server server("arcade.box", "0.1.0", locations);
        server.describe = [list, run] { return QJsonArray{list, run}; };
        server.invoke = [](const QJsonObject &request, const ArcadeLink::Responder &reply) {
            if (request.value("action").toString() != "box.pipelines") {
                reply.fail(ArcadeLink::Error::make("unsupported_input", {})); return;
            }
            const QJsonObject pipeline{{"id", "share"}, {"name", "Share image"}, {"version", 1},
                {"accepts", QJsonArray{"file/image"}}, {"produces", QJsonArray{"file/image"}},
                {"effects", QJsonArray{"sends-to-device"}}, {"interactive", false}};
            reply.done({{"outputs", QJsonArray{QJsonObject{{"type", "structured/pipelines"}, {"data", QJsonArray{pipeline}}}}}});
        };
        QVERIFY(server.start());
        {
            PeerHub hub;
            hub.start();
            QTRY_COMPARE_WITH_TIMEOUT(hub.manifests().size(), 1, 3000);
            const auto offers = hub.offers({QJsonObject{{"type", "file/image"}, {"path", "/image.png"}, {"size", 1}}});
            QCOMPARE(offers.size(), 1);
            const auto candidate = offers.first().toObject();
            QCOMPARE(candidate.value("action").toString(), QString("box.pipeline.run"));
            QCOMPARE(candidate.value("options").toObject().value("pipeline").toString(), QString("share"));
            QVERIFY(candidate.value("confirmationRequired").toBool());
            QVERIFY(hub.offers({QJsonObject{{"type", "text/plain"}, {"text", "hello"}}}).isEmpty());
        }
        server.stop();
        qunsetenv("ARCADE_HOME");
    }

    void pendingJobHasDeadlineAndPeerCrashIsReported()
    {
        QTemporaryDir temp;
        qputenv("ARCADE_HOME", temp.path().toUtf8());
        const auto locations = ArcadeLink::Locations::discover();
        const auto pick = action("mock.pick", {}, {"text/plain"}, true);
        const auto peer = manifest("arcade.mock", {pick});
        QVERIFY(ArcadeLink::writeManifest(locations, peer));
        ArcadeLink::Server server("arcade.mock", "0.1.0", locations);
        std::optional<ArcadeLink::Responder> pending;
        server.invoke = [&](const QJsonObject &, const ArcadeLink::Responder &reply) {
            pending = reply;
            reply.startJob();
        };
        QVERIFY(server.start());
        QJsonObject result;
        ArcadeLink::Error error;
        std::atomic_bool done{false}, cancelled{false};
        const QJsonObject request{{"action", "mock.pick"}, {"version", 1}, {"inputs", QJsonArray{}},
            {"options", QJsonObject{}}, {"context", QJsonObject{{"source", "arcade.shelf"}, {"interactive", true}}}};
        auto *worker = QThread::create([&] {
            ShelfInvoke::run(locations, peer, request, &result, &error, {}, &cancelled, 200);
            done.store(true);
        });
        worker->start();
        QTRY_VERIFY_WITH_TIMEOUT(done.load(), 3000);
        worker->wait();
        delete worker;
        QCOMPARE(error.code, QString("timeout"));
        pending.reset();
        {
            PeerHub hub;
            QSignalSpy finished(&hub, &PeerHub::finished);
            hub.start();
            QTRY_COMPARE_WITH_TIMEOUT(hub.manifests().size(), 1, 3000);
            hub.invoke("arcade.mock", "mock.pick");
            QTRY_VERIFY_WITH_TIMEOUT(pending.has_value(), 3000);
            server.stop();
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
            QCOMPARE(qvariant_cast<ArcadeLink::Error>(finished.last().at(1)).code, QString("not_running"));
            QVERIFY(!hub.busy());
            pending.reset();
        }
        qunsetenv("ARCADE_HOME");
    }

    void malformedPeerReplyIsRejected()
    {
        QTemporaryDir temp;
        qputenv("ARCADE_HOME", temp.path().toUtf8());
        const auto locations = ArcadeLink::Locations::discover();
        QVERIFY(ArcadeLink::ensurePrivateDir(locations.runtime));
        QLocalServer server;
        const auto address = ArcadeLink::socketAddress(locations, "arcade.mock");
        QVERIFY(server.listen(address));
        ArcadeLink::Endpoint endpoint;
        endpoint.protocol = {1};
        endpoint.address = address;
#ifdef Q_OS_WIN
        endpoint.transport = "pipe";
#else
        endpoint.transport = "unix";
#endif
        endpoint.token = ArcadeLink::newToken();
        endpoint.pid = QCoreApplication::applicationPid();
        endpoint.startedAt = ArcadeLink::nowRfc3339();
        QVERIFY(endpoint.write(locations, "arcade.mock"));
        connect(&server, &QLocalServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, socket, [socket] {
                while (socket->canReadLine()) {
                    const auto message = QJsonDocument::fromJson(socket->readLine()).object();
                    if (message.value("method").toString() == "hello")
                        socket->write(ArcadeLink::responseLine(1, QJsonObject{{"protocol", 1},
                            {"server", QJsonObject{{"id", "arcade.mock"}, {"version", "0.1.0"}}}}));
                    else
                        // A request echoed as a reply must never count as success.
                        socket->write(ArcadeLink::requestLine(2, "invoke", {}));
                }
            });
        });
        const auto peer = manifest("arcade.mock", {action("mock.pick", {}, {"text/plain"}, true)});
        QVERIFY(ArcadeLink::writeManifest(locations, peer));
        {
            PeerHub hub;
            QSignalSpy finished(&hub, &PeerHub::finished);
            hub.start();
            QTRY_COMPARE_WITH_TIMEOUT(hub.manifests().size(), 1, 3000);
            hub.invoke("arcade.mock", "mock.pick");
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
            QCOMPARE(qvariant_cast<ArcadeLink::Error>(finished.last().at(1)).code, QString("internal"));
        }
        server.close();
        qunsetenv("ARCADE_HOME");
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--peer-helper")) {
        const auto locations = ArcadeLink::Locations::discover();
        ArcadeLink::Server server("arcade.mock", "0.1.0", locations);
        server.describe = [] { return QJsonArray{action("mock.add", {"text/plain"})}; };
        server.invoke = [](const QJsonObject &request, const ArcadeLink::Responder &reply) {
            if (request.value("context").toObject().value("source").toString() != "arcade.shelf") {
                reply.fail(ArcadeLink::Error::make("unsupported_input", {})); return;
            }
            reply.done({{"message", "Served resident request"}});
        };
        server.quit = [&app] { QTimer::singleShot(50, &app, &QCoreApplication::quit); return true; };
        // A failed parent test can never leave a permanent helper behind.
        QTimer::singleShot(10000, &app, &QCoreApplication::quit);
        if (!server.start()) return 2;
        return app.exec();
    }
    PeerHubTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_PeerHub.moc"
