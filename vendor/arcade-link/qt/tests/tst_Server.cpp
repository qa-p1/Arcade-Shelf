// The Qt server and client, in-process, plus interop with the Rust
// implementation when ARCADE_LINK_CLI points at the `arcade-link` binary.
#include "ArcadeLink.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtTest>

#ifndef ARCADE_LINK_FIXTURES
#define ARCADE_LINK_FIXTURES "fixtures"
#endif

using namespace ArcadeLink;

// Runs blocking client code on a worker while this thread's event loop serves.
template <typename F>
static void onWorker(F &&f)
{
    QThread *t = QThread::create(std::forward<F>(f));
    t->start();
    QTRY_VERIFY_WITH_TIMEOUT(t->isFinished(), 15000);
    delete t;
}

class ServerTest : public QObject {
    Q_OBJECT

    static void serve(Server &s)
    {
        s.describe = [] {
            return QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("echo")}, {QStringLiteral("title"), QStringLiteral("Echo")}},
                              QJsonObject{{QStringLiteral("id"), QStringLiteral("slow")}, {QStringLiteral("title"), QStringLiteral("Slow")}}};
        };
        s.invoke = [&s](const QJsonObject &req, const Responder &r) {
            const QString action = req.value(QStringLiteral("action")).toString();
            if (action == u"echo") {
                r.done({{QStringLiteral("outputs"), req.value(QStringLiteral("inputs"))}, {QStringLiteral("message"), QStringLiteral("hi ") + r.peerId()}});
            } else if (action == u"slow") {
                r.startJob();
                auto *timer = new QTimer(&s);
                auto step = std::make_shared<int>(0);
                QObject::connect(timer, &QTimer::timeout, timer, [timer, r, step] {
                    if (r.cancelled()) {
                        r.finishError(Error::make(QStringLiteral("cancelled"), QStringLiteral("cancelled")));
                        timer->deleteLater();
                        return;
                    }
                    if (++*step > 20) {
                        r.finish({{QStringLiteral("message"), QStringLiteral("done")}});
                        timer->deleteLater();
                        return;
                    }
                    r.progress(*step / 20.0, QStringLiteral("working"));
                });
                timer->start(10);
            } else {
                r.fail(Error::make(QStringLiteral("unavailable"), QStringLiteral("x"), QStringLiteral("FFmpeg isn't installed")));
            }
        };
    }

private slots:
    void roundtrip()
    {
        QTemporaryDir dir;
        const Locations loc = Locations::under(dir.path());
        Server s(QStringLiteral("arcade.qt"), QStringLiteral("1.2"), loc);
        serve(s);
        QString err;
        QVERIFY2(s.start(&err), qPrintable(err));
#ifdef Q_OS_UNIX
        const auto perms = QFile::permissions(loc.endpointPath(QStringLiteral("arcade.qt")));
        QVERIFY(perms & QFileDevice::ReadOwner);
        QVERIFY(!(perms & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther)));
