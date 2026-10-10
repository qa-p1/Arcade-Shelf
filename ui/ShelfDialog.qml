import QtQuick
import QtQuick.Controls

Dialog {
    id: control
    property color foreground: "#edeaf6"
    property color mutedColor: "#928b9e"
    property color surface: "#24212e"
    property color outline: "#443d51"
    property color accentColor: "#b7a6fa"
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 20
    anchors.centerIn: parent
    width: Math.min(390, parent.width - 32)
    background: Rectangle { radius: 15; color: control.surface; border.color: control.outline; border.width: 1 }
    header: Text {
        text: control.title
        textFormat: Text.PlainText
        padding: 20; bottomPadding: 8
        color: control.foreground
        font.pixelSize: 17
        font.weight: Font.DemiBold
    }
    Overlay.modal: Rectangle { color: "#8007060c" }
}
