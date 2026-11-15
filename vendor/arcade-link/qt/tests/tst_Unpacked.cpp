// arcade_relaunch_unpacked() (qt/arcade_unpacked.h), the C twin of runtime::relaunch_unpacked (RT-7), modelled on
// the Rust test an_appimage_app_relaunches_from_its_unpacked_copy_once: the code under test runs in a helper process
// whose /proc/self/exe is inside a fake AppDir, with an isolated XDG_DATA_HOME.
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>
#include "arcade_unpacked.h"
#include <sys/stat.h>

class UnpackedTest : public QObject {
  Q_OBJECT
#if defined(__linux__)
  QTemporaryDir m_tmp;
  QString m_root, m_mount, m_image, m_data, m_marker;

  void writeFile(const QString &path, const QByteArray &body, QFileDevice::Permissions perms) {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(body);
    f.close();
    QVERIFY(f.setPermissions(perms));
  }

  static constexpr QFileDevice::Permissions Exec = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
      QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther;
  static constexpr QFileDevice::Permissions Plain = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup;

  // A fake mounted AppDir: the probe as usr/bin/probe, an AppRun script that records how it was started and exits 7.
  void buildAppDir(const QString &dir) {
    QDir().mkpath(dir + "/usr/bin");
    QFile::remove(dir + "/usr/bin/probe");
    QVERIFY(QFile::copy(QStringLiteral(ARCADE_UNPACKED_PROBE), dir + "/usr/bin/probe"));
    QVERIFY(QFile::setPermissions(dir + "/usr/bin/probe", Exec));
    writeFile(dir + "/AppRun",
              "#!/bin/sh\n"
              "{ echo \"APPDIR=$APPDIR\"; echo \"UNPACKED=$ARCADE_UNPACKED\"; echo \"APPIMAGE=$APPIMAGE\"; echo \"ARENAS=$MALLOC_ARENA_MAX\";\n"
              "  for a in \"$@\"; do echo \"ARG=$a\"; done; } > \"$MARKER\"\n"
              "exit 7\n",
              Exec);
    writeFile(dir + "/usr/data.txt", "payload", Plain);
    writeFile(dir + "/usr/bin/tool.sh", "#!/bin/sh\n", Exec);
    QVERIFY(QFile::link("bin/probe", dir + "/usr/link"));
    QVERIFY(QFile::link("/nonexistent/target", dir + "/usr/dangling"));
  }

  QProcessEnvironment baseEnv() const {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral(ARCADE_UNPACKED_ENV));
    env.remove("MALLOC_ARENA_MAX");
    env.insert("APPIMAGE", m_image);
    env.insert("APPDIR", m_mount);
    env.insert("XDG_DATA_HOME", m_data);
    env.insert("MARKER", m_marker);
    return env;
  }

  // Runs the probe from `exe`; retries while exec says "text file busy" (a just-written executable).
  int runProbe(const QString &exe, const QProcessEnvironment &env, const QStringList &args = {}, QByteArray *err = nullptr) {
    for (int i = 0; i < 100; ++i) {
      QProcess p;
      p.setProcessEnvironment(env);
      p.start(exe, args);
      if (!p.waitForFinished(20000) || p.error() == QProcess::FailedToStart) {
        QTest::qWait(20);
        continue;
      }
      if (err)
        *err = p.readAllStandardError();
      return p.exitCode();
    }
    return -999;
  }

  QStringList copies() const {
    return QDir(m_data + "/arcade/runtime").entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
  }
  QStringList marker() const {
    QFile f(m_marker);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts) : QStringList();
  }