#endif
        bool ok = false;
        onWorker([&] {
            Error e;
            auto c = Client::connect(loc, QStringLiteral("arcade.qt"), QStringLiteral("arcade.test"), QStringLiteral("1"), 1000, &e);
            if (!c) return;
            const QJsonArray actions = c->call(QStringLiteral("describe"), {}, &e).toObject().value(QStringLiteral("actions")).toArray();
            QJsonObject r;
            const bool echoed = c->invoke({{QStringLiteral("action"), QStringLiteral("echo")}, {QStringLiteral("inputs"), QJsonArray{textContent(QStringLiteral("text/plain"), QStringLiteral("x"))}}}, &r, &e);
            const bool echoOk = echoed && r.value(QStringLiteral("message")).toString() == u"hi arcade.test";
            int steps = 0;
            QJsonObject done;
            const bool slow = c->invoke({{QStringLiteral("action"), QStringLiteral("slow")}}, &done, &e, [&](double, const QString &) { ++steps; });
            std::atomic_bool cancel{false};
            Error ce;
            int seen = 0;
            const bool cancelled = !c->invoke({{QStringLiteral("action"), QStringLiteral("slow")}}, nullptr, &ce, [&](double, const QString &) {
                if (++seen == 3) cancel = true;
            }, &cancel) && ce.code == u"cancelled";
            Error fe;
            const bool failed = !c->invoke({{QStringLiteral("action"), QStringLiteral("nope")}}, nullptr, &fe)
                                && fe.userMessage(QStringLiteral("Arcade Box")) == u"Arcade Box can't do this yet: FFmpeg isn't installed.";
            ok = actions.size() == 2 && echoOk && slow && steps > 3 && done.value(QStringLiteral("message")).toString() == u"done" && cancelled && failed;
        });
        QVERIFY(ok);
        s.stop();
        QVERIFY(!QFile::exists(loc.endpointPath(QStringLiteral("arcade.qt"))));
    }

    void wrongTokenIsDenied()
    {
        QTemporaryDir dir;
        const Locations loc = Locations::under(dir.path());
        Server s(QStringLiteral("arcade.qt"), QStringLiteral("1"), loc);
        serve(s);
        QVERIFY(s.start());
        Endpoint ep;
        QVERIFY(Endpoint::read(loc, QStringLiteral("arcade.qt"), &ep));
        ep.token = QString(64, QLatin1Char('0'));
        QVERIFY(ep.write(loc, QStringLiteral("arcade.qt")));
        QString code;
        onWorker([&] {
            Error e;
            auto c = Client::connect(loc, QStringLiteral("arcade.qt"), QStringLiteral("t"), QStringLiteral("1"), 1000, &e);
            code = c ? QStringLiteral("connected") : e.code;
        });
        QCOMPARE(code, QStringLiteral("denied"));
    }

    void secondServerRefused()
    {
        QTemporaryDir dir;
        const Locations loc = Locations::under(dir.path());
        Server a(QStringLiteral("arcade.qt"), QStringLiteral("1"), loc);
        QVERIFY(a.start());
        // The probe needs a's event loop, so start b from a worker.
        bool refused = false;
        QThread *t = QThread::create([&] {
            Server b(QStringLiteral("arcade.qt"), QStringLiteral("1"), loc);
            refused = !b.start();
        });
        t->start();
        QTRY_VERIFY_WITH_TIMEOUT(t->isFinished(), 5000);
        delete t;
        QVERIFY(refused);
    }

    void registryWatchAndShortcuts()
    {
        QTemporaryDir dir;
        const Locations loc = Locations::under(dir.path());
        Registry reg(loc);
        reg.refresh();
        reg.watch();
        QSignalSpy spy(&reg, &Registry::changed);
        const QJsonObject m{{QStringLiteral("schema"), 1}, {QStringLiteral("id"), QStringLiteral("arcade.box")}, {QStringLiteral("name"), QStringLiteral("Arcade Box")},
                            {QStringLiteral("executable"), QCoreApplication::applicationFilePath()},
                            {QStringLiteral("shortcuts"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("island")}, {QStringLiteral("accelerator"), QStringLiteral("Ctrl+Alt+Space")}}}}};
        QVERIFY(writeManifest(loc, m));
        QVERIFY(!writeManifest(loc, m)); // unchanged: not rewritten
        QTRY_VERIFY(spy.count() > 0);
        QCOMPARE(reg.apps().size(), 1);
        QCOMPARE(reg.shortcutOwner(QStringLiteral("arcade.wheel"), QStringLiteral("alt+ctrl+space")), QStringLiteral("Arcade Box"));
        QVERIFY(reg.shortcutOwner(QStringLiteral("arcade.wheel"), QStringLiteral("F8")).isEmpty());
    }

    // Qt client → Rust mock, and Rust CLI → Qt server.
    void interopWithRust()
    {
        const QString cli = qEnvironmentVariable("ARCADE_LINK_CLI");
        if (cli.isEmpty() || !QFileInfo(cli).isExecutable()) QSKIP("set ARCADE_LINK_CLI to the arcade-link binary");
        QTemporaryDir dir;
        const Locations loc = Locations::under(dir.path());
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("ARCADE_HOME"), dir.path());
        QProcess mock;
        mock.setProcessEnvironment(env);
        mock.start(cli, {QStringLiteral("mock"), QStringLiteral("--as"), QStringLiteral("box"), QStringLiteral("--actions"),
                         QStringLiteral(ARCADE_LINK_FIXTURES "/box.json")});
        QVERIFY(mock.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(loc.endpointPath(QStringLiteral("arcade.box"))), 5000);
        bool ok = false;
        onWorker([&] {
            Registry reg(loc);
            reg.refresh();
            const QJsonObject manifest = reg.app(QStringLiteral("arcade.box"));
            QJsonObject r;
            Error e;
            int steps = 0;
            const bool converted = invokeAction(loc, QStringLiteral("arcade.wheel"), QStringLiteral("1"), manifest,
                {{QStringLiteral("action"), QStringLiteral("box:arcade.image.convert")}, {QStringLiteral("preset"), QStringLiteral("webp")},
                 {QStringLiteral("inputs"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("file/image")}, {QStringLiteral("path"), QStringLiteral("/tmp/x.png")}}}}},
                &r, &e, [&](double, const QString &) { ++steps; });
            Error u;
            const bool unavailable = !invokeAction(loc, QStringLiteral("arcade.wheel"), QStringLiteral("1"), manifest,
                {{QStringLiteral("action"), QStringLiteral("box:arcade.pdf.ocr")}}, nullptr, &u) && u.code == u"unavailable";
            ok = converted && steps >= 3 && unavailable;
        });
        mock.kill();
        mock.waitForFinished();
        QVERIFY(ok);

        // Rust → Qt: the CLI invokes an action on a Qt server.
        Server s(QStringLiteral("arcade.wheel"), QStringLiteral("1"), loc);
        serve(s);
        QVERIFY(s.start());
        QVERIFY(writeManifest(loc, {{QStringLiteral("schema"), 1}, {QStringLiteral("id"), QStringLiteral("arcade.wheel")},
                                    {QStringLiteral("name"), QStringLiteral("Arcade Wheel")}, {QStringLiteral("executable"), QCoreApplication::applicationFilePath()},
                                    {QStringLiteral("actions"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("slow")}, {QStringLiteral("title"), QStringLiteral("Slow")}}}}}));
        QProcess call;
        call.setProcessEnvironment(env);
        call.start(cli, {QStringLiteral("invoke"), QStringLiteral("wheel"), QStringLiteral("slow")});
        QTRY_VERIFY_WITH_TIMEOUT(call.state() == QProcess::NotRunning, 10000);
        const QString out = QString::fromUtf8(call.readAllStandardOutput() + call.readAllStandardError());
        QVERIFY2(call.exitCode() == 0 && out.contains(QStringLiteral("done")) && out.contains(QStringLiteral("working")), qPrintable(out));
    }
};

QTEST_GUILESS_MAIN(ServerTest)
#include "tst_Server.moc"
