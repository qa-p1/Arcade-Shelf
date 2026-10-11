---
title: Fix common Arcade Shelf issues
order: 6
since: 0.1.1
---

# Fix common Arcade Shelf issues

## The global shortcut is not ready

Open **Settings → Trigger** and read the status below the shortcut field. A desktop portal may be unavailable, may have declined the shortcut, or may report a collision. Choose another shortcut and approve it when prompted. On Hyprland, if the portal cannot bind it, set a compositor binding to run `arcade-shelf --toggle` as described by the status message.

## Shelf does not appear or stays behind other windows

Check the tray menu or run `arcade-shelf --toggle`. If you want it above ordinary windows, enable **Always on top** in settings. The floating surface and edge tab were checked on Hyprland; GNOME, KDE, Sway, fullscreen behavior, and multi-monitor/DPI transitions are not yet verified on a real desktop.

## The original file is shown as unavailable

Shelf stores file and folder references rather than copies. Restore or move the original back to its path, then refresh the shelf. A missing reference is marked unavailable; Shelf does not recreate or delete the original.

## An action or preview is missing

Open **Settings → Connected apps** and check the master switch and that peer's toggle. Actions are shown only when a peer supports the selected content and is available. For Look previews, select a supported file; ordinary web URLs open through the browser.

## Dragging into another application does nothing

Native drag-out has not yet been checked on a real desktop for this release. Use the item's context menu or **Ctrl+C** to copy selected content as a workaround. The 0.1.1 real-desktop check verified dropping files and text into Shelf on Hyprland, not dragging Shelf items out.

## Windows or macOS behaves differently

Those builds and tests run in CI, but Shelf has **not been run interactively on a real Windows or macOS desktop**. If the native shortcut, tray or surface behaves differently, use the app's **Settings → Trigger** status and report the platform and desktop details with the issue.
