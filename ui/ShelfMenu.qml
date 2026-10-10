pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Menu {
    id: control
    property color foreground: "#edeaf6"
    property color mutedColor: "#928b9e"
    property color surface: "#24212e"
    property color hoverSurface: "#343040"
    property color outline: "#443d51"
    property color accentColor: "#b7a6fa"
    topPadding: 6; bottomPadding: 6; leftPadding: 6; rightPadding: 6
    implicitWidth: 225
    background: Rectangle { radius: 12; color: control.surface; border.color: control.outline; border.width: 1 }
    delegate: MenuItem {
        id: item
        implicitHeight: 34
        leftPadding: 11; rightPadding: 11
        contentItem: Text {
            text: item.text
            textFormat: Text.PlainText
            color: item.enabled ? control.foreground : control.mutedColor
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle { radius: 7; color: item.highlighted ? control.hoverSurface : "transparent" }
        indicator: Text { visible: item.checkable; text: item.checked ? "✓" : ""; textFormat: Text.PlainText; color: control.accentColor; x: parent.width - 22; anchors.verticalCenter: parent.verticalCenter }
    }
}
