import QtQuick
import QtQuick.Controls

AbstractButton {
    id: control
    property string glyph: ""
    property color foreground: "#edeaf6"
    property color surface: "transparent"
    property color hoverSurface: "#272531"
    property color accentColor: "#b7a6fa"
    property bool accent: false
    property bool destructive: false
    property bool compact: false
    property bool reduceMotion: false
    property string tooltip: ""
    implicitHeight: compact ? 32 : 36
    implicitWidth: Math.max(text.length ? 64 : implicitHeight, row.implicitWidth + (text.length ? 24 : 12))
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : 0.35
    scale: down ? 0.97 : 1
    Behavior on scale { NumberAnimation { duration: control.reduceMotion ? 0 : 110 } }
    background: Rectangle {
        radius: 9
        color: control.accent ? control.accentColor : control.down || control.hovered ? control.hoverSurface : control.surface
        border.width: control.activeFocus ? 2 : 0
        border.color: control.accentColor
        Behavior on color { ColorAnimation { duration: control.reduceMotion ? 0 : 120 } }
    }
    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 7
            ShelfIcon {
                visible: control.glyph !== ""
                name: control.glyph
                color: control.accent ? "#20162f" : control.destructive ? "#eb8397" : control.foreground
                width: 17; height: 17
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: control.text !== ""
                text: control.text
                color: control.accent ? "#20162f" : control.destructive ? "#eb8397" : control.foreground
                font.pixelSize: 12
                font.weight: Font.Medium
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
    ToolTip.visible: tooltip !== "" && hovered
    ToolTip.delay: 550
    ToolTip.text: tooltip
    Accessible.name: tooltip || text
    HoverHandler { cursorShape: Qt.PointingHandCursor }
}
