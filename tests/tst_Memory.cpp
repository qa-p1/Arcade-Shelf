#include "HideRelease.h"
#include "Startup.h"
#include <QSignalSpy>
#include <QTest>

using Shelf::HideRelease;
using Shelf::startsResident;

class MemoryTest final : public QObject {
    Q_OBJECT
private slots:
    void startsThatKeepAResidentRunUnpacked_data()
    {
        QTest::addColumn<QStringList>("arguments");
        QTest::addColumn<bool>("resident");
        QTest::newRow("plain launch") << QStringList{} << true;
        QTest::newRow("login start") << QStringList{"--background"} << true;
        QTest::newRow("show") << QStringList{"--show"} << true;
        QTest::newRow("toggle") << QStringList{"--toggle"} << true;
        QTest::newRow("settings") << QStringList{"--settings"} << true;
        QTest::newRow("add") << QStringList{"--add", "/tmp/a file"} << true;
        QTest::newRow("add=") << QStringList{"--add=/tmp/a file"} << true;
        QTest::newRow("restart handoff") << QStringList{"--wait-for-exit", "1234", "--background"} << true;
        QTest::newRow("restart handoff=") << QStringList{"--wait-for-exit=1234", "--background"} << true;
        QTest::newRow("show and settings") << QStringList{"--show", "--settings"} << true;
        // One-shot commands never keep a process of their own.
        QTest::newRow("version") << QStringList{"--version"} << false;
        QTest::newRow("short version") << QStringList{"-v"} << false;
        QTest::newRow("manifest") << QStringList{"--arcade-manifest"} << false;
        QTest::newRow("invoke") << QStringList{"--arcade-invoke"} << false;
        QTest::newRow("help") << QStringList{"--help"} << false;
        QTest::newRow("short help") << QStringList{"-h"} << false;
        QTest::newRow("status") << QStringList{"--status"} << false;
        QTest::newRow("quit") << QStringList{"--quit"} << false;
        QTest::newRow("hide") << QStringList{"--hide"} << false;
        QTest::newRow("restart") << QStringList{"--restart"} << false;
        QTest::newRow("install") << QStringList{"--install"} << false;
        QTest::newRow("silent install") << QStringList{"--install", "--silent", "--no-launch"} << false;
        QTest::newRow("uninstall") << QStringList{"--uninstall", "--remove-data"} << false;
        QTest::newRow("repair") << QStringList{"--repair"} << false;
        QTest::newRow("integration status") << QStringList{"--integration-status"} << false;
        // A command that only talks to the running instance is one-shot even next to a start option.
        QTest::newRow("quit with show") << QStringList{"--show", "--quit"} << false;
        QTest::newRow("hide with background") << QStringList{"--background", "--hide"} << false;
        // A malformed command line reports its error from the process that ran it.
        QTest::newRow("option without install") << QStringList{"--silent"} << false;
        QTest::newRow("channel without install") << QStringList{"--channel", "nightly"} << false;
        QTest::newRow("add without a path") << QStringList{"--add"} << false;
        QTest::newRow("add= without a path") << QStringList{"--add="} << false;
        QTest::newRow("unknown option") << QStringList{"--bogus"} << false;
        QTest::newRow("stray argument") << QStringList{"--show", "file.txt"} << false;
    }
    void startsThatKeepAResidentRunUnpacked()
    {
        QFETCH(QStringList, arguments);
        QFETCH(bool, resident);
        QCOMPARE(startsResident(arguments), resident);
    }

    void aFinalHideReleasesOnceAndNeverRepeats()
    {
        HideRelease release(30);
        QSignalSpy released(&release, &HideRelease::released);
        release.setOpen(true);
        QVERIFY(!release.pending());
        release.setOpen(false);
        QVERIFY(release.pending());
        QVERIFY(released.wait(2000));
        QVERIFY(!release.pending());
        // No periodic timer while hidden: nothing follows the one release.
        QTest::qWait(150);
        QCOMPARE(released.size(), 1);
    }
    void aReopenBeforeTheDelayCancelsTheRelease()
    {
        HideRelease release(60);
        QSignalSpy released(&release, &HideRelease::released);
        release.setOpen(true);
        release.setOpen(false);
        QVERIFY(release.pending());
        QTest::qWait(10);
        release.setOpen(true);
        QVERIFY(!release.pending());
        QTest::qWait(200);
        QCOMPARE(released.size(), 0);
        // The next hide starts a fresh delay.
        release.setOpen(false);
        QVERIFY(released.wait(2000));
        QCOMPARE(released.size(), 1);
    }
    void startingHiddenOrRepeatingAStateReleasesNothing()
    {
        HideRelease release(30);
        QSignalSpy released(&release, &HideRelease::released);
        release.setOpen(false);
        QVERIFY(!release.pending());
        release.setOpen(true);
        release.setOpen(true);
        release.setOpen(false);
        QTest::qWait(10);
        // A second "hidden" must not push the pending release back.
        release.setOpen(false);
        QVERIFY(released.wait(2000));
        QTest::qWait(100);
        QCOMPARE(released.size(), 1);
    }
    void theDefaultDelayIsTwoSeconds()
    {
        QCOMPARE(Shelf::ReleaseDelayMs, 2000);
    }
};

QTEST_GUILESS_MAIN(MemoryTest)
#include "tst_Memory.moc"
