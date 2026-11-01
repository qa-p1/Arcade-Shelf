pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// Kit tooltip: caption text on panel, 500 ms delay, at most 280 px wide.
T.ToolTip {
    id: control
    delay: Theme.tooltipDelay
    x: parent ? (parent.width - implicitWidth) / 2 : 0
    y: -implicitHeight - Theme.space6
    margins: Theme.space6
    topPadding: Theme.space4
    bottomPadding: Theme.space4
    leftPadding: Theme.space8
    rightPadding: Theme.space8
    implicitWidth: Math.min(280, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent
    contentItem: Label {
        text: control.text
        kind: "caption"
        wrapMode: Text.Wrap
    }
    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.panel
        border.color: Theme.line
    }
}
