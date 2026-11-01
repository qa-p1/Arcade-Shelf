pragma ComponentBehavior: Bound
import QtQuick

// The kit focus ring: 2 px accent, 2 px outside the control it fills.
Rectangle {
    property real controlRadius: Theme.radiusSm
    anchors.fill: parent
    anchors.margins: -(Theme.focusOffset + Theme.focusRing)
    radius: controlRadius + Theme.focusOffset + Theme.focusRing
    color: "transparent"
    border.width: Theme.focusRing
    border.color: Theme.accent
}
