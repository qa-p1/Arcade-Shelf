// Install/receipt helpers, mirroring arcade_link::{receipt,install}. Every path
// is explicit. Run all I/O on a worker, after caller-owned consent. No dialogs,
// shell profile edits or quarantine removal occur here; the only processes an
// install stops are this app's own pre-installer copies it replaces.
#pragma once
#include "ArcadeLink.h"
#include <QProcessEnvironment>

namespace ArcadeLink {
bool validateReceipt(const QJsonObject &receipt, QString *error = nullptr);
bool validateNativeReceiptPaths(const QJsonObject &receipt,
                                QString *error = nullptr);
class ReceiptStore {
public:
  explicit ReceiptStore(const Locations &locations);
  static ReceiptStore discover(); // ARCADE_HOME / native data root
  QString path(const QString &id) const;
  QString directory() const { return m_directory; }
  // Missing receipt is success with an empty object. Malformed receipts fail.
  bool read(const QString &id, QJsonObject *out,
            QString *error = nullptr) const;
  bool write(const QJsonObject &receipt, QString *error = nullptr) const;
  bool remove(const QString &id, QString *error = nullptr) const;

private:
  QString m_directory;
};

class ArcadeInstall {
public:
  struct Environment {
    QString home, data, config, applications, bin, localAppData;
    Locations locations;
    QString path; // inspect only, never mutate
    QString windowsEnvironmentKey;
    static Environment under(const QString &root);
    static bool discover(Environment *out, QString *error = nullptr);
    bool validate(QString *error = nullptr) const;
    ReceiptStore receipts() const { return ReceiptStore(locations); }
  };
  struct Runtime {
    QString executable, appimage, appdir;
    bool devBuild = false;
    static Runtime discover();
    bool isAppImage() const;
    QString runningFrom() const;
  };
  struct Detection {
    QString runningFrom, installedPath;
    bool isAppImage = false;
    QJsonObject receipt;
    QString state; // notInstalled/installedHere/installedElsewhere/devBuild
    QJsonObject toJson() const;
  };
  struct AppInfo {
    QString id, name, filename, desktopId, cliName;
    QStringList backgroundArgs, legacyDesktopIds;
    QString autostartId;
  };
  struct Icon {
    QString source;
    int pixels = 0;
  }; // 0 = scalable SVG; 16–512 = PNG
  struct InstallOptions {
    AppInfo app;
    Runtime runtime;
    QString version, channel = QStringLiteral("stable"),
                     managedBy = QStringLiteral("self");
    QList<Icon> icons;
    bool startAtLogin = false, removeDownload = false, refreshCaches = true;
  };
  struct UninstallOptions {
    bool removeData = false;
    QStringList dataFolders;
  };

  explicit ArcadeInstall(Environment environment)
      : m_environment(std::move(environment)) {}
  const Environment &environment() const { return m_environment; }
  bool detect(const QString &id, const Runtime &runtime, Detection *out,
              QString *error = nullptr) const;
  static bool isDevBuild(const QString &executable);
  static bool shouldPrompt(const Detection &detection, const QStringList &args,
                           bool noPrompt = false, bool dismissed = false);
  bool install(const InstallOptions &options, QJsonObject *receipt,
               QString *error = nullptr) const;
  bool repair(const AppInfo &app, const QString &movedTo, QJsonObject *receipt,
              QString *error = nullptr) const;
  // Returns success even when already absent; removed reports whether a receipt
  // existed.
  bool uninstall(const QString &id, const UninstallOptions &options,
                 bool *removed = nullptr, QString *error = nullptr) const;
  QJsonObject integrationStatus(const QString &id, const QString &shell,
                                QString *error = nullptr) const;
  static QString pathHint(const QString &bin, const QString &shell);
  static QJsonObject strings(); // shared assets/strings/install.json
  static QString formatString(const QString &pattern, const QString &app,
                              const QString &version, const QString &from,
                              const QString &to);
  // Starts the installed copy with the original args and the user's
  // environment (see childEnvironment). From an AppImage, and on Windows, it
  // starts a detached successor and the caller exits on success; elsewhere on
  // Unix it replaces the process.
  static bool reexec(const QString &path, const QStringList &originalArgs,
                     QString *error = nullptr);
  // `environment` without an AppImage runtime's mount paths and variables.
  static QProcessEnvironment childEnvironment(QProcessEnvironment environment);
  // Older pre-installer copies this app's launcher or login entry still run.
  QStringList legacyCopies(const AppInfo &app) const;
  // Asks a running copy of `id` to quit over the Link (before its replacement
  // starts) and waits up to timeoutMs for its endpoint to go away.
  static bool quitRunning(const Locations &locations, const QString &id,
                          int timeoutMs, bool *wasRunning = nullptr,
                          QString *error = nullptr);
  // A login entry that will start the app: present and not switched off with
  // Hidden=true or X-GNOME-Autostart-enabled=false.
  static bool autostartEnabled(const QString &path);
  // Whether `path` is inside the user's Downloads folder.
  bool isDownload(const QString &path) const;

  bool writeWindowsShim(const QString &cliName, const QString &executable,
                        QString *shim = nullptr,
                        QString *error = nullptr) const;
  bool removeWindowsShim(const QString &cliName,
                         QString *error = nullptr) const;
  bool addWindowsPath(const QString &bin, QString *error = nullptr) const;
  bool removeWindowsPathIfLast(const QString &bin,
                               QString *error = nullptr) const;
  static bool isTranslocated(const QString &path);
  static QString bundleFor(const QString &executable);
  bool inApplications(const QString &bundle) const;
  bool moveToApplications(const QString &bundle, const QString &destination,
                          QString *installed, QString *error = nullptr) const;
  QString cliSymlinkOffer(const QString &cliName,
                          const QString &executable) const;
  bool installCli(const QString &cliName, const QString &executable,
                  QString *link = nullptr, QString *error = nullptr) const;

private:
  Environment m_environment;
};
} // namespace ArcadeLink
