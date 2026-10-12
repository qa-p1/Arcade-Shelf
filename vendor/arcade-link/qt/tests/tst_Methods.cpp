#include "ArcadeAccelerator.h"
#include "ArcadeLink.h"
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
using namespace ArcadeLink;

// Keep the server event loop running while the blocking client uses a worker.
template <typename F> static void worker(F &&fn) {
  auto *thread = QThread::create(std::forward<F>(fn));
  thread->start();
  QTRY_VERIFY_WITH_TIMEOUT(thread->isFinished(), 15000);
  delete thread;
}

class MethodsTest : public QObject {
  Q_OBJECT
  static void configure(Server &server, const QJsonObject &response) {
    const auto handler = [response](const QJsonObject &, const Responder &r) {
      if (response.contains(QStringLiteral("error")))
        r.fail(Error::fromJson(
            response.value(QStringLiteral("error")).toObject()));
      else
        r.done(response.value(QStringLiteral("result")).toObject());
    };
    server.settings = handler;
    server.menu = handler;
    server.menuInvoke = handler;
    server.shortcutsSet = handler;
    server.settingsExport = handler;
    server.settingsImport = handler;
    server.restart = [](const QString &) {};
  }
private slots:
  void destroyingServerWithALiveJobDisconnectsSafely() {
    QTemporaryDir root;
    const auto loc = Locations::under(root.path());
    Responder held;
    auto server = std::make_unique<Server>(Ids::Find, QStringLiteral("1"), loc);
    server->invoke = [&](const QJsonObject &, const Responder &r) {
      held = r;
      r.startJob();
    };
    QVERIFY(server->start());
    std::atomic_bool started{false}, disconnected{false};
    auto *thread = QThread::create([&] {
      Error error;
      auto client = Client::connect(loc, Ids::Find, Ids::Tools,
                                   QStringLiteral("1"), 3000, &error);
      if (!client) return;
      started = !client->call(QStringLiteral("invoke"), {}, &error)
                     .toObject().value(QStringLiteral("job")).toString().isEmpty();
      QJsonObject message;
      disconnected = !client->nextNotification(&message, &error, 3000);
    });
    thread->start();
    QTRY_VERIFY_WITH_TIMEOUT(started.load(), 4000);
    // The owned socket emits disconnected during destruction; its callbacks
    // must not access maps that have already been destroyed.
    server.reset();
    QTRY_VERIFY_WITH_TIMEOUT(thread->isFinished(), 4000);
    delete thread;
    QVERIFY(disconnected.load());
    QVERIFY(held.cancelled());
    held.finish({});
  }
  void everyMethodVectorRunsThroughTheServer() {
    QFile file(QStringLiteral(ARCADE_LINK_VECTORS "/methods.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto cases = QJsonDocument::fromJson(file.readAll())
                           .object()
                           .value(QStringLiteral("cases"))
                           .toArray();
    for (const auto &value : cases) {
      const auto item = value.toObject(),
                 request = item.value(QStringLiteral("request")).toObject(),
                 response = item.value(QStringLiteral("response")).toObject();
      QTemporaryDir root;
      const auto loc = Locations::under(root.path());
      Server server(Ids::Find, QStringLiteral("0.3.0"), loc);
      configure(server, response);
      const auto method = request.value(QStringLiteral("method")).toString();
      const auto expectedError =
          response.value(QStringLiteral("error")).toObject();
      if (expectedError.value(QStringLiteral("code")).toString() ==
          u"unsupported")
        server.settings = {};
      if (expectedError.value(QStringLiteral("code")).toString() == u"busy")
        server.restartReady = [](const QString &) {
          return Error::make(QStringLiteral("busy"), QStringLiteral("busy"));
        };
      if (method == u"describe") {
        server.restart = {};
        server.menuInvoke = {};
        server.shortcutsSet = {};
        server.settingsExport = {};
        server.settingsImport = {};
      }
      QVERIFY(server.start());
      QJsonValue result;
      Error error;
      worker([&] {
        auto client = Client::connect(loc, Ids::Find, Ids::Tools,
                                      QStringLiteral("0.3.0"), 3000, &error);
        if (client)
          result = client->call(
              method, request.value(QStringLiteral("params")).toObject(),
              &error, 3000);
      });
      if (!expectedError.isEmpty()) {
        QCOMPARE(error.code,
                 expectedError.value(QStringLiteral("code")).toString());
        if (error.code == u"unsupported")
          QVERIFY(error.isUnsupported());
      } else {
        QVERIFY2(!error.isError(), qPrintable(error.message));
        QCOMPARE(result, response.value(QStringLiteral("result")));
      }
    }
  }
  void optionalMethodValidationVectorsRunBeforeCallbacks() {
    QFile file(QStringLiteral(ARCADE_LINK_VECTORS "/methods.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto cases = QJsonDocument::fromJson(file.readAll()).object()
                           .value(QStringLiteral("validation")).toArray();
    QVERIFY(!cases.isEmpty());
    QTemporaryDir root;
    const auto loc = Locations::under(root.path());
    Server server(Ids::Find, QStringLiteral("0.3.0"), loc);
    int calls = 0;
    QString accelerator;
    const auto handler = [&](const QJsonObject &params, const Responder &r) {
      ++calls;
      accelerator = params.value(QStringLiteral("accelerator")).toString();
      r.done({{QStringLiteral("applied"), true}, {QStringLiteral("via"), QStringLiteral("native")}});
    };
    server.menuInvoke = handler;
    server.shortcutsSet = handler;
    server.settingsImport = handler;
    server.restartReady = [&](const QString &) { ++calls; return Error{}; };
    server.restart = [](const QString &) {};
    QVERIFY(server.start());
    for (const auto &value : cases) {
      const auto item = value.toObject();
      const auto request = item.value(QStringLiteral("request")).toObject();
      const auto response = item.value(QStringLiteral("response")).toObject();
      const auto before = calls;
      QJsonValue result;
      Error error;
      worker([&] {
        auto client = Client::connect(loc, Ids::Find, Ids::Tools, QStringLiteral("1"), 10000, &error);
        if (client) result = client->call(request.value(QStringLiteral("method")).toString(),
                                          request.value(QStringLiteral("params")).toObject(), &error, 10000);
      });
      if (response.contains(QStringLiteral("error"))) {
        QCOMPARE(error.toJson(), response.value(QStringLiteral("error")).toObject());
        QCOMPARE(calls, before);
      } else {
        QVERIFY2(!error.isError(), qPrintable(error.message));
        QCOMPARE(result, response.value(QStringLiteral("result")));
        QCOMPARE(calls, before + 1);
        QCOMPARE(accelerator, item.value(QStringLiteral("normalized")).toString());
      }
    }
  }
  void callbacksHelpersMenuNotificationsAndBusyRestart() {
    QTemporaryDir root;
    const auto loc = Locations::under(root.path());
    Server server(Ids::Find, QStringLiteral("0.3.0"), loc);
    bool checked = false, opened = false, imported = false;
    QString binding, mode;
    Responder job;
    const auto path = root.filePath(QStringLiteral("settings.json"));
    server.settings = [&](const QJsonObject &, const Responder &r) {
      opened = true;
      r.done({{QStringLiteral("opened"), true}});
    };
    server.menu = [&](const QJsonObject &, const Responder &r) {
      r.done({{QStringLiteral("items"),
               QJsonArray{QJsonObject{
                   {QStringLiteral("id"), QStringLiteral("private")},
                   {QStringLiteral("title"), QStringLiteral("Private mode")},
                   {QStringLiteral("kind"), QStringLiteral("toggle")},
                   {QStringLiteral("checked"), checked},
                   {QStringLiteral("enabled"), true}}}}});
    };
    server.menuInvoke = [&](const QJsonObject &, const Responder &r) {
      checked = !checked;
      r.done({{QStringLiteral("done"), true}});
    };
    server.shortcutsSet = [&](const QJsonObject &p, const Responder &r) {
      QString error;
      binding = Accelerator::normalize(
          p.value(QStringLiteral("accelerator")).toString(), &error);
      if (!error.isEmpty())
        r.fail(Error::make(QStringLiteral("denied"), error));
      else
        r.done({{QStringLiteral("applied"), true},
                {QStringLiteral("via"), QStringLiteral("native")}});
    };
    server.settingsExport = [&](const QJsonObject &, const Responder &r) {
      QFile f(path);
      if (!f.open(QIODevice::WriteOnly)) {
        r.fail(Error::make(QStringLiteral("internal"), f.errorString()));
        return;
      }
      f.write("{\"theme\":\"dark\"}");
      f.close();
      r.done({{QStringLiteral("outputs"),
               QJsonArray{QJsonObject{
                   {QStringLiteral("type"), QStringLiteral("file/any")},
                   {QStringLiteral("path"), path}}}}});
    };
    server.settingsImport = [&](const QJsonObject &p, const Responder &r) {
      const auto input = p.value(QStringLiteral("inputs"))
                             .toArray()
                             .first()
                             .toObject()
                             .value(QStringLiteral("path"))
                             .toString();
      QFile file(input);
      imported =
          file.open(QIODevice::ReadOnly) && file.readAll().contains("theme");
      r.done({{QStringLiteral("imported"), imported},
              {QStringLiteral("restartRequired"), false}});
    };
    server.restart = [&](const QString &value) { mode = value; };
    server.trayState = [] { return QStringLiteral("hosted"); };
    server.invoke = [&](const QJsonObject &, const Responder &r) {
      job = r;
      r.startJob();
    };
    QVERIFY(server.start());
    bool ok = false;
    worker([&] {
      Error e;
      auto c = Client::connect(loc, Ids::Find, Ids::Tools, QStringLiteral("1"),
                               3000, &e);
      if (!c)
        return;
      if (c->describe(&e).value(QStringLiteral("methods")).toArray().size() !=
              7 ||
          !c->settings(&e))
        return;
      if (!c->subscribe({QStringLiteral("app.changed")}, &e) ||
          c->menu(&e)
              .first()
              .toObject()
              .value(QStringLiteral("checked"))
              .toBool())
        return;
      if (!c->menuInvoke(QStringLiteral("private"), &e))
        return;
      QJsonObject notice;
      if (!c->nextNotification(&notice, &e, 3000) ||
          !notice.value(QStringLiteral("params"))
               .toObject()
               .value(QStringLiteral("menu"))
               .toBool())
        return;
      if (!c->menu(&e)
               .first()
               .toObject()
               .value(QStringLiteral("checked"))
               .toBool())
        return;
      if (!c->shortcutsSet(QStringLiteral("toggle"),
                           QStringLiteral("Control+alt+f"), &e)
               .value(QStringLiteral("applied"))
               .toBool())
        return;
      if (!c->settingsImport(c->settingsExport(&e), &e)
               .value(QStringLiteral("imported"))
               .toBool())
        return;
      if (c->call(QStringLiteral("app.status"), {}, &e)
              .toObject()
              .value(QStringLiteral("status"))
              .toObject()
              .value(QStringLiteral("tray"))
              .toString() != u"hosted")
        return;
      if (c->call(QStringLiteral("invoke"), {}, &e)
              .toObject()
              .value(QStringLiteral("job"))
              .toString()
              .isEmpty())
        return;
      if (c->restart({}, &e) || e.code != u"busy")
        return;
      ok = c->restart(QStringLiteral("force"), &e);
    });
    QVERIFY(ok);
    QVERIFY(opened && checked && imported);
    QCOMPARE(binding, QStringLiteral("Ctrl+Alt+F"));
    QTRY_COMPARE(mode, QStringLiteral("force"));
    job.finish({});
    QVERIFY(Error::make(QStringLiteral("internal"), QStringLiteral("old"),
                        QStringLiteral("unsupported_method"))
                .isUnsupported());
  }
  void optionalMethodsInteropInBothDirections() {
    const auto cli = qEnvironmentVariable("ARCADE_LINK_CLI");
    if (cli.isEmpty())
      QSKIP("ARCADE_LINK_CLI required");
    QTemporaryDir root;
    const auto loc = Locations::under(root.path());
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("ARCADE_HOME"), root.path());
    QProcess mock;
    mock.setProcessEnvironment(env);
    mock.start(cli, {QStringLiteral("mock"), QStringLiteral("--as"),
                     QStringLiteral("clipboard"), QStringLiteral("--actions"),
                     QStringLiteral(ARCADE_LINK_FIXTURES "/clipboard.json")});
    struct Stop {
      QProcess &p;
      ~Stop() {
        if (p.state() != QProcess::NotRunning) {
          p.kill();
          p.waitForFinished();
        }
      }
    } stop{mock};
    QVERIFY(mock.waitForStarted());
    // Presence publishes its manifest before binding the socket. Wait for the
    // mock's post-bind acknowledgement, rather than treating publication as
    // readiness on a loaded CI runner.
    QByteArray startup;
    QTRY_VERIFY_WITH_TIMEOUT(
        (startup += mock.readAllStandardError()).contains("listening ("), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(
        QFileInfo::exists(loc.manifestPath(Ids::Clipboard)), 10000);
    bool ok = false;
    QString failure;
    worker([&] {
      Error e;
      auto c = Client::connect(loc, Ids::Clipboard, Ids::Tools,
                               QStringLiteral("1"), 10000, &e);
      if (!c) {
        failure = QStringLiteral("connect: %1 (%2)").arg(e.message, e.reason);
        return;
      }
      const auto menu = c->menu(&e);
      if (menu.isEmpty() || !c->settings(&e) || !c->restart({}, &e) ||
          !c->menuInvoke(
              menu.first().toObject().value(QStringLiteral("id")).toString(),
              &e)) {
        failure = QStringLiteral("menu/settings/restart/invoke: %1 (%2)")
                      .arg(e.message, e.reason);
        return;
      }
      const auto files = c->settingsExport(&e);
      ok = !files.isEmpty() &&
           c->settingsImport(files, &e)
               .value(QStringLiteral("imported"))
               .toBool() &&
           c->describe(&e).value(QStringLiteral("methods")).toArray().size() ==
               7;
      if (!ok)
        failure = QStringLiteral("export/import/describe: %1 (%2)")
                      .arg(e.message, e.reason);
    });
    QVERIFY2(ok, qPrintable(failure));
    Server server(Ids::Wheel, QStringLiteral("0.3.0"), loc);
    configure(server, {{QStringLiteral("result"),
                        QJsonObject{{QStringLiteral("done"), true}}}});
    QVERIFY(server.start());
    QFile vectors(QStringLiteral(ARCADE_LINK_VECTORS "/methods.json"));
    QVERIFY(vectors.open(QIODevice::ReadOnly));
    for (const auto &value : QJsonDocument::fromJson(vectors.readAll())
                                 .object()
                                 .value(QStringLiteral("cases"))
                                 .toArray()) {
      const auto item = value.toObject();
      if (item.value(QStringLiteral("response"))
              .toObject()
              .contains(QStringLiteral("error")))
        continue;
      const auto req = item.value(QStringLiteral("request")).toObject();
      QProcess call;
      call.setProcessEnvironment(env);
      call.start(
          cli,
          {QStringLiteral("call"), QStringLiteral("wheel"),
           req.value(QStringLiteral("method")).toString(),
           QStringLiteral("--params"),
           QString::fromUtf8(
               QJsonDocument(req.value(QStringLiteral("params")).toObject())
                   .toJson(QJsonDocument::Compact))});
      QTRY_VERIFY_WITH_TIMEOUT(call.state() == QProcess::NotRunning, 10000);
      const auto output = call.readAllStandardOutput();
      QVERIFY2(call.exitCode() == 0, call.readAllStandardError().constData());
      QVERIFY(QJsonDocument::fromJson(output).isObject());
    }
  }
};
QTEST_GUILESS_MAIN(MethodsTest)
#include "tst_Methods.moc"
