pragma ComponentBehavior: Bound
import QtQuick

// Page head (Template A): title (21/600), one-line subtitle, actions right
// (the head's children).
Item {
    id: root
    property string title: ""
    property string subtitle: ""
    default property alias actions: actionRow.data
    width: parent ? parent.width : implicitWidth
    implicitWidth: 480
    implicitHeight: Math.max(texts.implicitHeight, actionRow.implicitHeight)
    Column {
        id: texts
        width: parent.width - (actionRow.width ? actionRow.width + Theme.space16 : 0)
        anchors.bottom: parent.bottom
        spacing: Theme.space4
        Label {
            width: parent.width
            text: root.title
            kind: "title"
            elide: Text.ElideRight
            Accessible.role: Accessible.Heading
        }
        Label {
            visible: root.subtitle !== ""
            width: parent.width
            text: root.subtitle
            color: Theme.muted
            wrapMode: Text.Wrap
        }
    }
    Row {
        id: actionRow
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: Theme.space8
    }
}
