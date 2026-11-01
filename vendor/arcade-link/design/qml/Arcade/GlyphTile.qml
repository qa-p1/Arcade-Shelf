pragma ComponentBehavior: Bound
import QtQuick

// A glyph tile (28 / 36 / 52 px): sunken, 1 px line, the app mark or an icon.
Rectangle {
    id: root
    // An app id ("arcade.find") shows its mark; otherwise `iconName`.
    property string appId: ""
    property string iconName: ""
    property real size: Theme.tile
    property color ink: Theme.text
    implicitWidth: size
    implicitHeight: size
    radius: size >= Theme.tileLg ? Theme.radiusTileLg : size <= Theme.tileSm ? Theme.radiusTileSm : Theme.radiusTile
    color: Theme.sunken
    border.color: Theme.line
    readonly property real markSize: size >= Theme.tileLg ? 26 : size <= Theme.tileSm ? 15 : 18
    Glyph {
        visible: root.appId !== ""
        anchors.centerIn: parent
        appId: root.appId
        size: root.markSize
        color: root.ink
    }
    Icon {
        visible: root.appId === "" && root.iconName !== ""
        anchors.centerIn: parent
        name: root.iconName
        size: root.markSize
        color: root.ink
    }
}