private slots:
  void init() {
    QVERIFY(m_tmp.isValid());
    // Every test gets a fresh world.
    m_root = QDir(m_tmp.path()).canonicalPath() + "/" + QString::fromLatin1(QTest::currentTestFunction());
    m_mount = m_root + "/mount";
    m_image = m_root + "/App.AppImage";
    m_data = m_root + "/data";
    m_marker = m_root + "/marker";
    QDir().mkpath(m_root);
    writeFile(m_image, "image", Plain);
    buildAppDir(m_mount);
  }

  void relaunchesOnceFromTheCopyAndReturnsItsExitCode() {
    QByteArray err;
    QCOMPARE(runProbe(m_mount + "/usr/bin/probe", baseEnv(), {"a", "b c"}, &err), 7);
    QCOMPARE(copies().size(), 1);
    const QString name = copies().first();
    QVERIFY2(QRegularExpression("^test-1\\.2\\.3-[0-9a-f]{16}$").match(name).hasMatch(), qPrintable(name));
    const QString copy = m_data + "/arcade/runtime/" + name;
    QVERIFY(QFile::exists(copy + "/.complete"));
    const QStringList m = marker();
    QVERIFY2(m.contains("APPDIR=" + copy), qPrintable(m.join('|')));
    QVERIFY(m.contains("UNPACKED=1"));
    QVERIFY(m.contains("APPIMAGE=" + m_image));
    QVERIFY(m.contains("ARG=a"));
    QVERIFY(m.contains("ARG=b c"));
    QVERIFY(m.contains("ARENAS=")); // not set by us, not invented
    QCOMPARE(QFile(copy + "/usr/data.txt").size(), qint64(7));
  }

  void aSecondCallReusesTheCopy() {
    QCOMPARE(runProbe(m_mount + "/usr/bin/probe", baseEnv()), 7);
    QCOMPARE(copies().size(), 1);
    const QString copy = m_data + "/arcade/runtime/" + copies().first();
    writeFile(copy + "/usr/kept", "x", Plain);
    QFile::remove(m_marker);
    // The source can no longer be copied (a FIFO is not a regular file): a second start succeeds only if it copies nothing.
    QVERIFY(QFile::remove(m_mount + "/usr/data.txt"));
    QCOMPARE(::mkfifo((m_mount + "/usr/data.txt").toLocal8Bit().constData(), 0644), 0);
    QCOMPARE(runProbe(m_mount + "/usr/bin/probe", baseEnv()), 7);
    QVERIFY(QFile::exists(copy + "/usr/kept")); // not copied again
    QVERIFY(QFile::exists(m_marker));
    QCOMPARE(copies().size(), 1); // no tmp folder left, no second folder
    QVERIFY(QFile::exists(copy + "/.complete"));
    QVERIFY(QRegularExpression("^test-1\\.2\\.3-[0-9a-f]{16}$").match(copies().first()).hasMatch());
  }

  void aUserArenaCapIsPassedOn() {
    auto env = baseEnv();
    env.insert("MALLOC_ARENA_MAX", "4");
    QCOMPARE(runProbe(m_mount + "/usr/bin/probe", env), 7);
    QVERIFY(marker().contains("ARENAS=4"));
  }

  void withoutWaitItReturnsZeroAtOnce() {
    auto env = baseEnv();
    env.insert("PROBE_WAIT", "0");
    QCOMPARE(runProbe(m_mount + "/usr/bin/probe", env), 0);
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(m_marker) && marker().size() >= 4, 10000); // the copy still ran
  }

  void unpackedEnvironmentKeepsItRunning() {
    auto env = baseEnv();
    env.insert(QStringLiteral(ARCADE_UNPACKED_ENV), "1");
    QCOMPARE(runProbe(m_mount + "/usr/bin/probe", env), 42);
    QVERIFY(!QFile::exists(m_marker));
    QVERIFY(copies().isEmpty());
  }

  void notInAnAppImageKeepsItRunning() {
    for (const char *name : {"APPIMAGE", "APPDIR"}) {
      auto env = baseEnv();
      env.remove(name);
      QCOMPARE(runProbe(m_mount + "/usr/bin/probe", env), 42);
      env.insert(name, "");
      QCOMPARE(runProbe(m_mount + "/usr/bin/probe", env), 42);
    }
    QVERIFY(copies().isEmpty());
  }

  void anExecutableOutsideAppDirKeepsItRunning() {
    buildAppDir(m_root + "/other");
    auto env = baseEnv();
    QCOMPARE(runProbe(m_root + "/other/usr/bin/probe", env), 42);
    // A string prefix is not inside: /…/mountain/… is not under /…/mount.
    buildAppDir(m_root + "/mountain");
    QCOMPARE(runProbe(m_root + "/mountain/usr/bin/probe", env), 42);
    QVERIFY(copies().isEmpty());
    QVERIFY(!QFile::exists(m_marker));
    // A trailing slash on APPDIR still means the same directory.
    env.insert("APPDIR", m_mount + "/");
    QCOMPARE(runProbe(m_mount + "/usr/bin/probe", env), 7);
  }

  void aChangedImageGetsANewFolder() {
    QCOMPARE(runProbe(m_mount + "/usr/bin/probe", baseEnv()), 7);
    QCOMPARE(copies().size(), 1);
    const QDateTime later = QFileInfo(m_image).lastModified().addSecs(60);
    QFile image(m_image);
    QVERIFY(image.open(QIODevice::ReadWrite));
    QVERIFY(image.setFileTime(later, QFileDevice::FileModificationTime));
    image.close();
    QCOMPARE(runProbe(m_mount + "/usr/bin/probe", baseEnv()), 7);
    QCOMPARE(copies().size(), 2);
    // A different size does too.
    writeFile(m_image, "image, longer", Plain);
    QCOMPARE(runProbe(m_mount + "/usr/bin/probe", baseEnv()), 7);
    QCOMPARE(copies().size(), 3);
    for (const QString &c : copies())
      QVERIFY2(QRegularExpression("^test-1\\.2\\.3-[0-9a-f]{16}$").match(c).hasMatch(), qPrintable(c));
  }

  void symlinksAndPermissionsSurviveTheCopy() {
    QVERIFY(QFile::setPermissions(m_mount + "/usr/data.txt", QFileDevice::ReadOwner));
    QCOMPARE(runProbe(m_mount + "/usr/bin/probe", baseEnv()), 7);
    const QString copy = m_data + "/arcade/runtime/" + copies().first();
    const QFileInfo link(copy + "/usr/link");
    QVERIFY(link.isSymLink());
    QCOMPARE(link.symLinkTarget(), copy + "/usr/bin/probe");
    QCOMPARE(QFile::symLinkTarget(copy + "/usr/dangling"), QString("/nonexistent/target"));
    QVERIFY(QFileInfo(copy + "/usr/dangling").isSymLink());
    QVERIFY(QFileInfo(copy + "/AppRun").permission(QFileDevice::ExeOwner));
    QVERIFY(QFileInfo(copy + "/usr/bin/tool.sh").permission(QFileDevice::ExeOwner));
    QVERIFY(!QFileInfo(copy + "/usr/data.txt").permission(QFileDevice::ExeOwner));
    struct stat st;
    QCOMPARE(::stat((copy + "/usr/data.txt").toLocal8Bit().constData(), &st), 0);
    QCOMPARE(int(st.st_mode & 07777), 0400);
    QCOMPARE(::stat((copy + "/usr/bin/tool.sh").toLocal8Bit().constData(), &st), 0);
    QCOMPARE(int(st.st_mode & 07777), 0755);
    QCOMPARE(::stat((copy + "/usr").toLocal8Bit().constData(), &st), 0);
    struct stat src;
    QCOMPARE(::stat((m_mount + "/usr").toLocal8Bit().constData(), &src), 0);
    QCOMPARE(int(st.st_mode & 07777), int(src.st_mode & 07777));
  }

  void aMissingAppRunKeepsItRunning() {
    QVERIFY(QFile::remove(m_mount + "/AppRun"));
    QCOMPARE(runProbe(m_mount + "/usr/bin/probe", baseEnv()), 42);
    QVERIFY(!QFile::exists(m_marker));
  }

  void anUnwritableDataDirReportsAndKeepsRunning() {
    writeFile(m_data, "a file, not a folder", Plain);
    QByteArray err;
    QCOMPARE(runProbe(m_mount + "/usr/bin/probe", baseEnv(), {}, &err), 42);
    QVERIFY2(err.startsWith("arcade-link: running from the AppImage mount ("), err.constData());
    QVERIFY(!QFile::exists(m_marker));
  }

  void folderNamesAreTheOnesToolsRecognises() {
    // runtime_gc: <app-without-arcade.>-<version>-<16 hex>; copies in progress are .<name>.tmp-<pid>.
    QCOMPARE(runProbe(m_mount + "/usr/bin/probe", baseEnv()), 7);
    QVERIFY(QRegularExpression("^[a-z]+-[0-9.]+-[0-9a-f]{16}$").match(copies().first()).hasMatch());
    QVERIFY(!copies().first().startsWith('.'));
  }
#endif

private slots:
  void nonLinuxDoesNothing() {
#if !defined(__linux__)
    char *argv[] = {nullptr};
    QCOMPARE(arcade_relaunch_unpacked("arcade.test", "1.0.0", 1, argv), -1);
#else
    QSKIP("Linux build: covered by the other tests");
#endif
  }
};

QTEST_GUILESS_MAIN(UnpackedTest)
#include "tst_Unpacked.moc"
