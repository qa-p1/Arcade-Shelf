import QtQuick
import QtQuick.Controls

TextField {
    id: control
    property color foreground: "#edeaf6"
    property color mutedColor: "#928b9e"
    property color surface: "#22202c"
    property color outline: "#383440"
    property color accentColor: "#b7a6fa"
    implicitHeight: 36
    color: foreground
    placeholderTextColor: mutedColor
    selectionColor: accentColor
    selectedTextColor: "#20162f"
    font.pixelSize: 12
    leftPadding: 12
    rightPadding: 12
    background: Rectangle {
        radius: 9
        color: control.surface
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? control.accentColor : control.outline
    }
}
