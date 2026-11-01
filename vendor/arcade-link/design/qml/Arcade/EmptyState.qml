pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

// Empty state: icon, heading, text and actions (its children), centred, in a
// dashed border (set `dashed: false` inside an overlay that has its own edge).
Item {
    id: root
    property string iconName: "info"
    property string title: ""
    property string text: ""
    property bool dashed: true
    default property alias actions: actionRow.data
    implicitWidth: 420
    implicitHeight: column.implicitHeight + (dashed ? Theme.space48 * 2 : Theme.space24 * 2)
    Accessible.role: Accessible.StaticText
    Accessible.name: title + ". " + text

    Shape {
        anchors.fill: parent
        visible: root.dashed
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: Theme.lineStrong
            strokeWidth: 1
            strokeStyle: ShapePath.DashLine
            dashPattern: [4, 3]
            fillColor: "transparent"
            PathRectangle { x: 0.5; y: 0.5; width: root.width - 1; height: root.height - 1; radius: Theme.radiusMd }
        }
    }
    Column {
        id: column
        anchors.centerIn: parent
        width: Math.min(420, root.width - Theme.space24 * 2)
        spacing: Theme.space6
        Icon {
            name: root.iconName
            size: 24
            color: Theme.muted
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Item { width: 1; height: Theme.space6 - parent.spacing + Theme.space6 }
        Label {
            width: parent.width
            text: root.title
            kind: "heading"
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
        Label {
            visible: root.text !== ""
            width: parent.width
            text: root.text
            color: Theme.muted
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
        Item { visible: actionRow.children.length > 0; width: 1; height: Theme.space10 - parent.spacing }
        Row {
            id: actionRow
            spacing: Theme.space8
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }
}
