#include "Startup.h"
#include <QTest>

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
};

QTEST_GUILESS_MAIN(MemoryTest)
#include "tst_Memory.moc"
