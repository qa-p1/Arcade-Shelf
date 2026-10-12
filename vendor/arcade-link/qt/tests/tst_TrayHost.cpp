#include "ArcadeTrayHost.h"
#include <QSaveFile>
#include <QtTest>
using namespace ArcadeLink;
class RustHost {
public:
  QProcess process;
  QByteArray output;
  RustHost(const QString &root, const QString &cli) {
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("ARCADE_HOME"), root);
    process.setProcessEnvironment(env);
    process.start(cli, {QStringLiteral("tray"), QStringLiteral("host")});
  }
  ~RustHost() {
    if (process.state() != QProcess::NotRunning) {
      process.kill();
      process.waitForFinished(5000);
    }
  }
  bool saw(const QByteArray &line) {
    output += process.readAllStandardOutput();
    return output.split('\n').contains(line);
  }
  void send(const QByteArray &command) { process.write(command + '\n'); }
};
class TrayTest : public QObject {
  Q_OBJECT
  static QString cli() { return qEnvironmentVariable("ARCADE_LINK_CLI"); }
private slots:
  void hostedCrashExclusionLinkOffAndQueuedSignals() {
    if (cli().isEmpty())
      QSKIP("ARCADE_LINK_CLI required for Rust interop");
    QTemporaryDir root;
    RustHost host(root.path(), cli());
    QTRY_VERIFY_WITH_TIMEOUT(host.saw("ready"), 5000);
    ArcadeTrayHost watcher(Locations::under(root.path()), Ids::Find);
    bool wrongThread = false;
    connect(&watcher, &ArcadeTrayHost::stateChanged, this,
            [&](const QString &) {
              wrongThread |= QThread::currentThread() != thread();
            });
    QSignalSpy states(&watcher, &ArcadeTrayHost::stateChanged),
        ready(&watcher, &ArcadeTrayHost::startupReady);
    QElapsedTimer call;
    call.start();
    watcher.start();
    QVERIFY(call.elapsed() < 20);
    QTRY_VERIFY_WITH_TIMEOUT(watcher.ready(), 1000);
    QCOMPARE(watcher.state(), QStringLiteral("hosted"));
    QCOMPARE(ready.count(), 1);
    QCOMPARE(states.count(), 1);
    QVERIFY(!wrongThread);
    host.send("exclude find");
    QTRY_VERIFY(host.saw("ok exclude find"));
    QTRY_COMPARE(watcher.state(), QStringLiteral("own"));
    host.send("include find");
    QTRY_VERIFY(host.saw("ok include find"));
    QTRY_COMPARE(watcher.state(), QStringLiteral("hosted"));
    watcher.setLinkEnabled(false);
    QCOMPARE(watcher.state(), QStringLiteral("own"));
    watcher.setLinkEnabled(true);
    QTRY_COMPARE(watcher.state(), QStringLiteral("hosted"));
    host.send("hosted false");
    QTRY_VERIFY(host.saw("ok hosted false"));
    QTRY_COMPARE(watcher.state(), QStringLiteral("own"));
    host.send("hosted true");
    QTRY_VERIFY(host.saw("ok hosted true"));
    QTRY_COMPARE(watcher.state(), QStringLiteral("hosted"));
    QElapsedTimer latency;
    latency.start();
    host.process.kill();
    QTRY_COMPARE_WITH_TIMEOUT(watcher.state(), QStringLiteral("own"), 950);
    QVERIFY(latency.elapsed() < 1000);
    qInfo("Qt tray hand-back: %lld ms", latency.elapsed());
    QVERIFY(!wrongThread);
    watcher.stop();
    watcher.start(false);
    QTRY_VERIFY(watcher.ready());
    QCOMPARE(watcher.state(), QStringLiteral("own"));
  }
  void restartingHasNoFlashAndExpiresAtFiveSeconds() {
    if (cli().isEmpty())
      QSKIP("ARCADE_LINK_CLI required for Rust interop");
    QTemporaryDir root;
    auto host = std::make_unique<RustHost>(root.path(), cli());
    QTRY_VERIFY_WITH_TIMEOUT(host->saw("ready"), 5000);
    ArcadeTrayHost watcher(Locations::under(root.path()), Ids::Wheel);
    watcher.start();
    QTRY_COMPARE(watcher.state(), QStringLiteral("hosted"));
    QSignalSpy states(&watcher, &ArcadeTrayHost::stateChanged);
    host->send("restarting");
    QTRY_VERIFY(host->saw("ok restarting"));
    host.reset();
    QTest::qWait(200);
    QCOMPARE(watcher.state(), QStringLiteral("hosted"));
    host = std::make_unique<RustHost>(root.path(), cli());
    QTRY_VERIFY_WITH_TIMEOUT(host->saw("ready"), 5000);
    // Hosted can still mean the old host's grace. Observe the successor's
    // actual subscription before asking it to restart a second time.
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
      host->send("subscribers");
      return host->saw("subscribers arcade.wheel");
    })(), 4000);
    QCOMPARE(states.count(), 0);
    QCOMPARE(watcher.state(), QStringLiteral("hosted"));
    QElapsedTimer elapsed;
    elapsed.start();
    host->send("restarting");
    QTRY_VERIFY(host->saw("ok restarting"));
    host.reset();
    QTest::qWait(1000);
    QCOMPARE(watcher.state(), QStringLiteral("hosted"));
    QTRY_COMPARE_WITH_TIMEOUT(watcher.state(), QStringLiteral("own"), 6500);
    QVERIFY(elapsed.elapsed() >= 4900);
    // Shared vectors separately check the exact 4999/5000 boundary with a
    // controllable clock. Scheduler delays must not fail this live I/O test.
  }
  void startupGateIsBoundedAndRecoversAfterSameTokenRewrite() {
    QTemporaryDir root;
    const auto loc = Locations::under(root.path());
    QLocalServer server;
    QVERIFY(ensurePrivateDir(loc.runtime));
    const auto address = socketAddress(loc, Ids::Tools);
    QVERIFY(server.listen(address));
    Endpoint ep;
    ep.address = address;
    ep.token = newToken();
    ep.protocol = {1};
    ep.pid = QCoreApplication::applicationPid();
    ep.transport = QStringLiteral("unix");
    ep.startedAt = nowRfc3339();
    QVERIFY(ep.write(loc, Ids::Tools));
    QJsonObject manifest{
        {QStringLiteral("schema"), 1},
        {QStringLiteral("id"), Ids::Tools},
        {QStringLiteral("executable"), QCoreApplication::applicationFilePath()},
        {QStringLiteral("settings"),
         QJsonObject{{QStringLiteral("trayHost"),
                      QJsonObject{{QStringLiteral("enabled"), true}}}}}};
    QVERIFY(writeManifest(loc, manifest));
    bool answer = false;
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
      auto *socket = server.nextPendingConnection();
      QObject::connect(socket, &QLocalSocket::readyRead, socket, [&, socket] {
        while (socket->canReadLine()) {
          QJsonObject message;
          if (classify(socket->readLine(), &message) != Kind::Request ||
              !answer)
            continue;
          const auto id = message.value(QStringLiteral("id")).toInteger();
          const auto method =
              message.value(QStringLiteral("method")).toString();
          if (method == u"hello")
            socket->write(responseLine(
                id, QJsonObject{
                        {QStringLiteral("protocol"), 1},
                        {QStringLiteral("server"),
                         QJsonObject{{QStringLiteral("id"), Ids::Tools}}}}));
          if (method == u"subscribe") {
            socket->write(responseLine(
                id, QJsonObject{{QStringLiteral("topics"),
                                 QJsonArray{QStringLiteral("tray.host")}}}));
            socket->write(notificationLine(QStringLiteral("tray.host"),
                                           {{QStringLiteral("hosted"), true}}));
          }
        }
      });
    });
    ArcadeTrayHost watcher(loc, Ids::Shelf);
    QSignalSpy ready(&watcher, &ArcadeTrayHost::startupReady);
    QElapsedTimer elapsed;
    elapsed.start();
    qint64 answeredAt = -1;
    connect(&watcher, &ArcadeTrayHost::startupReady, this,
            [&](const QString &) { answeredAt = elapsed.elapsed(); });
    watcher.start();
    QTRY_VERIFY_WITH_TIMEOUT(watcher.ready(), 1000);
    QCOMPARE(watcher.state(), QStringLiteral("own"));
    // Measure signal delivery itself, not QTRY's next 50 ms sampling turn.
    QVERIFY2(answeredAt <= 450, qPrintable(QString::number(answeredAt)));
    QCOMPARE(ready.count(), 1);
    QTest::qWait(30);
    answer = true;
    // A real identical-content rewrite is a readiness event; endpoint token
    // stays unchanged.
    QFile original(loc.manifestPath(Ids::Tools));
    QVERIFY(original.open(QIODevice::ReadOnly));
    const auto bytes = original.readAll();
    original.close();
    QSaveFile file(loc.manifestPath(Ids::Tools));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
    QVERIFY(file.commit());
    QTRY_COMPARE_WITH_TIMEOUT(watcher.state(), QStringLiteral("hosted"), 1200);
    watcher.stop();
    watcher.start(true, false);
    QTRY_VERIFY(watcher.ready());
    QCOMPARE(watcher.state(), QStringLiteral("none"));
  }
};
QTEST_GUILESS_MAIN(TrayTest)
#include "tst_TrayHost.moc"
