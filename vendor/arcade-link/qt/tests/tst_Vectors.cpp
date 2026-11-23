// Conformance vectors (spec/vectors), the same files the Rust crate runs.
#include "ArcadeLink.h"
#include "ArcadeAccelerator.h"
#include "ArcadeShortcuts.h"
#include "ArcadeInstall.h"
#include "ArcadeTrayHost.h"

#include <QFile>
#include <QJsonDocument>
#include <QtTest>
#include <QTemporaryDir>

#ifndef ARCADE_LINK_VECTORS
#define ARCADE_LINK_VECTORS "spec/vectors"
#endif

using namespace ArcadeLink;

static QJsonObject load(const QString &name)
{
    const QString dir = qEnvironmentVariable("ARCADE_LINK_VECTORS", QStringLiteral(ARCADE_LINK_VECTORS));
    QFile f(dir + QLatin1Char('/') + name);
    if (!f.open(QIODevice::ReadOnly)) qFatal("cannot open %s", qPrintable(f.fileName()));
    return QJsonDocument::fromJson(f.readAll()).object();
}

class Vectors : public QObject {
    Q_OBJECT
private slots:
    void onboardingMetadata()
    {
        QVERIFY(Ids::apps().contains(Ids::Shelf));
        QVERIFY(Ids::apps().contains(Ids::Find));
        QCOMPARE(appName(Ids::Shelf), QStringLiteral("Arcade Shelf"));
        QCOMPARE(appName(Ids::Find), QStringLiteral("Arcade Find"));
        QCOMPARE(releasesUrl(Ids::Shelf), QStringLiteral("https://github.com/qa-p1/Arcade-Shelf/releases"));
        QCOMPARE(releasesUrl(Ids::Find), QStringLiteral("https://github.com/qa-p1/Arcade-Find/releases"));
        QVERIFY(!appPitch(Ids::Shelf).isEmpty());
        QVERIFY(!appPitch(Ids::Find).isEmpty());
        QCOMPARE(osIdentifier(Ids::Box), QStringLiteral("dev.arcade.Box"));
        QVERIFY(osIdentifier(QStringLiteral("other.app")).isEmpty());
    }
    void wire()
    {
        const QJsonObject v = load(QStringLiteral("wire.json"));
        for (const auto &c : v.value(QStringLiteral("messages")).toArray()) {
            const QJsonObject o = c.toObject();
            const Kind k = classify(o.value(QStringLiteral("line")).toString().toUtf8());
            const QString name = o.value(QStringLiteral("name")).toString();
            QVERIFY2((k != Kind::Invalid) == o.value(QStringLiteral("valid")).toBool(), qPrintable(name));
            if (k == Kind::Invalid) continue;
            const QString kind = k == Kind::Request ? QStringLiteral("request") : k == Kind::Notification ? QStringLiteral("notification") : QStringLiteral("response");
            QCOMPARE(kind, o.value(QStringLiteral("kind")).toString());
        }
        for (const auto &c : v.value(QStringLiteral("negotiate")).toArray()) {
            const QJsonObject o = c.toObject();
            QList<int> cl, sv;
            for (const auto &x : o.value(QStringLiteral("client")).toArray()) cl << x.toInt();
            for (const auto &x : o.value(QStringLiteral("server")).toArray()) sv << x.toInt();
            const int expected = o.value(QStringLiteral("result")).isNull() ? -1 : o.value(QStringLiteral("result")).toInt();
            QCOMPARE(negotiate(cl, sv), expected);
        }
        QCOMPARE(qsizetype(v.value(QStringLiteral("maxLineBytes")).toInteger()), MaxLineBytes);
    }

