---
title: Install and start Arcade Shelf
order: 2
since: 0.2.0
---

# Install and start Arcade Shelf

## Install with Arcade Tools

Open **Arcade Tools → Apps**, then install, update or remove Arcade Shelf there. Tools can show Shelf's tray controls inside the shared Arcade tray icon. Shelf keeps its own tray icon when Tools is not running.

## Install by hand

Download the current build from [Arcade Shelf releases](https://github.com/qa-p1/Arcade-Shelf/releases/latest).

### Linux

Download the Arcade Shelf AppImage and run it. On first run, Shelf asks **“Install Arcade Shelf?”** Choose **Install** to add it to the app launcher, place it in `~/Applications/Arcade/`, add the `arcade-shelf` command in `~/.local/bin`, and optionally start Shelf at login. Choose **Just run it once** to use Shelf without installing it.

If `~/.local/bin` is not on your `PATH`, Shelf shows the exact line to add for your shell. To remove the installation, run `arcade-shelf --uninstall`, use the launcher's **Uninstall** action, or remove Shelf in Arcade Tools. Your settings stay unless you choose to remove them.

### Windows

Run the per-user installer; no administrator account is needed. It adds a Start menu entry, offers an optional desktop shortcut and start-at-login setting, and puts `arcade-shelf` on `PATH`. If SmartScreen appears, choose **More info → Run anyway**. Uninstall from **Settings → Apps** or Arcade Tools.

### macOS

Open the disk image and drag Arcade Shelf to Applications. If you run it from the disk image or Downloads, Shelf offers to move itself to Applications. The app is not notarized; the first time, Control-click or right-click it and choose **Open**.

## First run

Start Shelf from the app launcher or run `arcade-shelf`. A compact capsule appears. Click it to expand the active collection. You can start or toggle the resident app with `arcade-shelf --toggle`, open settings with `arcade-shelf --settings`, or add a path with `arcade-shelf --add /path/to/file`.

The default global shortcut is **Ctrl+Alt+S**. You can change it from **Settings → Trigger**. Linux may ask you to approve the shortcut. If your desktop cannot bind it, the status message explains the available alternative.

## Try this first

Drag a file and a piece of text onto the capsule, then expand Shelf. Select an item and use its context menu to open it or reveal its original. Existing files are not copied or changed; Shelf stores references to them.
