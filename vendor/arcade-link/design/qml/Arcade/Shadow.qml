pragma ComponentBehavior: Bound
import QtQuick

// The kit's shadow-pop (or shadow-rest) under a rounded surface: a few
// stacked translucent rounded rectangles. Static geometry, so it costs
// nothing per frame and works on the software renderer too.
Item {
    id: root
    property real radius: Theme.radiusLg
    property bool pop: true
    readonly property real blur: pop ? Theme.shadowPopBlur : Theme.shadowRestBlur
    readonly property real offsetY: pop ? Theme.shadowPopY : Theme.shadowRestY
    readonly property color tint: pop ? Theme.shadowPop : Theme.shadowRest
    readonly property int layers: blur > 0 ? 6 : 0
    anchors.fill: parent
    z: -1
    Repeater {
        model: root.layers
        Rectangle {
            required property int index
            readonly property real spread: root.blur * 0.4 * (index + 1) / root.layers
            x: -spread
            y: root.offsetY * (index + 1) / root.layers - spread
            width: root.width + spread * 2
            height: root.height + spread * 2
            radius: root.radius + spread
            color: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, root.tint.a / root.layers)
        }
    }
}