    void content()
    {
        const QJsonObject v = load(QStringLiteral("content.json"));
        for (const auto &c : v.value(QStringLiteral("matches")).toArray()) {
            const QJsonObject o = c.toObject();
            QJsonObject content{{QStringLiteral("type"), o.value(QStringLiteral("offered"))}};
            if (o.contains(QStringLiteral("hints"))) content.insert(QStringLiteral("hints"), o.value(QStringLiteral("hints")));
            QVERIFY2(contentMatches(o.value(QStringLiteral("accept")).toString(), content) == o.value(QStringLiteral("expected")).toBool(),
                     qPrintable(QJsonDocument(o).toJson(QJsonDocument::Compact)));
        }
        for (const auto &c : v.value(QStringLiteral("kinds")).toArray()) {
            const QJsonObject o = c.toObject();
            QCOMPARE(fileKindForPath(o.value(QStringLiteral("path")).toString()), o.value(QStringLiteral("kind")).toString());
        }
        for (const auto &c : v.value(QStringLiteral("sendAs")).toArray()) {
            const QJsonObject o = c.toObject();
            QCOMPARE(typeFor(o.value(QStringLiteral("accepts")).toArray(), o.value(QStringLiteral("offered")).toString()),
                     o.value(QStringLiteral("send")).toString());
        }
    }

    void actions()
    {
        const QJsonObject v = load(QStringLiteral("actions.json"));
        for (const auto &c : v.value(QStringLiteral("actions")).toArray()) {
            const QJsonObject o = c.toObject();
            const QByteArray name = o.value(QStringLiteral("name")).toString().toUtf8();
            const QJsonObject action = o.value(QStringLiteral("action")).toObject();
            if (o.contains(QStringLiteral("surfaces"))) {
                QStringList want, got = actionSurfaces(action);
                for (const auto &s : o.value(QStringLiteral("surfaces")).toArray()) want << s.toString();
                want.sort();
                got.sort();
                QVERIFY2(got == want, name.constData());
            }
            if (o.contains(QStringLiteral("oneClick")))
                QVERIFY2(actionIsOneClick(action) == o.value(QStringLiteral("oneClick")).toBool(), name.constData());
            const QJsonObject rank = o.value(QStringLiteral("rank")).toObject();
            for (auto it = rank.begin(); it != rank.end(); ++it)
                QVERIFY2(actionRankFor(action, it.key()) == it.value().toInt(), (name + ": " + it.key().toUtf8()).constData());
        }
    }

    void catalog()
    {
        const QJsonObject v = load(QStringLiteral("catalog.json"));
        const auto check = [](const Catalog &c, const QJsonObject &o) {
            const auto s = [&o](const char *k) { return o.value(QLatin1String(k)).toString(); };
            const QString fn = s("fn");
            QJsonValue got;
            if (fn == u"name") got = c.name(s("id"));
            else if (fn == u"tagline") got = c.tagline(s("id"));
            else if (fn == u"releasesUrl") got = c.releasesUrl(s("id"));
            else if (fn == u"pair") got = c.pair(s("app"), s("peer"));
            else if (fn == u"appIds") got = QJsonArray::fromStringList(c.appIds());
            QVERIFY2(got == o.value(QStringLiteral("expect")), qPrintable(QJsonDocument(o).toJson(QJsonDocument::Compact)));
        };
        bool ok = false;
        const Catalog c = Catalog::parse(QJsonDocument(v.value(QStringLiteral("catalog")).toObject()).toJson(), &ok);
        QVERIFY(ok);
        for (const auto &l : v.value(QStringLiteral("lookups")).toArray()) check(c, l.toObject());
        for (const auto &l : v.value(QStringLiteral("fallback")).toArray()) check(Catalog{}, l.toObject());
    }

