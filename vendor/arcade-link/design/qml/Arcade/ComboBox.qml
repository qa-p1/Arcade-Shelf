pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// Kit select (more than four options): looks like an input with a chevron;
// the list is a kit menu.
T.ComboBox {
    id: control
    property bool small: false
    implicitWidth: Math.max(160, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: small ? Theme.controlSm : Theme.control
    leftPadding: Theme.space10
    rightPadding: Theme.space6 + Theme.iconSize + Theme.space6
    hoverEnabled: true
    opacity: enabled ? 1 : 0.55
    font.family: Theme.fontSans
    font.pixelSize: small ? Theme.smallSize : Theme.controlSize
    delegate: MenuItem {
        id: option
        required property var model
        required property int index
        width: ListView.view ? ListView.view.width : implicitWidth
        text: control.textRole ? option.model[control.textRole] : option.model.modelData
        checkable: true
        checked: control.currentIndex === index
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
    }
    indicator: Icon {
        x: control.width - width - Theme.space8
        anchors.verticalCenter: parent.verticalCenter
        name: "chevronDown"
        color: Theme.muted
    }
    contentItem: Label {
        text: control.displayText
        kind: control.small ? "small" : "control"
        font.weight: 400
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: Theme.radiusSm
        color: control.hovered ? Theme.hover : Theme.panel
        border.color: control.visualFocus || control.down ? Theme.accent : Theme.lineStrong
        Rectangle {
            visible: control.visualFocus
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: "transparent"
            border.width: 3
            border.color: Theme.accentSoft
            z: -1
        }
    }
    popup: T.Popup {
        y: control.height + Theme.space4
        width: Math.max(control.width, 180)
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 320)
        padding: Theme.space4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
        }
        background: Rectangle {
            radius: Theme.radiusMd
            color: Theme.panel
            border.color: Theme.line
            Shadow { radius: Theme.radiusMd }
        }
    }
}
