pragma ComponentBehavior: Bound
import QtQuick

// One key: 22 px, sunken, line-strong border with a 2 px bottom edge.
Rectangle {
    id: root
    property string text: ""
    property bool small: false
    implicitWidth: Math.max(implicitHeight, label.implicitWidth + (small ? Theme.space4 : Theme.space6) * 2 + 2)
    implicitHeight: small ? 18 : Theme.keyChip
    radius: Theme.radiusXs
    color: Theme.lineStrong
    Accessible.role: Accessible.StaticText
    Accessible.name: text
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        anchors.bottomMargin: 2
        radius: Theme.radiusXs - 1
        color: Theme.sunken
    }
    Label {
        id: label
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -0.5
        text: root.text
        kind: "caption"
    }
}
