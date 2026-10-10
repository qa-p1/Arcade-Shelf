#include "platform/Backdrop.h"
#include "platform/FullscreenGuard.h"

#include <QJsonObject>
#include <QSignalSpy>
#include <QWindow>
#include <QtTest>
#include <limits>

using namespace Shelf;

class SurfaceEffectsTest final : public QObject {
    Q_OBJECT
private slots:
    void materialRegionStaysInsideTheNativeSurface()
    {
        QCOMPARE(Backdrop::boundedRegion({}, QSize(216, 76)), QRectF(0, 0, 216, 76));
        QCOMPARE(Backdrop::boundedRegion(QRectF(-4, -6, 220, 100), QSize(216, 76)), QRectF(0, 0, 216, 76));
        QCOMPARE(Backdrop::boundedRegion(QRectF(6, 6, 204, 64), QSize(216, 76)), QRectF(6, 6, 204, 64));
        QVERIFY(Backdrop::boundedRegion(QRectF(400, 0, 20, 20), QSize(216, 76)).isEmpty());
        QVERIFY(Backdrop::boundedRegion(QRectF(0, 0, -2, 20), QSize(216, 76)).isEmpty());
        QVERIFY(Backdrop::boundedRegion(QRectF(std::numeric_limits<qreal>::quiet_NaN(), 0, 20, 20), QSize(216, 76)).isEmpty());
        QCOMPARE(Backdrop::boundedRadius(99, QRectF(6, 6, 204, 64)), qreal(32));
        QCOMPARE(Backdrop::boundedRadius(-5, QRectF(6, 6, 204, 64)), qreal(0));
        QCOMPARE(Backdrop::boundedRadius(5, QRectF(0, 0, -2, 20)), qreal(0));
        QCOMPARE(Backdrop::boundedRadius(std::numeric_limits<qreal>::infinity(), QRectF(6, 6, 204, 64)), qreal(0));
    }

    void materialDetachAndHiddenSurfaceNeverClaimAnEffect()
    {
        Backdrop material;
        QSignalSpy changed(&material, &Backdrop::changed);
        QVERIFY(material.enabled());
        QVERIFY(!material.active());
        {
            QWindow window;
            window.resize(216, 76);
            material.attachWindow(&window);
            QVERIFY(!material.active());
            material.setRegion(QRectF(6, 6, 204, 64), 24);
            material.setDark(false);
            QCoreApplication::processEvents();
            QVERIFY(!material.active());
        }
        QVERIFY(!material.active());
        material.attachWindow(nullptr);
        material.setEnabled(false);
        QVERIFY(!material.enabled());
        QVERIFY(!material.active());
        QVERIFY(!changed.isEmpty());
    }

    void fullscreenBoundsExcludeOtherScreensAndWindowedApps()
    {
        const QRect left(-1920, 0, 1920, 1080), right(0, 0, 2560, 1440);
        QVERIFY(FullscreenGuard::coversScreen(left, left));
        QVERIFY(!FullscreenGuard::coversScreen(left, right));
        QVERIFY(!FullscreenGuard::coversScreen(QRect(-1920, 0, 1920, 1040), left));
        QVERIFY(FullscreenGuard::coversScreen(QRect(-1922, -2, 1924, 1084), left));
        QVERIFY(!FullscreenGuard::coversScreen({}, left));
        QVERIFY(!FullscreenGuard::coversScreen(left, {}));
        QVERIFY(!FullscreenGuard::coversScreen(QRect(-1920, 0, 1920, 1000), left, 5000));
    }

