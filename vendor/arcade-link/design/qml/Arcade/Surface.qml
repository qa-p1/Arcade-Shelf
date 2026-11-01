pragma ComponentBehavior: Bound
import QtQuick

// Template B overlay surface: panel, 1 px line, radius 12, shadow-pop. Put
// it inside a window with a margin for the shadow, or set `shadow: false`
// where the compositor draws one.
Rectangle {
    property bool shadow: true
    radius: Theme.radiusLg
    color: Theme.panel
    border.color: Theme.line
    Shadow { visible: parent.shadow; radius: parent.radius }
}