    void sharedCatalogCopy()
    {
        QTemporaryDir dir;
        const Locations loc = Locations::under(dir.path());
        QCOMPARE(Catalog::load(loc).name(QStringLiteral("arcade.box")), QStringLiteral("Arcade Box"));
        QVERIFY(!writeSharedCatalog(loc, "{\"apps\": []}"));
        QVERIFY(!writeSharedCatalog(loc, "not json"));
        const QByteArray text = R"({"apps": [{"id": "arcade.paint", "name": "Arcade Paint", "repo": "qa-p1/Arcade-paint"}]})";
        bool changed = false;
        QVERIFY(writeSharedCatalog(loc, text, &changed) && changed);
        QVERIFY(writeSharedCatalog(loc, text, &changed) && !changed);
        QVERIFY(!QFileInfo(loc.catalogPath()).absolutePath().startsWith(QFileInfo(loc.registry).absoluteFilePath()));
        QCOMPARE(Catalog::load(loc).name(QStringLiteral("arcade.paint")), QStringLiteral("Arcade Paint"));

        QCOMPARE(loadOfferPrefs(loc), QJsonObject{});
        const QJsonObject prefs{{QStringLiteral("pinned"), QJsonArray{QStringLiteral("arcade.clipboard/clipboard.add")}}};
        QVERIFY(saveOfferPrefs(loc, prefs));
        QCOMPARE(loadOfferPrefs(loc), prefs);
        QVERIFY(!QFileInfo(loc.offerPrefsPath()).absolutePath().startsWith(QFileInfo(loc.registry).absoluteFilePath()));
    }

    void offersVectors()
    {
        const QJsonObject v = load(QStringLiteral("offers.json"));
        QList<QJsonObject> cards;
        for (const auto &c : v.value(QStringLiteral("cards")).toArray()) cards << c.toObject();
        for (const auto &c : v.value(QStringLiteral("inputType")).toArray()) {
            const QJsonObject o = c.toObject();
            const QString got = offerInputType(o.value(QStringLiteral("items")).toArray());
            QCOMPARE(got, o.value(QStringLiteral("type")).toString());
        }
        for (const auto &c : v.value(QStringLiteral("cases")).toArray()) {
            const QJsonObject o = c.toObject();
            const QByteArray name = o.value(QStringLiteral("name")).toString().toUtf8();
            const QList<QJsonObject> got = offers(o.value(QStringLiteral("items")).toArray(), o.value(QStringLiteral("surface")).toString(),
                                                  cards, o.value(QStringLiteral("rules")).toObject(), o.value(QStringLiteral("prefs")).toObject());
            const QJsonArray want = o.value(QStringLiteral("expect")).toArray();
            QStringList gotRefs, wantRefs;
            for (const auto &g : got) gotRefs << g.value(QStringLiteral("app")).toString() + QLatin1Char('/') + g.value(QStringLiteral("action")).toString();
            for (const auto &w : want) wantRefs << w.toObject().value(QStringLiteral("ref")).toString();
            QVERIFY2(gotRefs == wantRefs, (name + ": " + gotRefs.join(QLatin1Char(',')).toUtf8()).constData());
            for (qsizetype i = 0; i < got.size(); ++i) {
                const QJsonObject g = got.at(i), w = want.at(i).toObject();
                const QByteArray where = name + ": " + wantRefs.at(i).toUtf8();
                for (const auto &[key, fallback] : {std::pair{"enabled", true}, {"preferred", false}, {"other", false}, {"choose", false}, {"pinned", false}})
                    QVERIFY2(g.value(QLatin1String(key)).toBool(false) == w.value(QLatin1String(key)).toBool(fallback), (where + " " + key).constData());
                for (const char *key : {"rank", "title", "reason", "install", "group", "oneClick"})
                    if (w.contains(QLatin1String(key)))
                        QVERIFY2(g.value(QLatin1String(key)) == w.value(QLatin1String(key)), (where + " " + key).constData());
            }
        }
    }

    void errors()
    {
        const QJsonObject v = load(QStringLiteral("errors.json"));
        for (const auto &c : v.value(QStringLiteral("messages")).toArray()) {
            const QJsonObject o = c.toObject();
            const qint64 limit = o.contains(QStringLiteral("limit")) ? o.value(QStringLiteral("limit")).toInteger() : -1;
            QCOMPARE(standardMessage(o.value(QStringLiteral("code")).toString(), o.value(QStringLiteral("app")).toString(),
                                     o.value(QStringLiteral("reason")).toString(), limit),
                     o.value(QStringLiteral("message")).toString());
        }
        for (const auto &c : v.value(QStringLiteral("limits")).toArray()) {
            const QJsonObject o = c.toObject();
            QCOMPARE(formatLimit(o.value(QStringLiteral("bytes")).toInteger()), o.value(QStringLiteral("text")).toString());
        }
    }

