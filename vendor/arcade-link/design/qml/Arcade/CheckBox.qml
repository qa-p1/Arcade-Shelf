pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// Kit checkbox (selection in lists and dialogs): 15 px box, label in the
// hit target, 28 px minimum height.
T.CheckBox {
    id: control
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: 0
    spacing: Theme.space8
    implicitWidth: 15 + (text ? spacing + label.implicitWidth : 0)
    implicitHeight: Math.max(Theme.hitTarget, label.implicitHeight)
    Accessible.name: text
    indicator: Rectangle {
        x: 0
        anchors.verticalCenter: parent.verticalCenter
        width: 15
        height: 15
        radius: 4
        color: !control.enabled ? Theme.sunken : control.checked ? Theme.accent : Theme.panel
        border.color: !control.enabled ? Theme.lineStrong : control.checked ? Theme.accent : Theme.muted
        Icon {
            anchors.centerIn: parent
            visible: control.checked
            name: "check"
            size: 12
            color: control.enabled ? Theme.accentText : Theme.muted
        }
        FocusRing { visible: control.visualFocus; controlRadius: 4 }
    }
    contentItem: Label {
        id: label
        leftPadding: 15 + control.spacing
        text: control.text
        color: control.enabled ? Theme.text : Theme.muted
        verticalAlignment: Text.AlignVCenter
    }
    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
}
