pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// Inline accent text that acts (STANDARD §5 "Link button").
T.AbstractButton {
    id: control
    property string kind: "body"
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: 0
    implicitWidth: label.implicitWidth
    implicitHeight: label.implicitHeight
    Accessible.role: Accessible.Link
    Accessible.name: text
    contentItem: Label {
        id: label
        text: control.text
        kind: control.kind
        medium: true
        color: control.enabled ? Theme.accentInk : Theme.faint
        font.underline: control.hovered && control.enabled
    }
    background: Item {
        FocusRing { visible: control.visualFocus; controlRadius: 4 }
    }
    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
}