    void manifests()
    {
        const QJsonObject v = load(QStringLiteral("manifest.json"));
        for (const auto &c : v.value(QStringLiteral("valid")).toArray()) {
            const QJsonObject o = c.toObject();
            QJsonObject m;
            QString err;
            QVERIFY2(parseManifest(QJsonDocument(o.value(QStringLiteral("manifest")).toObject()).toJson(), &m, &err), qPrintable(err));
            const QJsonObject e = o.value(QStringLiteral("expect")).toObject();
            QCOMPARE(m.value(QStringLiteral("id")).toString(), e.value(QStringLiteral("id")).toString());
            QCOMPARE(m.value(QStringLiteral("actions")).toArray().size(), e.value(QStringLiteral("actions")).toInt());
            QCOMPARE(m.value(QStringLiteral("settings")).toObject().value(QStringLiteral("linkEnabled")).toBool(true), e.value(QStringLiteral("linkEnabled")).toBool());
            QCOMPARE(m.value(QStringLiteral("launch")).toObject().value(QStringLiteral("invoke")).isArray(), e.value(QStringLiteral("invoke")).toBool());
            if (e.contains(QStringLiteral("firstActionAvailable"))) {
                const QJsonObject a = m.value(QStringLiteral("actions")).toArray().first().toObject();
                QCOMPARE(a.value(QStringLiteral("available")).toBool(true), e.value(QStringLiteral("firstActionAvailable")).toBool());
                QCOMPARE(a.value(QStringLiteral("version")).toInt(1), e.value(QStringLiteral("firstActionVersion")).toInt());
            }
        }
        for (const auto &c : v.value(QStringLiteral("invalid")).toArray())
            QVERIFY(!parseManifest(QJsonDocument(c.toObject().value(QStringLiteral("manifest")).toObject()).toJson(), nullptr));
    }

    void accelerators()
    {
        const QJsonObject v = load(QStringLiteral("accelerators.json"));
        for (const auto &p : v.value(QStringLiteral("same")).toArray())
            QCOMPARE(normalizeAccelerator(p.toArray().at(0).toString()), normalizeAccelerator(p.toArray().at(1).toString()));
        for (const auto &p : v.value(QStringLiteral("different")).toArray())
            QVERIFY(normalizeAccelerator(p.toArray().at(0).toString()) != normalizeAccelerator(p.toArray().at(1).toString()));
        for (const auto &value : v.value(QStringLiteral("normalize")).toArray()) {
            const auto item = value.toObject();
            QCOMPARE(Accelerator::normalize(item.value(QStringLiteral("input")).toString()), item.value(QStringLiteral("expected")).toString());
        }
        for (const auto &value : v.value(QStringLiteral("display")).toArray()) {
            const auto item = value.toObject();
            const QStringList os{QStringLiteral("linux"), QStringLiteral("windows"), QStringLiteral("macos")};
            const int i = os.indexOf(item.value(QStringLiteral("os")).toString());
            QVERIFY(i >= 0);
            QCOMPARE(Accelerator::display(item.value(QStringLiteral("input")).toString(), static_cast<Accelerator::Platform>(i)), item.value(QStringLiteral("expected")).toString());
        }
        for (const auto &value : v.value(QStringLiteral("conflicts")).toArray()) {
            const auto item = value.toObject();
            QCOMPARE(Accelerator::conflicts(item.value(QStringLiteral("a")).toString(), item.value(QStringLiteral("b")).toString()), item.value(QStringLiteral("expected")).toBool());
        }
        for (const auto &value : v.value(QStringLiteral("invalid")).toArray()) {
            QString error;
            QVERIFY(Accelerator::normalize(value.toString(), &error).isEmpty());
            QVERIFY(!error.isEmpty());
        }
    }

