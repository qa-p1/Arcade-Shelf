pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// Kit input: 32 px (28 when small), line-strong border, accent border and a
// 3 px accent-soft ring on focus; `invalid` turns both to danger.
T.TextField {
    id: control
    property bool small: false
    property bool invalid: false
    implicitWidth: Math.max(160, implicitBackgroundWidth + leftInset + rightInset, contentWidth + leftPadding + rightPadding)
    implicitHeight: small ? Theme.controlSm : Theme.control
    leftPadding: Theme.space10
    rightPadding: Theme.space10
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    color: Theme.text
    placeholderTextColor: Theme.muted
    selectionColor: Theme.accentSoft
    selectedTextColor: Theme.text
    font.family: Theme.fontSans
    font.pixelSize: small ? Theme.smallSize : Theme.controlSize
    font.features: { "cv11": 1 }
    opacity: enabled ? 1 : 0.55
    selectByMouse: true

    Label {
        x: control.leftPadding
        width: control.width - control.leftPadding - control.rightPadding
        anchors.verticalCenter: parent.verticalCenter
        visible: !control.length && !control.preeditText && control.placeholderText !== ""
        text: control.placeholderText
        kind: control.small ? "small" : "control"
        font.weight: 400
        color: control.placeholderTextColor
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitWidth: 160
        radius: Theme.radiusSm
        color: Theme.panel
        border.color: control.invalid ? Theme.danger : control.activeFocus ? Theme.accent : Theme.lineStrong
        Rectangle {
            visible: control.activeFocus
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: "transparent"
            border.width: 3
            border.color: control.invalid ? Theme.dangerSoft : Theme.accentSoft
            z: -1
        }
    }
}
