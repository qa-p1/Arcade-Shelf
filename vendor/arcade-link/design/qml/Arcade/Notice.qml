pragma ComponentBehavior: Bound
import QtQuick

// An inline notice: tone "neutral", "ok", "warn" or "danger", optional title,
// the message, and an optional action (the notice's children, right-aligned).
Rectangle {
    id: root
    property string tone: "neutral"
    property string title: ""
    property string text: ""
    default property alias actions: actionRow.data
    implicitWidth: 360
    implicitHeight: Math.max(row.implicitHeight, 20) + Theme.space10 * 2
    width: parent ? parent.width : implicitWidth
    radius: Theme.radiusMd
    readonly property color toneColor: tone === "warn" ? Theme.warn : tone === "danger" ? Theme.danger : Theme.line
    color: tone === "warn" ? Theme.warnSoft : tone === "danger" ? Theme.dangerSoft : Theme.panel
    border.color: tone === "warn" || tone === "danger" ? Qt.tint(Theme.line, Qt.rgba(toneColor.r, toneColor.g, toneColor.b, 0.35)) : Theme.line
    Accessible.role: Accessible.AlertMessage
    Accessible.name: (title ? title + ". " : "") + text
    Row {
        id: row
        x: Theme.space14
        width: parent.width - Theme.space14 * 2
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space10
        Icon {
            name: root.tone === "ok" ? "check" : root.tone === "neutral" ? "info" : "alert"
            color: root.tone === "ok" ? Theme.okInk : root.tone === "warn" ? Theme.warnInk : root.tone === "danger" ? Theme.dangerInk : Theme.muted
            anchors.verticalCenter: parent.verticalCenter
        }
        Column {
            width: row.width - Theme.iconSize - row.spacing - (actionRow.width ? actionRow.width + row.spacing : 0)
            anchors.verticalCenter: parent.verticalCenter
            Label { visible: root.title !== ""; width: parent.width; text: root.title; kind: "control"; wrapMode: Text.Wrap }
            Label { width: parent.width; text: root.text; kind: "control"; font.weight: 400; wrapMode: Text.Wrap }
        }
        Row {
            id: actionRow
            spacing: Theme.space8
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
