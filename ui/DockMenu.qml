pragma ComponentBehavior: Bound
import QtQuick
import Arcade

// "Dock" submenu, shared by the capsule menu and the panel's window menu.
Menu {
    id: root
    required property ShelfController shelf
    // Collapses the panel first, so a docked tab grows from the right edge.
    signal dockRequested(string edge)
    title: "Dock"

    MenuItem { text: "Left edge"; onTriggered: root.dockRequested("left") }
    MenuItem { text: "Right edge"; onTriggered: root.dockRequested("right") }
    MenuItem { text: "Top edge"; onTriggered: root.dockRequested("top") }
    MenuItem { text: "Bottom edge"; onTriggered: root.dockRequested("bottom") }
    MenuSeparator {}
    MenuItem { text: "Undock"; enabled: root.shelf.dockedTo.length > 0; onTriggered: root.dockRequested("") }
}
