pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// Kit switch (a boolean setting): 30×18 track, 14 px knob.
T.Switch {
    id: control
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: 0
    implicitWidth: 30
    implicitHeight: 18
    opacity: enabled ? 1 : 0.45
    Accessible.name: text
    indicator: Rectangle {
        width: 30
        height: 18
        radius: height / 2
        color: control.checked ? Theme.accent : Theme.lineStrong
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Rectangle {
            x: control.checked ? 14 : 2
            y: 2
            width: 14
            height: 14
            radius: 7
            color: Theme.knob
            Behavior on x { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }
        }
        FocusRing { visible: control.visualFocus; controlRadius: 9 }
    }
    contentItem: null
    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
}
