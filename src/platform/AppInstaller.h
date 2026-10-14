// The install standard's app side (Arcade Link SPEC §8.6): the self-installing
// AppImage dialog, the macOS move to Applications, and the --install,
// --uninstall, --repair and --integration-status commands. File work goes
// through ArcadeLink::ArcadeInstall; this file owns consent and wording.
#pragma once
#include "ArcadeInstall.h"
#include <functional>

namespace ArcadeApp {
struct InstallSpec {
    ArcadeLink::ArcadeInstall::AppInfo app;
    QString version;
    QString bundleName;   // macOS: "Arcade Shelf.app"
    QString stateDir;     // where "Don't ask again" is remembered
    QStringList dataFolders;
    bool startAtLoginDefault = false;
    // Writes PNG/SVG icons into `dir` (a private temporary folder).
    std::function<QList<ArcadeLink::ArcadeInstall::Icon>(const QString &dir)> writeIcons;
    // Asks a running copy to quit before files are removed.
    std::function<bool(QString *error)> quitRunning;
};

// True for --install, --uninstall, --repair and --integration-status.
bool isIntegrationCommand(const QStringList &args);
// True when the command needs no window (status, repair, silent paths).
bool isHeadlessIntegrationCommand(const QStringList &args);
// Runs one integration command; returns the process exit code.
int runIntegrationCommand(const InstallSpec &spec, const QStringList &args);
// Before the first window: offer to install a downloaded copy. True when
// this process should exit (it relaunched the installed copy or handed off).
bool offerInstall(const InstallSpec &spec, const QStringList &args);
// The manifest's `install` echo: appimage, macos-bundle, windows-installer,
// manual or dev.
QString installMethod(const QString &id);
} // namespace ArcadeApp
