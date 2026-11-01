pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

// A 12 px busy ring (the busy button's spinner). Spins only while visible,
// on the render thread.
Item {
    id: root
    property color color: Theme.text
    property real size: 12
    implicitWidth: size
    implicitHeight: size
    Shape {
        id: ring
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: root.color
            strokeWidth: 2
            fillColor: "transparent"
            capStyle: ShapePath.FlatCap
            PathAngleArc {
                centerX: root.size / 2; centerY: root.size / 2
                radiusX: root.size / 2 - 1; radiusY: root.size / 2 - 1
                startAngle: 0; sweepAngle: 270
            }
        }
        RotationAnimator on rotation {
            from: 0; to: 360; duration: 700
            loops: Animation.Infinite
            running: root.visible
        }
    }
}
