pragma ComponentBehavior: Bound
import QtQuick

// A small status label: tone "neutral", "ok", "warn", "danger" or "accent".
Rectangle {
    id: root
    property string text: ""
    property string tone: "neutral"
    property bool dot: false
    implicitWidth: row.implicitWidth + Theme.space6 * 2
    implicitHeight: 20
    radius: height / 2
    color: tone === "ok" ? Theme.okSoft : tone === "warn" ? Theme.warnSoft : tone === "danger" ? Theme.dangerSoft : tone === "accent" ? Theme.accentSoft : Theme.sunken
    border.color: tone === "neutral" ? Theme.line : "transparent"
    Accessible.role: Accessible.StaticText
    Accessible.name: text
    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.space4
        Dot { visible: root.dot; tone: root.tone; anchors.verticalCenter: parent.verticalCenter }
        Label {
            text: root.text
            kind: "caption"
            color: root.tone === "ok" ? Theme.okInk : root.tone === "warn" ? Theme.warnInk : root.tone === "danger" ? Theme.dangerInk : root.tone === "accent" ? Theme.accentInk : Theme.muted
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
