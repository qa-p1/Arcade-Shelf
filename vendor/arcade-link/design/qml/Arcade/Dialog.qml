pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// Kit dialog: 460 px wide (less on a small window), 22 px padding, radius 12,
// title-sm heading, muted body, actions right-aligned with the primary last.
// Esc and the backdrop close it unless `destructive` or `busy`.
T.Dialog {
    id: control
    property string body: ""
    property bool destructive: false
    property bool busy: false
    // Buttons, right-aligned in order (put the primary last).
    property list<Item> actions
    modal: true
    focus: true
    anchors.centerIn: T.Overlay.overlay
    width: Math.min(460, (T.Overlay.overlay ? T.Overlay.overlay.width : 492) - 32)
    padding: 22
    topPadding: 22
    closePolicy: destructive || busy ? T.Popup.NoAutoClose : T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
    implicitHeight: Math.min(implicitHeaderHeight + implicitContentHeight + implicitFooterHeight + topPadding + bottomPadding + spacing * 2,
                             T.Overlay.overlay ? T.Overlay.overlay.height - 32 : 760)
    spacing: Theme.space8
    header: Item {
        implicitHeight: title.implicitHeight + 22
        Label {
            id: title
            x: 22; y: 22
            width: parent.width - 44
            text: control.title
            kind: "titleSm"
            wrapMode: Text.Wrap
        }
    }
    footer: Item {
        implicitHeight: control.actions.length ? actionRow.implicitHeight + 22 + Theme.space20 : 22
        Flow {
            id: actionRow
            x: 22
            y: Theme.space20
            width: parent.width - 44
            layoutDirection: Qt.RightToLeft
            spacing: Theme.space8
            Repeater {
                // Right-to-left flow: reverse so the primary (last) sits at the right edge.
                model: control.actions.length
                delegate: Item {
                    required property int index
                    readonly property Item action: control.actions[control.actions.length - 1 - index]
                    implicitWidth: action ? action.implicitWidth : 0
                    implicitHeight: action ? action.implicitHeight : 0
                    width: implicitWidth
                    height: implicitHeight
                    Component.onCompleted: if (action) { action.parent = this; action.anchors.fill = this }
                }
            }
        }
    }
    // The body sentence, then whatever the dialog declares inside it.
    contentItem: Column {
        spacing: Theme.space10
        DialogBody {
            visible: control.body !== ""
            text: control.body
            width: parent.width
        }
    }
    background: Rectangle {
        radius: Theme.radiusLg
        color: Theme.panel
        border.color: Theme.line
        Shadow { radius: Theme.radiusLg }
    }
    T.Overlay.modal: Rectangle { color: Theme.scrim }
    T.Overlay.modeless: Rectangle { color: "transparent" }
}
