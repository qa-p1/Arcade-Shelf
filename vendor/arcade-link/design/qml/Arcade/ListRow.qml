pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

// A list or settings row (STANDARD §5.1): optional leading item, title
// (body 500) and an optional subtitle (small, muted, wraps), then trailing
// controls (the row's children). At least 52 px; 1 px line above every row
// but the first in its list. `clickable` rows highlight on hover.
Item {
    id: root
    property string title: ""
    property string subtitle: ""
    // Right-aligned on the title line (a slider row's value with its unit).
    property string meta: ""
    property Component leading
    property bool clickable: false
    // The control goes under the text instead of beside it.
    property bool stacked: false
    property real minimumHeight: Theme.row
    default property alias trailing: trailingRow.data
    readonly property bool first: Positioner.index <= 0
    readonly property bool last: Positioner.isLastItem
    signal clicked()
    width: parent ? parent.width : implicitWidth
    implicitWidth: 360
    implicitHeight: Math.max(minimumHeight, content.implicitHeight + Theme.space10 * 2)
    Accessible.role: clickable ? Accessible.Button : Accessible.ListItem
    Accessible.name: title
    Accessible.description: subtitle
    Accessible.onPressAction: if (clickable) root.clicked()

    Rectangle {
        anchors.fill: parent
        visible: root.clickable && hover.hovered
        color: Theme.sunken
        topLeftRadius: root.first ? Theme.radiusMd - 1 : 0
        topRightRadius: root.first ? Theme.radiusMd - 1 : 0
        bottomLeftRadius: root.last ? Theme.radiusMd - 1 : 0
        bottomRightRadius: root.last ? Theme.radiusMd - 1 : 0
    }
    Rectangle {
        visible: !root.first
        width: parent.width
        height: 1
        color: Theme.line
    }
    HoverHandler { id: hover; enabled: root.clickable; cursorShape: Qt.PointingHandCursor }
    TapHandler { enabled: root.clickable; onTapped: root.clicked() }

    GridLayout {
        id: content
        x: Theme.space14
        width: parent.width - Theme.space14 * 2
        anchors.verticalCenter: parent.verticalCenter
        columns: root.stacked ? 1 : 3
        columnSpacing: Theme.space12
        rowSpacing: Theme.space10
        Loader {
            active: !!root.leading
            visible: active
            sourceComponent: root.leading
            Layout.alignment: Qt.AlignVCenter
        }
        Column {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 1
            Item {
                width: parent.width
                implicitHeight: titleText.implicitHeight
                Label {
                    id: titleText
                    width: parent.width - (metaText.visible ? metaText.implicitWidth + Theme.space12 : 0)
                    text: root.title
                    medium: true
                    elide: Text.ElideRight
                    HoverHandler { id: titleHover; enabled: titleText.truncated }
                    ToolTip { visible: titleHover.hovered && titleText.truncated; text: root.title }
                }
                Label {
                    id: metaText
                    visible: root.meta !== ""
                    anchors.right: parent.right
                    anchors.baseline: titleText.baseline
                    text: root.meta
                    kind: "small"
                    color: Theme.muted
                    font.features: { "tnum": 1, "cv11": 1 }
                }
            }
            Label {
                visible: root.subtitle !== ""
                width: parent.width
                text: root.subtitle
                kind: "small"
                color: Theme.muted
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }
        }
        Row {
            id: trailingRow
            spacing: Theme.space6
            Layout.alignment: root.stacked ? Qt.AlignLeft : Qt.AlignRight | Qt.AlignVCenter
            Layout.fillWidth: root.stacked
        }
    }
}
