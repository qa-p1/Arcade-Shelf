pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// Kit button (STANDARD §5). variant: "default", "primary" (accent),
// "ghost", "danger" (outline) or "destructive" (filled). `small` is the
// 28 px size; a button with an icon and no text is icon-only (30 or 28 px
// square). `busy` replaces the icon with a spinner and keeps the width.
T.Button {
    id: control
    property string variant: "default"
    property bool small: false
    property string iconName: ""
    property bool busy: false
    // Shown after the tooltip delay while hovered (lazily created).
    property string tooltip: ""
    readonly property bool iconOnly: iconName !== "" && text === ""
    readonly property color ink: variant === "primary" ? Theme.accentText
        : variant === "destructive" ? Theme.onFill
        : variant === "danger" ? Theme.dangerInk
        : variant === "ghost" && !(hovered && enabled) ? Theme.muted : Theme.text

    implicitHeight: small ? Theme.controlSm : Theme.control
    implicitWidth: iconOnly ? (small ? Theme.controlSm : 30)
        : Math.max(implicitHeight, implicitContentWidth + leftPadding + rightPadding)
    leftPadding: iconOnly ? 0 : small ? Theme.space10 : Theme.space12
    rightPadding: leftPadding
    topPadding: 0
    bottomPadding: 0
    spacing: Theme.space6
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : 0.45
    Accessible.name: text || tooltip
    Accessible.description: tooltip

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        transform: Translate { y: control.down ? 1 : 0 }
        Row {
            id: row
            anchors.centerIn: parent
            spacing: control.spacing
            Item {
                visible: control.iconName !== "" || control.busy
                width: control.small ? 15 : Theme.iconSize
                height: width
                anchors.verticalCenter: parent.verticalCenter
                Icon {
                    anchors.fill: parent
                    visible: !control.busy
                    name: control.iconName
                    size: parent.width
                    color: control.ink
                }
                Loader {
                    anchors.centerIn: parent
                    active: control.busy
                    sourceComponent: Spinner { color: control.ink }
                }
            }
            Label {
                visible: control.text !== ""
                text: control.text
                kind: control.small ? "small" : "control"
                medium: true
                color: control.ink
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    background: Rectangle {
        radius: Theme.radiusSm
        readonly property bool lit: control.hovered && control.enabled && !control.busy
        color: control.variant === "primary" ? (lit ? Qt.lighter(Theme.accent, 1.06) : Theme.accent)
            : control.variant === "destructive" ? (lit ? Qt.lighter(Theme.danger, 1.06) : Theme.danger)
            : control.variant === "danger" && lit ? Theme.dangerSoft
            : control.variant === "ghost" ? (lit ? Theme.hover : "transparent")
            : lit ? Theme.hover : Theme.panel
        border.width: 1
        border.color: control.variant === "primary" ? Theme.accent
            : control.variant === "destructive" ? Theme.danger
            : control.variant === "danger" && lit ? Theme.danger
            : control.variant === "ghost" ? "transparent" : Theme.lineStrong
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        FocusRing { visible: control.visualFocus; controlRadius: Theme.radiusSm }
    }

    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    Loader {
        active: control.tooltip !== "" && control.hovered
        sourceComponent: ToolTip { parent: control; text: control.tooltip; visible: true }
    }
}
