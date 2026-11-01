pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import Arcade

// Every kit component on one page, for review and screenshots:
//   qml -I design/qml design/qml/gallery/Gallery.qml -- light|dark [out.png]
Window {
    id: window
    width: 1180
    height: 1700
    visible: true
    color: Theme.bg
    title: "Arcade QML kit"
    Rectangle { anchors.fill: parent; color: Theme.bg }
    readonly property var args: Qt.application.arguments
    readonly property int sep: args.indexOf("--")
    Component.onCompleted: {
        Theme.appId = "arcade.shelf";
        Theme.mode = sep >= 0 && args.length > sep + 1 ? args[sep + 1] : "light";
        if (sep >= 0 && args.length > sep + 2) shot.start();
    }
    Timer {
        id: shot
        interval: 900
        onTriggered: {
            menu.popup(menuAnchor, 0, 0);
            Qt.callLater(() => window.contentItem.grabToImage(r => { r.saveToFile(window.args[window.sep + 2]); Qt.quit(); }));
        }
    }

    Sidebar {
        id: sidebar
        height: parent.height
        appName: "Arcade Shelf"
        version: "0.4.0"
        NavItem { text: "General"; iconName: "settings"; checked: true }
        NavItem { text: "Shortcuts"; iconName: "keyboard" }
        NavItem { text: "Storage"; iconName: "archive"; count: 3 }
        NavItem { text: "Connected apps"; iconName: "apps" }
        NavItem { text: "About"; iconName: "info" }
    }

    Flickable {
        x: sidebar.width
        width: parent.width - x
        height: parent.height
        contentHeight: page.height + 72
        ScrollBar.vertical: ScrollBar {}
        Column {
            id: page
            x: 36; y: 32
            width: Math.min(980, parent.width - 72)
            spacing: 32
            PageHead {
                title: "General"
                subtitle: "How Shelf looks and starts."
                Button { text: "Export…"; iconName: "download" }
                Button { text: "Save"; variant: "primary" }
            }
            ListSection {
                title: "Appearance"
                meta: "Applies at once"
                ListRow {
                    title: "Appearance"
                    subtitle: "System, light or dark."
                    SegmentedControl { options: ["System", "Light", "Dark"]; value: Theme.mode === "dark" ? "Dark" : "Light" }
                }
                SwitchRow { title: "Start at login"; subtitle: "Shelf starts in the background."; checked: true }
                SwitchRow { title: "Always on top"; checked: false }
                SliderRow { title: "Owned storage quota"; subtitle: "Collected text, images and temporary outputs."; unit: "MiB"; from: 64; to: 4096; stepSize: 64; value: 512 }
                ListRow {
                    title: "Temporary item retention"
                    NumberField { value: 7; to: 3650; unit: "days" }
                }
                ListRow {
                    title: "Theme accent"
                    subtitle: "A longer description that wraps onto a second line when the window is narrow, which it is not here."
                    ComboBox { model: ["Fuchsia", "Lime", "Teal", "Blue", "Amber"] }
                }
                note: "Removing an item never deletes its original file."
            }
            ListSection {
                title: "Shortcuts"
                ListRow {
                    title: "Show Shelf"
                    stacked: true
                    ShortcutRecorder { sequence: "Ctrl+Alt+S"; defaultSequence: "Ctrl+Alt+H"; conflict: "Used by Arcade Find › Search"; note: "On Hyprland, Shelf adds its own bind." }
                }
            }
            ListSection {
                title: "Connected apps"
                ListRow {
                    title: "Arcade Find"; subtitle: "Find can add search results to Shelf."
                    leading: Component { GlyphTile { appId: "arcade.find" } }
                    Badge { text: "Connected"; tone: "ok"; dot: true }
                }
                ListRow {
                    title: "Arcade Look"; subtitle: "Space previews the selection in Look."
                    leading: Component { GlyphTile { appId: "arcade.look" } }
                    Badge { text: "Not installed" }
                    Button { text: "Install"; small: true }
                }
            }
            Row {
                spacing: 8
                Button { text: "Default" }
                Button { text: "Primary"; variant: "primary" }
                Button { text: "Ghost"; variant: "ghost" }
                Button { text: "Danger"; variant: "danger" }
                Button { text: "Delete collection"; variant: "destructive" }
                Button { text: "Busy"; iconName: "refresh"; busy: true }
                Button { text: "Disabled"; enabled: false }
                Button { iconName: "more"; tooltip: "More" }
                Button { text: "Small"; small: true; iconName: "plus" }
                LinkButton { text: "Reset to default"; anchors.verticalCenter: parent.verticalCenter }
            }
            Row {
                spacing: 12
                TextField { placeholderText: "Collection name" }
                TextField { text: "Invalid"; invalid: true }
                SearchField { placeholderText: "Filter items"; text: "report" }
                CheckBox { text: "Temporary collection"; checked: true; anchors.verticalCenter: parent.verticalCenter }
                CheckBox { text: "Unchecked"; anchors.verticalCenter: parent.verticalCenter }
            }
            Row {
                spacing: 10
                Badge { text: "Neutral" }
                Badge { text: "OK"; tone: "ok" }
                Badge { text: "Warn"; tone: "warn" }
                Badge { text: "Danger"; tone: "danger" }
                Badge { text: "12 items"; tone: "accent" }
                KeyCombo { keys: ["Ctrl", "Shift", "K"]; alternatives: [["F1"]] }
                ProgressBar { value: 0.6; anchors.verticalCenter: parent.verticalCenter }
                ProgressBar { indeterminate: true; anchors.verticalCenter: parent.verticalCenter }
                Skeleton { width: 90; anchors.verticalCenter: parent.verticalCenter }
            }
            Notice { tone: "warn"; title: "Connections are off in this app"; text: "Other Arcade apps can't add to Shelf."; Button { text: "Turn on"; small: true } }
            Notice { tone: "danger"; text: "Couldn't read the backup file."; Button { text: "Retry"; small: true } }
            Row {
                spacing: 16
                EmptyState { width: 420; iconName: "archive"; title: "Drop to collect"; text: "Keep the originals right where they are."; Button { text: "Paste"; small: true } }
                Surface {
                    width: 400; height: 196
                    Column {
                        anchors.fill: parent
                        anchors.margins: 6
                        OverlayRow { title: "quarterly-report.pdf"; subtitle: "~/Documents/Reports"; meta: "2 MB"; selected: true; leading: Component { Icon { name: "file" } } }
                        OverlayRow { title: "report-cover.png"; subtitle: "~/Pictures"; meta: "84 kB"; leading: Component { Icon { name: "image" } } }
                        OverlayRow { title: "Notes"; leading: Component { Icon { name: "folder" } } }
                    }
                    HintBar {
                        anchors.bottom: parent.bottom
                        hints: [{ keys: ["↵"], label: "Open" }, { keys: ["Space"], label: "Preview in Look" }, { keys: ["Esc"], label: "Close" }]
                        trailing: "3 items"
                    }
                }
            }
            Item { id: menuAnchor; width: 1; height: 1 }
            Toast { visible: true; width: 380; tone: "success"; text: "Copied 3 items"; Button { text: "Undo"; variant: "ghost"; small: true } }
        }
    }
    Menu {
        id: menu
        MenuItem { text: "Open"; iconName: "external"; shortcut: "Enter" }
        MenuItem { text: "Show in folder"; iconName: "showInFolder" }
        MenuItem { text: "Copy"; iconName: "copy"; shortcut: "Ctrl+C" }
        MenuItem { text: "Always on top"; checkable: true; checked: true }
        MenuSeparator {}
        MenuItem { text: "Remove from Shelf"; iconName: "trash"; danger: true; shortcut: "Delete" }
    }
}
