// Conformance vectors (spec/vectors), the same files the Rust crate runs.
#include "ArcadeLink.h"

#include <QFile>
#include <QJsonDocument>
#include <QtTest>

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
    }
};

QTEST_GUILESS_MAIN(Vectors)
#include "tst_Vectors.moc"
