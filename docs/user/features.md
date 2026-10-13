---
title: Collect and use items in Shelf
order: 3
since: 0.2.0
---

# Collect and use items in Shelf

## Collect files, links and text

Drop files, folders, browser links or text onto the capsule or expanded panel. Use **Ctrl+V** to explicitly collect supported content from the clipboard. Shelf does not keep a clipboard history. The capsule gives feedback when content is added; **Ctrl+Z** undoes the most recent addition. Duplicate additions are skipped and reported.

Existing files and folders are stored as references; collecting them does not move or copy the original. A missing original is marked unavailable. Image content supplied as data and temporary outputs returned by Arcade actions are copied into Shelf's private storage so the collected item remains available.

## Organize items into collections

The default **Quick Shelf** is temporary. Create additional named collections, choose whether a new collection is temporary, switch between collections, rename, pin, reorder or delete them. Pinned collections are retained. Items in unpinned temporary collections expire after 30 days by default; change retention in **Settings → Storage**, or set it to 0 to keep temporary items.

Drag items inside the grid to reorder them. Select one or more items with clicks, **Shift+arrow** for a range, or **Ctrl+arrow** to toggle items. **Ctrl+A** selects all visible items. Filter the current collection with the search box or **Ctrl+F**; matching checks item names, paths and stored text.

## Open, copy and hand off items

Use an item's context menu to open it, reveal a referenced file, copy or remove it. Double-click opens an item. Drag selected items from Shelf to another application. **Ctrl+C** copies selected content in a form another application can receive. **Delete** or **Backspace** removes selected entries from the collection; it never deletes the original file.

When an installed Arcade app offers a compatible action, Shelf can run it on the selection. Actions with effects such as sending content, writing files, changing the clipboard or persisting data ask you to confirm first. Progress and returned outputs appear in Shelf; image and temporary-file outputs become owned Shelf content.

Press **Space** on a compatible selection to preview it through Look. **Enter** opens the focused item, or confirms the current selection when an app explicitly opened Shelf's picker. **Escape** cancels that picker, clears the filter, or closes the expanded panel, in that order.

## Move and display the Shelf surface

Drag the capsule to move it. Drag it to a screen edge to dock it as a narrow tab; hover over the tab to peek at the panel without taking keyboard focus. Click the peek to work in the panel. Press **Escape** to collapse back to the tab. Resize the expanded panel from its edges or corner.

Settings include system, light and dark themes, reduced motion, optional always-on-top behavior and start at login. Shelf's tested desktop behavior is on Hyprland. Other Linux desktops, fullscreen behavior and multi-monitor/DPI handling still need real-desktop verification.

## Connect other Arcade apps

Shelf exposes an explicit picker and actions for adding content or opening a collection. Other apps can add selected content, and Shelf can show compatible actions for its current selection. Use **Settings → Connected apps** to turn all connections off or toggle individual peers. See [what each Arcade app adds](with-other-apps.md).

## Keep track of storage

Settings show the owned-content quota and temporary-item retention. The default owned-content quota is 512 MiB. Referenced files do not use that content quota; owned images and temporary results do. Shelf stores its collection metadata and preferences locally in SQLite.

## Back up and restore Shelf

In **Settings → About → Backup and restore**, choose **Export backup** to save
collections and portable preferences. Shelf shows how much owned content it
stores and asks whether to include it. A metadata-only backup leaves owned
items out; referenced original files are never copied. Tools backups use this
metadata-only export.

Choose **Import backup** to replace current collections and portable preferences.
Shelf validates metadata, quota, file sizes and hashes before changing the live
database, and stores a private database copy under the profile's `backups/`
folder first. Referenced originals stay untouched. Window geometry and
machine-specific login entries are not part of the portable backup.
