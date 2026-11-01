pragma ComponentBehavior: Bound
import QtQuick

// A settings or list section: heading (and meta) 10 px above a bordered
// list of ListRow / SwitchRow children separated by 1 px lines, then an
// optional muted note.
Column {
    id: root
    property string title: ""
    property string meta: ""
    property string note: ""
    default property alias rows: list.data
    width: parent ? parent.width : 360
    spacing: Theme.space10

    Item {
        visible: root.title !== "" || root.meta !== ""
        width: parent.width
        implicitHeight: Math.max(heading.implicitHeight, metaText.implicitHeight)
        Label {
            id: heading
            text: root.title
            kind: "heading"
            width: parent.width - metaText.width - Theme.space12
            elide: Text.ElideRight
            Accessible.role: Accessible.Heading
        }
        Label {
            id: metaText
            anchors.right: parent.right
            anchors.baseline: heading.baseline
            text: root.meta
            kind: "small"
            color: Theme.muted
        }
    }
    Rectangle {
        width: parent.width
        height: list.implicitHeight + 2
        radius: Theme.radiusMd
        color: Theme.panel
        border.color: Theme.line
        Shadow { radius: Theme.radiusMd; pop: false }
        Column {
            id: list
            x: 1; y: 1
            width: parent.width - 2
        }
    }
    Label {
        visible: root.note !== ""
        width: parent.width
        text: root.note
        kind: "small"
        color: Theme.muted
        wrapMode: Text.Wrap
    }
}
