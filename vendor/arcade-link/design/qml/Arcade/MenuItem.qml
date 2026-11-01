pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// Kit menu item: 32 px, icon + label, optional shortcut chip and check,
// `danger` for destructive items (last, after a separator).
T.MenuItem {
    id: control
    property string iconName: ""
    // "Ctrl+Shift+K" style; shown as key chips on the right.
    property string shortcut: ""
    property bool danger: false
    readonly property color ink: !enabled ? Theme.faint : danger ? Theme.dangerInk : Theme.text
    implicitWidth: leftPadding + rightPadding + body.implicitWidth
    implicitHeight: visible ? Theme.control : 0
    height: visible ? implicitHeight : 0
    leftPadding: Theme.space10
    rightPadding: Theme.space10
    spacing: Theme.space8
    hoverEnabled: true
    Accessible.name: text

    contentItem: Item {
        id: body
        readonly property real leadWidth: leadingIcon.visible ? leadingIcon.width + control.spacing : 0
        implicitWidth: leadWidth + label.implicitWidth + (trailing.implicitWidth ? Theme.space16 + trailing.implicitWidth : 0)
        Icon {
            id: leadingIcon
            visible: control.iconName !== ""
            name: control.iconName
            color: control.danger && control.enabled ? Theme.dangerInk : control.enabled ? Theme.muted : Theme.faint
            anchors.verticalCenter: parent.verticalCenter
        }
        Label {
            id: label
            x: body.leadWidth
            width: parent.width - x - (trailing.implicitWidth ? Theme.space16 + trailing.implicitWidth : 0)
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            kind: "control"
            font.weight: 400
            color: control.ink
            elide: Text.ElideRight
        }
        Row {
            id: trailing
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space8
            KeyCombo {
                visible: control.shortcut !== ""
                keys: control.shortcut ? control.shortcut.split("+") : []
                small: true
                anchors.verticalCenter: parent.verticalCenter
            }
            Icon {
                visible: control.checkable && control.checked
                name: "check"
                color: Theme.accentInk
                anchors.verticalCenter: parent.verticalCenter
            }
            Icon {
                visible: !!control.subMenu
                name: "chevronRight"
                color: Theme.muted
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
    indicator: null
    arrow: null
    background: Rectangle {
        radius: Theme.radiusSm
        color: control.highlighted && control.enabled ? Theme.hover : "transparent"
    }
}