    void shortcuts()
    {
        const auto vectors = load(QStringLiteral("shortcuts.json"));
        for (const auto &value : vectors.value(QStringLiteral("valid")).toArray()) {
            const auto item = value.toObject();
            const auto doc = item.value(item.contains(QStringLiteral("sheet")) ? QStringLiteral("sheet") : QStringLiteral("document")).toObject();
            const auto manifest = item.value(QStringLiteral("manifest")).toObject();
            QString error;
            QVERIFY2(Shortcuts::validate(doc, &error, manifest.isEmpty() ? nullptr : &manifest), qPrintable(error));
            QCOMPARE(Shortcuts::markdown(doc), item.value(QStringLiteral("markdown")).toString());
        }
        for (const auto &value : vectors.value(QStringLiteral("invalid")).toArray()) {
            const auto item = value.toObject();
            const auto manifest = item.value(QStringLiteral("manifest")).toObject();
            QString error;
            QVERIFY2(!Shortcuts::validate(item.value(item.contains(QStringLiteral("sheet")) ? QStringLiteral("sheet") : QStringLiteral("document")).toObject(), &error, manifest.isEmpty() ? nullptr : &manifest), qPrintable(item.value(QStringLiteral("name")).toString()));
            QVERIFY(!error.isEmpty());
        }
    }

    void receipts()
    {
        const auto vectors = load(QStringLiteral("receipts.json"));
        for (const auto &value : vectors.value(QStringLiteral("valid")).toArray()) {
            QString error;
            QVERIFY2(validateReceipt(value.toObject().value(QStringLiteral("receipt")).toObject(), &error), qPrintable(error));
        }
        for (const auto &value : vectors.value(QStringLiteral("invalid")).toArray()) {
            QString error;
            QVERIFY2(!validateReceipt(value.toObject().value(QStringLiteral("receipt")).toObject(), &error), qPrintable(value.toObject().value(QStringLiteral("name")).toString()));
            QVERIFY(!error.isEmpty());
        }
    }

    void methods()
    {
        const auto vectors = load(QStringLiteral("methods.json"));
        QCOMPARE(vectors.value(QStringLiteral("protocol")).toInt(), ProtocolVersion);
        for (const auto &value : vectors.value(QStringLiteral("cases")).toArray()) {
            const auto item = value.toObject();
            QCOMPARE(classify(QJsonDocument(item.value(QStringLiteral("request")).toObject()).toJson(QJsonDocument::Compact)), Kind::Request);
            const auto response = item.value(QStringLiteral("response")).toObject();
            QCOMPARE(classify(QJsonDocument(response).toJson(QJsonDocument::Compact)), Kind::Response);
            if (response.contains(QStringLiteral("error"))) {
                const auto error = Error::fromJson(response.value(QStringLiteral("error")).toObject());
                QCOMPARE(error.code, response.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString());
            }
        }
    }

    void trayhost()
    {
        const auto vectors = load(QStringLiteral("trayhost.json"));
        QCOMPARE(vectors.value(QStringLiteral("startupCapMs")).toInt(), TrayStartupCapMs);
        QCOMPARE(vectors.value(QStringLiteral("restartGraceMs")).toInt(), TrayRestartGraceMs);
        for (const auto &value : vectors.value(QStringLiteral("sequences")).toArray()) {
            const auto sequence = value.toObject();
            TrayHostStateMachine policy;
            policy.begin(0, true);
            for (const auto &eventValue : sequence.value(QStringLiteral("events")).toArray()) {
                const auto event = eventValue.toObject();
                const auto now = event.value(QStringLiteral("atMs")).toInteger();
                policy.advance(now);
                if (event.value(QStringLiteral("startup")).toBool()) policy.begin(now, true);
                if (event.contains(QStringLiteral("hosted"))) policy.host(now, event.value(QStringLiteral("hosted")).toBool(), event.value(QStringLiteral("restarting")).toBool());
                if (event.value(QStringLiteral("closed")).toBool()) policy.closed(now);
                if (event.value(QStringLiteral("excluded")).toBool() || (event.contains(QStringLiteral("linkEnabled")) && !event.value(QStringLiteral("linkEnabled")).toBool())) policy.setAllowed(now, false);
                QCOMPARE(policy.state(), event.value(QStringLiteral("expect")).toString());
            }
        }
    }
};

QTEST_GUILESS_MAIN(Vectors)
#include "tst_Vectors.moc"
