pragma ComponentBehavior: Bound
import QtQuick

// Overlay footer hint bar (28 px, caption, faint): key chips for the main
// actions. `hints`: [{ keys: ["↵"], label: "Open" }, …]; `trailing` text on
// the right (a count).
Rectangle {
    id: root
    property var hints: []
    property string trailing: ""
    implicitHeight: Theme.hintBar
    width: parent ? parent.width : implicitWidth
    color: "transparent"
    Rectangle { width: parent.width; height: 1; color: Theme.line }
    Row {
        id: hintRow
        x: Theme.space12
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space14
        Repeater {
            model: root.hints
            Row {
                id: hint
                required property var modelData
                spacing: Theme.space6
                KeyCombo { keys: hint.modelData.keys; small: true; anchors.verticalCenter: parent.verticalCenter }
                Label { text: hint.modelData.label; kind: "caption"; color: Theme.muted; anchors.verticalCenter: parent.verticalCenter }
            }
        }
    }
    Label {
        visible: root.trailing !== ""
        anchors.right: parent.right
        anchors.rightMargin: Theme.space12
        anchors.verticalCenter: parent.verticalCenter
        // Never drawn over the hints: a long message is cut with an ellipsis.
        width: Math.min(implicitWidth, parent.width - Theme.space12 * 2 - (root.hints.length ? hintRow.width + Theme.space14 : 0))
        elide: Text.ElideRight
        text: root.trailing
        kind: "caption"
        color: Theme.muted
    }
}
