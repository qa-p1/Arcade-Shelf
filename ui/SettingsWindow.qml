pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import Arcade

// Settings (Template A): a normal window with native decorations, opened from
// the menus and the tray and independent of the panel. Main.qml creates it
// only while it is open.
Window {
    id: root
    required property ShelfController shelf
    objectName: "settingsWindow"
    title: "Settings — Arcade Shelf"
    width: Theme.settingsWidth
    height: Theme.settingsHeight
    minimumWidth: Theme.windowMinWidth
    minimumHeight: Theme.windowMinHeight
    visible: true
    color: Theme.bg
    onClosing: root.shelf.settingsOpen = false

    readonly property var pages: [
        { title: "General", icon: "settings", subtitle: "How Shelf looks, floats and starts." },
        { title: "Shortcuts", icon: "keyboard", subtitle: "The shortcut that opens Shelf, and the keys inside it." },
        { title: "Storage", icon: "archive", subtitle: "What Shelf keeps, and for how long." },
        { title: "Backup and restore", icon: "download", subtitle: "Move your collections to another computer." },
        { title: "Connected apps", icon: "apps", subtitle: "Which Arcade apps work with Shelf." },
        { title: "About", icon: "info", subtitle: "Version, updates, help and licences." }
    ]
    readonly property var panelKeys: [
        { label: "Open the selected item", keys: ["Enter"] },
        { label: "Preview in Look", keys: ["Space"] },
        { label: "Remove from Shelf", keys: ["Del"] },
        { label: "Select all", keys: ["Ctrl", "A"] },
        { label: "Copy", keys: ["Ctrl", "C"] },
        { label: "Collect from clipboard", keys: ["Ctrl", "V"] },
        { label: "Undo", keys: ["Ctrl", "Z"] },
        { label: "Filter items", keys: ["Ctrl", "F"] },
        { label: "Settings", keys: ["Ctrl", ","] },
        { label: "Close the panel", keys: ["Esc"] }
    ]

    Shortcut { sequences: [StandardKey.Close]; onActivated: root.close() }
    Shortcut { sequence: "Esc"; enabled: !recorder.recording; onActivated: root.close() }

    Sidebar {
        id: sidebar
        height: parent.height
        appName: "Arcade Shelf"
        version: root.shelf.preferences.version || ""
        collapsed: root.width < Theme.collapseBelow
        NavItem { objectName: "nav-0"; text: root.pages[0].title; iconName: root.pages[0].icon }
        NavItem { objectName: "nav-1"; text: root.pages[1].title; iconName: root.pages[1].icon }
        NavItem { objectName: "nav-2"; text: root.pages[2].title; iconName: root.pages[2].icon }
        NavItem { objectName: "nav-3"; text: root.pages[3].title; iconName: root.pages[3].icon }
        NavItem { objectName: "nav-4"; text: root.pages[4].title; iconName: root.pages[4].icon }
        NavItem { objectName: "nav-5"; text: root.pages[5].title; iconName: root.pages[5].icon }
    }

    Flickable {
        id: scroll
        objectName: "preferencesScroll"
        x: sidebar.width
        width: parent.width - x
        height: parent.height
        contentHeight: page.y + page.height + Theme.space48
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {}
        Connections { target: sidebar; function onCurrentIndexChanged() { scroll.contentY = 0; } }

        Column {
            id: page
            x: Math.max(Theme.space32, (scroll.width - width) / 2)
            y: Theme.space32
            width: Math.min(Theme.contentMax, scroll.width - Theme.space32 * 2)
            spacing: Theme.space32

            PageHead {
                width: parent.width
                title: root.pages[sidebar.currentIndex].title
                subtitle: root.pages[sidebar.currentIndex].subtitle
            }

            // General
            ListSection {
                visible: sidebar.currentIndex === 0
                title: "Appearance"
                ListRow {
                    title: "Theme"
                    subtitle: "System follows your desktop."
                    SegmentedControl {
                        objectName: "themePicker"
                        options: [{ value: "system", label: "System" }, { value: "light", label: "Light" }, { value: "dark", label: "Dark" }]
                        value: root.shelf.theme
                        onActivated: value => root.shelf.theme = value
                    }
                }
                SwitchRow {
                    objectName: "reducedMotionToggle"
                    title: "Reduce motion"
                    subtitle: "Turn off animations in the capsule and panel."
                    checked: root.shelf.reducedMotion
                    onToggled: checked => root.shelf.reducedMotion = checked
                }
                SwitchRow {
                    objectName: "backdropToggle"
                    title: "Backdrop blur"
                    subtitle: root.shelf.backdropEnabled && !root.shelf.backdropActive
                        ? "On, but your desktop doesn't offer a blur here, so Shelf stays opaque."
                        : "Blur the desktop behind the capsule and panel."
                    checked: root.shelf.backdropEnabled
                    onToggled: checked => root.shelf.backdropEnabled = checked
                }
            }
            ListSection {
                visible: sidebar.currentIndex === 0
                title: "Window"
                SwitchRow {
                    objectName: "alwaysTopToggle"
                    title: "Always on top"
                    subtitle: "Keep the capsule and panel above other windows."
                    checked: root.shelf.alwaysOnTop
                    onToggled: checked => root.shelf.alwaysOnTop = checked
                }
                SwitchRow {
                    objectName: "hoverPeekToggle"
                    title: "Open docked Shelf on hover"
                    subtitle: "Hovering the tab shows the panel. Click it to type (where focus follows the mouse, it may take keyboard focus at once)."
                    checked: root.shelf.hoverPeek
                    onToggled: checked => root.shelf.hoverPeek = checked
                }
                SwitchRow {
                    objectName: "startAtLoginToggle"
                    title: "Start at login"
                    subtitle: "Shelf starts in the background."
                    checked: !!root.shelf.preferences.startAtLogin
                    onToggled: checked => root.shelf.setStartAtLogin(checked)
                }
            }

            // Shortcuts
            ListSection {
                visible: sidebar.currentIndex === 1
                title: "Global shortcut"
                ListRow {
                    title: "Open Shelf"
                    stacked: true
                    ShortcutRecorder {
                        id: recorder
                        objectName: "shortcutRecorder"
                        sequence: root.shelf.shortcut
                        defaultSequence: "Ctrl+Alt+S"
                        allowEmpty: true
                        conflict: root.shelf.shortcutWarning
                        note: root.shelf.platformStatus
                        onCaptured: sequence => root.shelf.shortcut = sequence
                        onCleared: root.shelf.shortcut = ""
                        onResetRequested: root.shelf.shortcut = defaultSequence
                        onChooseAnother: recording = true
                    }
                }
            }
            ListSection {
                visible: sidebar.currentIndex === 1
                title: "In the panel"
                note: "These keys work while the panel has focus."
                Repeater {
                    model: root.panelKeys
                    delegate: ListRow {
                        id: keyRow
                        required property var modelData
                        title: modelData.label
                        minimumHeight: Theme.rowDialog
                        KeyCombo { keys: keyRow.modelData.keys; small: true }
                    }
                }
            }

            // Storage
            ListSection {
                visible: sidebar.currentIndex === 2
                title: "Storage"
                meta: "Using " + Format.formatSize(root.shelf.ownedContentBytes)
                note: "Shelf holds references to your files. Only collected text, images and temporary outputs take space, and removing an item never deletes its original."
                ListRow {
                    title: "Owned storage quota"
                    subtitle: "The most collected content Shelf keeps."
                    NumberField {
                        objectName: "quotaField"
                        from: 1; to: 4000; stepSize: 50; unit: "MB"
                        // MB here is MiB, as in every size Shelf shows (SH-4: 512 MiB read "536 MB").
                        value: Math.max(1, Math.round(Number(root.shelf.preferences.quotaBytes || 536870912) / 1048576))
                        onValueModified: value => root.shelf.setQuota(value)
                    }
                }
                ListRow {
                    title: "Temporary item retention"
                    subtitle: "Unpinned items in temporary collections leave after this. 0 keeps them."
                    NumberField {
                        objectName: "retentionField"
                        from: 0; to: 3650; unit: "days"
                        value: Number(root.shelf.preferences.retentionDays === undefined ? 30 : root.shelf.preferences.retentionDays)
                        onValueModified: value => root.shelf.setRetention(value)
                    }
                }
            }

            // Backup and restore
            ListSection {
                visible: sidebar.currentIndex === 3
                title: "Backup and restore"
                note: "Export asks before copying owned content (" + Format.formatSize(root.shelf.ownedContentBytes) + "). Referenced original files are never copied."
                ListRow {
                    title: "Export a backup"
                    subtitle: "Settings and collections in one .arcade-shelf file."
                    Button { text: "Export…"; iconName: "download"; onClicked: root.shelf.exportPortableSettings() }
                }
                ListRow {
                    title: "Import a backup"
                    subtitle: "Shelf shows what it will change before it does."
                    Button { text: "Import…"; onClicked: root.shelf.importPortableSettings() }
                }
            }

            // Connected apps
            ListSection {
                visible: sidebar.currentIndex === 4
                title: "Arcade Link"
                SwitchRow {
                    objectName: "linkToggle"
                    title: "Work with other Arcade apps"
                    subtitle: "Other apps can add to Shelf and pick items from it."
                    checked: root.shelf.linkEnabled
                    onToggled: checked => root.shelf.linkEnabled = checked
                }
            }
            ListSection {
                visible: sidebar.currentIndex === 4
                title: "Apps"
                note: root.shelf.peers.length ? "" : "Installed Arcade apps appear here."
                Repeater {
                    model: root.shelf.settingsOpen && sidebar.currentIndex === 4 ? root.shelf.peers : []
                    delegate: ListRow {
                        id: peerRow
                        required property var modelData
                        readonly property bool installed: modelData.installed !== false
                        title: modelData.name || modelData.id
                        subtitle: !installed ? "Not installed"
                            : modelData.linkEnabled === false ? "Arcade Link is off in that app."
                            : "Installed"
                        leading: Component { GlyphTile { appId: peerRow.modelData.id; size: Theme.tile } }
                        Switch {
                            visible: peerRow.installed
                            checked: peerRow.modelData.enabled !== false
                            enabled: root.shelf.linkEnabled
                            onToggled: root.shelf.setPeerEnabled(peerRow.modelData.id, checked)
                        }
                        LinkButton {
                            visible: !peerRow.installed
                            text: "Install"
                            onClicked: root.shelf.getPeer(peerRow.modelData.id)
                        }
                    }
                }
            }

            // About
            ListSection {
                visible: sidebar.currentIndex === 5
                title: "Arcade Shelf"
                ListRow {
                    title: "Version " + (root.shelf.preferences.version || "")
                    subtitle: "A floating workspace for desktop content."
                    leading: Component { GlyphTile { appId: "arcade.shelf"; size: Theme.tile } }
                    Badge { text: root.shelf.channel.length ? root.shelf.channel.charAt(0).toUpperCase() + root.shelf.channel.slice(1) : "Not installed" }
                }
                ListRow {
                    title: "Updates"
                    subtitle: "Updates are managed by Arcade Tools."
                }
                ListRow {
                    title: "User guide"
                    subtitle: "How to collect, organise and use items."
                    Button { text: "Open user guide"; onClicked: root.shelf.openUserGuide() }
                }
                ListRow {
                    title: "Logs"
                    subtitle: root.shelf.logsPath
                    Button { text: "Open logs folder"; onClicked: root.shelf.openLogsFolder() }
                }
                ListRow {
                    title: "Diagnostics"
                    subtitle: "App, version, desktop and Link details for a bug report. No file names."
                    Button { text: "Copy diagnostics"; onClicked: root.shelf.copyDiagnostics() }
                }
                ListRow {
                    title: "Licences"
                    subtitle: "MIT. Qt under LGPLv3; Inter under the SIL Open Font License."
                    Button { text: "Open"; onClicked: root.shelf.openLicences() }
                }
                ListRow {
                    title: "Restart Arcade Shelf"
                    subtitle: "Reloads collections and the shortcut."
                    Button { text: "Restart"; onClicked: root.shelf.restart() }
                }
                ListRow {
                    title: "Quit Arcade Shelf"
                    subtitle: "Stops Shelf until you open it again."
                    Button { text: "Quit"; variant: "danger"; onClicked: Qt.quit() }
                }
            }
        }
    }

    // The latest message (a copy, a failed action) along the bottom.
    Toast {
        id: toast
        objectName: "settingsToast"
        anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: Theme.space16
    }
    property string shownMessage: shelf.message
    Connections {
        target: root.shelf
        function onChanged() {
            const message = root.shelf.message;
            if (message === root.shownMessage) return;
            root.shownMessage = message;
            if (message.length) toast.show(message, message.indexOf("Couldn't") === 0 ? "error" : "info");
        }
    }
}
