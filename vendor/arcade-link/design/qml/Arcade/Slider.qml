pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// Kit slider: 4 px line-strong track with the accent filled part, a 16 px
// panel knob with an accent ring. Arrow keys step.
T.Slider {
    id: control
    implicitWidth: 200
    implicitHeight: Theme.hitTarget
    padding: 0
    hoverEnabled: true
    background: Item {
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: control.availableWidth
        height: 4
        Rectangle { anchors.fill: parent; radius: 2; color: Theme.lineStrong }
        Rectangle { width: control.visualPosition * parent.width; height: parent.height; radius: 2; color: Theme.accent }
    }
    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 16
        height: 16
        radius: 8
        color: Theme.panel
        border.width: 2
        border.color: Theme.accent
        FocusRing { visible: control.visualFocus; controlRadius: 8 }
    }
    HoverHandler { cursorShape: Qt.PointingHandCursor }
}
