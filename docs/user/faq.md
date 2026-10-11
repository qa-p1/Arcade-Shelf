---
title: Arcade Shelf questions
order: 5
since: 0.1.1
---

# Arcade Shelf questions

## Does Shelf copy files when I collect them?

No. Existing files and folders are held as references, so they stay in place. Shelf does own raw images and temporary outputs returned by an app. Removing a reference never deletes its original file.

## How long do items stay in Quick Shelf?

Quick Shelf is temporary, and unpinned items in temporary collections expire after 30 days by default. Pin a collection to retain it, or change **Settings → Storage → Retention**. A value of 0 disables expiration.

## Where is Shelf data stored?

Collection metadata and preferences are stored in a local SQLite database under Qt's per-user application-data directory. Owned content is kept in Shelf's private storage. The default owned-content quota is 512 MiB.

## Does Shelf have clipboard history?

No. **Ctrl+V** explicitly collects the current supported clipboard content; Shelf does not poll or save clipboard history.

## Which desktop has been tested?

Shelf 0.1.1 was checked on a real Hyprland 0.56.2 desktop running Arch Linux. GNOME, KDE, Sway, fullscreen behavior, multi-monitor/DPI handling and native drag-out still need real-desktop checks. Windows and macOS builds pass CI, but have **not been run interactively on real Windows or macOS desktops**. See [project status](../STATUS.md).
