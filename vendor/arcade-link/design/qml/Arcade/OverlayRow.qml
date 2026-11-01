pragma ComponentBehavior: Bound
import QtQuick

// An overlay result row: 40 px (one line) or 52 px (with subtitle); leading
// item, title (body 500), subtitle (small muted), trailing meta. Selected:
// accent-soft fill and a 2 px accent bar; hover: hover fill.
Item {
    id: root
    property string title: ""
    property string subtitle: ""
    property string meta: ""
    property Component leading
    property bool selected: false
    signal clicked()
    signal activated()
    width: parent ? parent.width : implicitWidth
    implicitWidth: 320
    implicitHeight: subtitle !== "" ? Theme.rowOverlay2 : Theme.rowOverlay
    Accessible.role: Accessible.ListItem
    Accessible.name: title
    Accessible.description: subtitle
    Accessible.selected: selected
    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSm
        color: root.selected ? Theme.accentSoft : hover.hovered ? Theme.hover : "transparent"
        Rectangle {
            visible: root.selected
            width: Theme.selectionBar
            height: parent.height - Theme.space8 * 2
            anchors.verticalCenter: parent.verticalCenter
            radius: 1
            color: Theme.accent
        }
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: root.clicked(); onDoubleTapped: root.activated() }
    Row {
        x: Theme.space12
        width: parent.width - Theme.space12 * 2
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space10
        Loader {
            id: lead
            active: !!root.leading
            visible: active
            sourceComponent: root.leading
            anchors.verticalCenter: parent.verticalCenter
        }
        Column {
            width: parent.width - (lead.visible ? lead.width + parent.spacing : 0) - (metaText.visible ? metaText.width + parent.spacing : 0)
            anchors.verticalCenter: parent.verticalCenter
            Label { width: parent.width; text: root.title; medium: true; elide: Text.ElideRight }
            Label { visible: root.subtitle !== ""; width: parent.width; text: root.subtitle; kind: "small"; color: Theme.muted; elide: Text.ElideRight }
        }
        Label {
            id: metaText
            visible: root.meta !== ""
            text: root.meta
            kind: "small"
            color: Theme.muted
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