    void hyprlandFullscreenIsScopedToVisibleWorkspaceAndMonitor()
    {
        const QJsonArray monitors{
            QJsonObject{{"id", 1}, {"name", "DP-1"}, {"activeWorkspace", QJsonObject{{"id", 2}}}, {"specialWorkspace", QJsonObject{{"id", 0}}}},
            QJsonObject{{"id", 2}, {"name", "DP-2"}, {"activeWorkspace", QJsonObject{{"id", 3}}}, {"specialWorkspace", QJsonObject{{"id", -99}}}}
        };
        QJsonObject client{{"monitor", 1}, {"fullscreen", 2}, {"pid", 100}, {"mapped", true},
                           {"hidden", false}, {"workspace", QJsonObject{{"id", 2}}}};
        bool matched = false;
        QVERIFY(FullscreenGuard::hyprlandFullscreen(monitors, {client}, "DP-1", 200, &matched));
        QVERIFY(matched);
        QVERIFY(!FullscreenGuard::hyprlandFullscreen(monitors, {client}, "DP-2", 200));
        QVERIFY(!FullscreenGuard::hyprlandFullscreen(monitors, {client}, "absent", 200, &matched));
        QVERIFY(!matched);
        QVERIFY(!FullscreenGuard::hyprlandFullscreen(monitors, {client}, "DP-1", 100));
        client["fullscreen"] = 1; // Maximized is not fullscreen.
        QVERIFY(!FullscreenGuard::hyprlandFullscreen(monitors, {client}, "DP-1", 200));
        client["fullscreen"] = 2;
        client["workspace"] = QJsonObject{{"id", 4}};
        QVERIFY(!FullscreenGuard::hyprlandFullscreen(monitors, {client}, "DP-1", 200));
        client["workspace"] = QJsonObject{{"id", -99}};
        client["monitor"] = 2;
        QVERIFY(FullscreenGuard::hyprlandFullscreen(monitors, {client}, "DP-2", 200));
        client["mapped"] = false;
        QVERIFY(!FullscreenGuard::hyprlandFullscreen(monitors, {client}, "DP-2", 200));
        client["mapped"] = true; client["hidden"] = true;
        QVERIFY(!FullscreenGuard::hyprlandFullscreen(monitors, {client}, "DP-2", 200));
    }

    void malformedHyprlandSnapshotsCannotHideTheCapsule()
    {
        const QJsonObject monitor{{"id", 1}, {"name", "DP-1"}, {"activeWorkspace", QJsonObject{{"id", 2}}}};
        const QJsonObject good{{"monitor", 1}, {"fullscreen", 2}, {"pid", 100}, {"workspace", QJsonObject{{"id", 2}}}};
        QVERIFY(!FullscreenGuard::hyprlandFullscreen({}, {good}, "DP-1", 200));
        QVERIFY(!FullscreenGuard::hyprlandFullscreen({monitor}, {QJsonObject{{"fullscreen", "2"}}}, "DP-1", 200));
        QJsonArray excessive;
        for (int index = 0; index < 257; ++index) excessive.append(monitor);
        bool matched = true;
        QVERIFY(!FullscreenGuard::hyprlandFullscreen(excessive, {good}, "DP-1", 200, &matched));
        QVERIFY(!matched);
    }

    void currentHyprlandNamedWorkspacesAndVisibilityAreSupported()
    {
        const QJsonArray monitors{QJsonObject{{"id", 1}, {"name", "DP-1"},
            {"activeWorkspace", QJsonObject{{"address", "name:work"}, {"type", "named"}, {"name", "work"}}}}};
        QJsonObject client{{"monitor", 1}, {"fullscreen", 2}, {"pid", 100},
            {"workspace", QJsonObject{{"address", "name:work"}, {"type", "named"}}}};
        QVERIFY(FullscreenGuard::hyprlandFullscreen(monitors, {client}, "DP-1", 200));
        client["workspace"] = QJsonObject{{"address", "name:other"}};
        QVERIFY(!FullscreenGuard::hyprlandFullscreen(monitors, {client}, "DP-1", 200));
        client["visible"] = true;
        QVERIFY(FullscreenGuard::hyprlandFullscreen(monitors, {client}, "DP-1", 200));
        client["visible"] = false;
        QVERIFY(!FullscreenGuard::hyprlandFullscreen(monitors, {client}, "DP-1", 200));
        client["visible"] = true; client.remove("pid");
        QVERIFY(!FullscreenGuard::hyprlandFullscreen(monitors, {client}, "DP-1", 200));
    }

    void interactiveAndUserHiddenWindowsRemainExempt()
    {
        FullscreenGuard guard;
        QWindow window;
        guard.attachWindow(&window);
        guard.setCollapsed(false);
        guard.setRequestedVisible(true);
        QVERIFY(!guard.suppressed());
        guard.setRequestedVisible(false);
        guard.setCollapsed(true);
        QVERIFY(!guard.suppressed());
        guard.attachWindow(nullptr);
        QCoreApplication::processEvents();
        QVERIFY(!guard.suppressed());
    }
};

QTEST_MAIN(SurfaceEffectsTest)
#include "tst_SurfaceEffects.moc"
