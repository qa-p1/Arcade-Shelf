pragma ComponentBehavior: Bound
import QtQuick

// A toast: tone "info", "success" or "error", the message, an optional action
// (children) and a close button. Info and success hide after 5 s; errors stay
// until closed. Place it bottom-right, raised above an operation bar.
Rectangle {
    id: root
    property string tone: "info"
    property string text: ""
    default property alias actions: actionRow.data
    signal dismissed()
    function show(message, toneName) { text = message; tone = toneName || "info"; visible = true; if (tone !== "error") timer.restart(); else timer.stop() }
    function hide() { timer.stop(); visible = false; dismissed() }
    visible: false
    implicitWidth: 380
    implicitHeight: Math.max(row.implicitHeight, 24) + Theme.space10 * 2
    radius: Theme.radiusMd
    color: Theme.panel
    border.color: Theme.line
    Accessible.role: Accessible.AlertMessage
    Accessible.name: text
    Shadow { radius: Theme.radiusMd }
    Timer { id: timer; interval: Theme.toastTimeout; onTriggered: root.hide() }
    Row {
        id: row
        x: Theme.space14
        width: parent.width - Theme.space14 - Theme.space12
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space10
        Icon {
            name: root.tone === "success" ? "check" : root.tone === "error" ? "alert" : "info"
            color: root.tone === "success" ? Theme.okInk : root.tone === "error" ? Theme.dangerInk : Theme.accentInk
            anchors.verticalCenter: parent.verticalCenter
        }
        Label {
            width: row.width - Theme.iconSize - closeButton.width - actionRow.width - row.spacing * (actionRow.width ? 3 : 2)
            text: root.text
            kind: "control"
            font.weight: 400
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            anchors.verticalCenter: parent.verticalCenter
        }
        Row { id: actionRow; spacing: Theme.space6; anchors.verticalCenter: parent.verticalCenter }
        Button {
            id: closeButton
            variant: "ghost"
            small: true
            iconName: "x"
            tooltip: "Close"
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.hide()
        }
    }
}
