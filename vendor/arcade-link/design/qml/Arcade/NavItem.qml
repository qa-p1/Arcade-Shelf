pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// A sidebar tab: 34 px, icon + label (+ count). Active: panel fill, line
// ring, accent icon. In a collapsed sidebar only the icon shows.
T.AbstractButton {
    id: control
    property string iconName: ""
    property int count: 0
    property bool collapsed: false
    width: parent ? parent.width : implicitWidth
    implicitHeight: Theme.nav
    implicitWidth: collapsed ? Theme.nav + Theme.space6 : 160
    leftPadding: collapsed ? 0 : Theme.space10
    rightPadding: collapsed ? 0 : Theme.space10
    spacing: Theme.space10
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.PageTab
    Accessible.name: text
    contentItem: Item {
        Icon {
            id: navIcon
            x: control.collapsed ? (parent.width - width) / 2 : 0
            anchors.verticalCenter: parent.verticalCenter
            name: control.iconName
            color: control.checked ? Theme.accentInk : control.hovered ? Theme.text : Theme.muted
        }
        Label {
            visible: !control.collapsed
            x: navIcon.width + control.spacing
            width: parent.width - x - (countPill.visible ? countPill.width + Theme.space8 : 0)
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            medium: true
            color: control.checked || control.hovered ? Theme.text : Theme.muted
            elide: Text.ElideRight
        }
        Rectangle {
            id: countPill
            visible: control.count > 0 && !control.collapsed
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(18, countText.implicitWidth + Theme.space6 * 2)
            height: 18
            radius: 9
            color: Theme.accent
            Label { id: countText; anchors.centerIn: parent; text: control.count; kind: "caption"; font.weight: 600; color: Theme.accentText }
        }
    }
    background: Rectangle {
        radius: Theme.radiusSm
        color: control.checked ? Theme.panel : control.hovered ? Theme.hover : "transparent"
        border.color: control.checked ? Theme.line : "transparent"
        FocusRing { visible: control.visualFocus; controlRadius: Theme.radiusSm }
    }
    ToolTip { visible: control.collapsed && control.hovered; text: control.text; x: control.width + Theme.space6; y: (control.height - implicitHeight) / 2 }
}
