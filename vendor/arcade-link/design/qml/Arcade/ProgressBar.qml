pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// 4 px progress bar: accent on sunken; indeterminate slides a segment
// (only while visible).
T.ProgressBar {
    id: control
    implicitWidth: 120
    implicitHeight: 4
    Accessible.role: Accessible.ProgressBar
    background: Rectangle { radius: 2; color: Theme.sunken }
    contentItem: Item {
        clip: true
        Rectangle {
            visible: !control.indeterminate
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: Theme.accent
            Behavior on width { NumberAnimation { duration: Theme.durationBase } }
        }
        Rectangle {
            id: segment
            visible: control.indeterminate
            width: parent.width * 0.35
            height: parent.height
            radius: 2
            color: Theme.accent
            XAnimator on x {
                from: -segment.width; to: control.width
                duration: 1200
                loops: Animation.Infinite
                running: control.indeterminate && control.visible && !Theme.reducedMotion
            }
        }
    }
}
