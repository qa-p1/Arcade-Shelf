pragma ComponentBehavior: Bound
import QtQuick

// A loading placeholder line (10 px) or block; shimmers unless motion is reduced.
Rectangle {
    id: root
    implicitWidth: 120
    implicitHeight: 10
    radius: Theme.radiusXs
    color: Theme.sunken
    clip: true
    Rectangle {
        id: sheen
        visible: !Theme.reducedMotion
        width: root.width
        height: root.height
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 0.5; color: Theme.hover }
            GradientStop { position: 1; color: "transparent" }
        }
        XAnimator on x {
            from: -root.width; to: root.width
            duration: 1200
            loops: Animation.Infinite
            running: root.visible && !Theme.reducedMotion
        }
    }
}
