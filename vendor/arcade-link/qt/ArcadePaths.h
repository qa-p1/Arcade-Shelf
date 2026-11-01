// App folders and the shared diagnostics header (STANDARD.md §10), matching
// the Rust crate's paths::{app_dirs, legacy_dirs, migrate_app_dirs} and
// diagnostics::Header. The legacy table is generated from spec/app-dirs.json.
#pragma once

#include "ArcadeLink.h"

#include <QList>
#include <QPair>
#include <QString>

namespace ArcadeLink {

// The current user's base folders; under() roots them for isolated runs.
struct UserDirs {
    QString home, config, data, cache, roaming, local;
    static UserDirs discover();
    static UserDirs under(const QString &root);
    QString base(const QString &name) const;   // "config", "roaming", "support", …
};

// ~/.config/arcade/<app>, ~/.local/share/arcade/<app>, ~/.cache/arcade/<app>
// on Linux; %APPDATA%\Arcade\<App> (+ %LOCALAPPDATA% cache) on Windows;
// ~/Library/Application Support/Arcade/<App> (+ Caches) on macOS.
struct AppDirs {
    QString config, data, cache;
    QString settingsFile() const { return config + QStringLiteral("/settings.json"); }
    QString get(const QString &kind) const;     // "config", "data" or "cache"
};
AppDirs appDirs(const QString &appId);
AppDirs appDirs(const UserDirs &user, const QString &platform, const QString &appId);

struct LegacyDir {
    QString kind;   // "config", "data" or "cache"
    QString path;
    QString into;   // subfolder of the new folder its files move into; empty for the folder itself
    QString target(const AppDirs &dirs) const;
};
QList<LegacyDir> legacyDirs(const QString &appId);
QList<LegacyDir> legacyDirs(const UserDirs &user, const QString &platform, const QString &appId);

struct Migration {
    QList<QPair<QString, QString>> moved;       // (from, to)
    QList<QPair<QString, QString>> kept;        // (legacy folder, reason)
    bool isEmpty() const { return moved.isEmpty() && kept.isEmpty(); }
};
// Copy, verify byte for byte, then remove each existing legacy folder. A
// conflict, a link or an I/O error leaves that folder in place. Idempotent.
Migration migrateAppDirs(const QString &appId);
Migration migrateAppDirs(const UserDirs &user, const QString &platform, const QString &appId);

// The first lines of "Copy diagnostics": app, version, channel, OS, desktop,
// Link protocol and peers, engines. App-specific lines follow.
struct DiagnosticsHeader {
    QString appId, version, channel, os, desktop;
    bool linkEnabled = true;
    QList<QPair<QString, QString>> peers;      // (Link id, "running 0.4.0" …)
    QList<QPair<QString, QString>> engines;    // (name, status)
    static DiagnosticsHeader create(const QString &appId, const QString &version, const QString &channel);
    void peersFrom(const Locations &locations);
    QString render() const;
};

} // namespace ArcadeLink
