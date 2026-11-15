// The QML kit components (design/qml/Arcade): measurements from STANDARD §4-5,
// states and behaviour.
import QtQuick
import QtQuick.Templates as T
import QtTest
import Arcade

Item {
    id: root
    width: 900
    height: 700

    Column {
        width: 600
        spacing: 8
        Button { id: plain; text: "Install" }
        Button { id: primary; text: "Save"; variant: "primary" }
        Button { id: small; text: "Small"; small: true }
        Button { id: iconOnly; iconName: "more"; tooltip: "More" }
        Button { id: busy; text: "Busy"; iconName: "refresh"; busy: true }
        TextField { id: field; placeholderText: "Name" }
        SearchField { id: search; width: 300 }
        Switch { id: toggle }
        CheckBox { id: check; text: "Temporary collection" }
        SegmentedControl { id: segmented; options: ["System", "Light", "Dark"]; value: "System" }
        NumberField { id: number; from: 1; to: 4096; value: 512; unit: "MiB" }
        ListSection {
            id: section
            title: "General"
            ListRow { id: row1; title: "Appearance"; subtitle: "System, light or dark." }
            SwitchRow { id: switchRow; title: "Start at login" }
            ListRow { id: row3; title: "A very long row title that cannot possibly fit in the space it is given here at all"; Button { text: "Change…" } }
        }
        KeyCombo { id: combo; keys: ["Ctrl", "K"]; alternatives: [["F1"]] }
        Badge { id: badge; text: "Connected"; tone: "ok" }
        GlyphTile { id: tile; appId: "arcade.shelf" }
        ShortcutRecorder { id: recorder; sequence: "Ctrl+Alt+S"; allowEmpty: true }
    }
    Sidebar {
        id: sidebar
        x: 650
        height: root.height
        appName: "Arcade Shelf"
        NavItem { id: nav0; text: "General"; iconName: "settings" }
        NavItem { id: nav1; text: "Shortcuts"; iconName: "keyboard" }
        NavItem { id: nav2; text: "About"; iconName: "info" }
    }
    Menu {
        id: menu
        MenuItem { id: openItem; text: "Open"; shortcut: "Enter" }
        MenuSeparator {}
        MenuItem { id: removeItem; text: "Remove from Shelf"; danger: true; shortcut: "Delete" }
    }
    Dialog {
        id: dialog
        title: "Delete collection “Work”?"
        body: "Staged items will be removed. Original files stay in place."
        destructive: true
        actions: [
            Button { id: keep; text: "Keep" },
            Button { id: del; text: "Delete collection"; variant: "destructive" }
        ]
    }

    TestCase {
        name: "Components"
        when: windowShown

        function initTestCase() { Theme.appId = "arcade.shelf"; Theme.mode = "light" }

        function test_button_sizes_and_variants() {
            compare(plain.height, Theme.control)
            compare(small.height, Theme.controlSm)
            compare(iconOnly.width, 30)
            compare(iconOnly.height, Theme.control)
            compare(String(primary.ink), String(Theme.accentText))
            compare(String(plain.ink), String(Theme.text))
            verify(plain.width >= plain.implicitContentWidth + 24)
            // Busy keeps its width when it stops.
            const w = busy.width
            busy.busy = false
            compare(busy.width, w)
            busy.busy = true
        }

        function test_inputs() {
            compare(field.height, Theme.control)
            compare(search.height, Theme.search)
            search.forceActiveFocus()
            keyClick(Qt.Key_A)
            compare(search.text, "a")
            let escaped = false
            const on = () => escaped = true
            search.escaped.connect(on)
            keyClick(Qt.Key_Escape)
            compare(search.text, "")
            verify(!escaped)
            keyClick(Qt.Key_Escape)
            verify(escaped)
            search.escaped.disconnect(on)
        }

        function test_switch_and_rows() {
            compare(toggle.width, 30)
            compare(toggle.height, 18)
            verify(!switchRow.checked)
            mouseClick(switchRow, 40, switchRow.height / 2)
            verify(switchRow.checked)
            // 52 px minimum; a subtitle can make a row taller, never shorter.
            verify(row1.height >= Theme.row)
            compare(switchRow.height, Theme.row)
            verify(row1.first)
            verify(!switchRow.first)
            verify(row3.last)
            verify(section.height > Theme.row * 3)
            verify(check.height >= Theme.hitTarget)
        }

        function test_segmented() {
            compare(segmented.currentIndex, 0)
            segmented.forceActiveFocus()
            keyClick(Qt.Key_Right)
            compare(segmented.value, "Light")
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Right)
            compare(segmented.value, "Dark")
        }

        function test_number_field_clamps() {
            number.field.forceActiveFocus()
            keyClick(Qt.Key_Up)
            compare(number.value, 513)
            number.commit(99999)
            compare(number.value, 4096)
            number.commit(-5)
            compare(number.value, 1)
        }

        function test_keys_and_badges() {
            compare(combo.children.length >= 2, true)
            compare(badge.height, 20)
            compare(tile.width, Theme.tile)
            compare(tile.radius, Theme.radiusTile)
        }

        function test_sidebar_tabs() {
            compare(sidebar.width, Theme.sidebar)
            verify(nav0.checked)
            compare(nav0.height, Theme.nav)
            verify(nav1.y >= nav0.y + nav0.height)
            mouseClick(nav2)
            compare(sidebar.currentIndex, 2)
            verify(nav2.checked && !nav0.checked)
            nav2.forceActiveFocus()
            keyClick(Qt.Key_Up)
            compare(sidebar.currentIndex, 1)
            verify(nav1.activeFocus)
            sidebar.collapsed = true
            compare(sidebar.width, Theme.sidebarCollapsed)
            verify(nav1.collapsed)
            sidebar.collapsed = false
        }

        function test_menu_fits_its_items() {
            menu.popup(root, 10, 10)
            tryCompare(menu, "opened", true)
            verify(menu.width >= 180)
            verify(removeItem.implicitWidth <= menu.availableWidth + 0.5, removeItem.implicitWidth + " > " + menu.availableWidth)
            compare(openItem.height, Theme.control)
            compare(String(removeItem.ink), String(Theme.dangerInk))
            menu.close()
        }

        function test_dialog_actions_primary_last() {
            dialog.open()
            tryCompare(dialog, "opened", true)
            verify(dialog.width <= 460)
            const k = keep.mapToItem(root, 0, 0), d = del.mapToItem(root, 0, 0)
            verify(d.x > k.x, "the destructive button sits right of Keep")
            dialog.close()
        }

        // WH-5: Escape cancels a destructive dialog; only a running job holds it.
        function test_escape_cancels_a_destructive_dialog_unless_busy() {
            let rejected = 0
            const onRejected = () => rejected++
            dialog.rejected.connect(onRejected)
            dialog.open()
            tryCompare(dialog, "opened", true)
            keyClick(Qt.Key_Escape)
            tryCompare(dialog, "visible", false)
            compare(rejected, 1)

            dialog.busy = true
            dialog.open()
            tryCompare(dialog, "opened", true)
            keyClick(Qt.Key_Escape)
            wait(50)
            verify(dialog.visible, "a busy dialog stays open on Escape")
            dialog.busy = false
            dialog.close()
            dialog.rejected.disconnect(onRejected)
        }

        function test_shortcut_recorder() {
            compare(recorder.displayKeys("Ctrl+Alt+S").length, 3)
            let captured = ""
            const on = s => captured = s
            recorder.captured.connect(on)
            recorder.recording = true
            const field = recorder.children[0].children[0]
            field.forceActiveFocus()
            recorder.recording = true
            keyClick(Qt.Key_K)              // no modifier: ignored
            verify(recorder.recording)
            keyClick(Qt.Key_F5)             // F-keys alone are allowed
            compare(captured, "F5")
            recorder.recording = true
            keyClick(Qt.Key_J, Qt.ControlModifier | Qt.ShiftModifier)
            compare(captured, Qt.platform.os === "osx" ? "Shift+Super+J" : "Ctrl+Shift+J")
            recorder.recording = true
            keyClick(Qt.Key_Escape)
            verify(!recorder.recording)
            recorder.captured.disconnect(on)
        }

        function test_dark_theme_follows() {
            Theme.mode = "dark"
            // The background animates to the new colour.
            tryCompare(plain.background, "color", Theme.panel)
            compare(String(Theme.panel), "#16181d")
            Theme.mode = "light"
        }
    }
}
