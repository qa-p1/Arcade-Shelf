pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// Kit scroll bar: a 4 px line-strong pill in a 10 px lane, shown while
// scrolling or hovered.
T.ScrollBar {
    id: control
    implicitWidth: orientation === Qt.Vertical ? 10 : 0
    implicitHeight: orientation === Qt.Horizontal ? 10 : 0
    padding: 3
    minimumSize: orientation === Qt.Vertical ? 24 / Math.max(1, height) : 24 / Math.max(1, width)
    visible: policy === T.ScrollBar.AlwaysOn || (policy === T.ScrollBar.AsNeeded && size < 1)
    contentItem: Rectangle {
        implicitWidth: 4
        implicitHeight: 4
        radius: 2
        color: control.pressed ? Theme.muted : Theme.lineStrong
        opacity: control.active || control.hovered || control.policy === T.ScrollBar.AlwaysOn ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationBase } }
    }
}
