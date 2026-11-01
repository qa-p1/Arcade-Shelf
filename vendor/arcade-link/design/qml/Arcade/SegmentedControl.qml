pragma ComponentBehavior: Bound
import QtQuick

// Up to four short options (STANDARD §5.1; the theme picker is one).
// `options`: strings or { value, label, count }. Arrow keys move the choice.
FocusScope {
    id: root
    property var options: []
    property var value
    signal activated(var value)
    implicitWidth: row.implicitWidth + 6
    implicitHeight: Theme.controlSm + 6
    activeFocusOnTab: true
    Accessible.role: Accessible.RadioButton
    readonly property int currentIndex: {
        for (let i = 0; i < options.length; ++i) if (valueOf(options[i]) === value) return i;
        return -1;
    }
    function valueOf(o) { return typeof o === "object" ? o.value : o }
    function labelOf(o) { return typeof o === "object" ? (o.label || String(o.value)) : String(o) }
    function choose(i) {
        if (i < 0 || i >= options.length) return;
        const v = valueOf(options[i]);
        if (v !== value) { value = v; activated(v) }
    }
    Keys.onLeftPressed: choose(Math.max(0, currentIndex - 1))
    Keys.onRightPressed: choose(Math.min(options.length - 1, currentIndex + 1))

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusTile
        color: Theme.sunken
        border.color: Theme.line
        FocusRing { visible: root.activeFocus; controlRadius: Theme.radiusTile }
    }
    Row {
        id: row
        x: 3; y: 3
        spacing: Theme.space2
        Repeater {
            model: root.options
            delegate: Rectangle {
                id: segment
                required property var modelData
                required property int index
                readonly property bool active: index === root.currentIndex
                width: segmentRow.implicitWidth + Theme.space12 * 2
                height: Theme.controlSm
                radius: 6
                color: active ? Theme.panel : "transparent"
                border.color: active ? Theme.line : "transparent"
                Accessible.role: Accessible.RadioButton
                Accessible.name: root.labelOf(modelData)
                Accessible.checked: active
                Accessible.onPressAction: root.choose(index)
                Shadow { visible: segment.active; radius: 6; pop: false }
                Row {
                    id: segmentRow
                    anchors.centerIn: parent
                    spacing: Theme.space6
                    Label {
                        text: root.labelOf(segment.modelData)
                        kind: "control"
                        color: segment.active || tap.hovered ? Theme.text : Theme.muted
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Rectangle {
                        readonly property int count: typeof segment.modelData === "object" && segment.modelData.count ? segment.modelData.count : 0
                        visible: count > 0
                        width: Math.max(17, countText.implicitWidth + Theme.space4 * 2)
                        height: 17
                        radius: 8.5
                        color: Theme.warnSoft
                        anchors.verticalCenter: parent.verticalCenter
                        Label { id: countText; anchors.centerIn: parent; text: parent.count; kind: "caption"; color: Theme.warnInk }
                    }
                }
                HoverHandler { id: tap; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: { root.forceActiveFocus(); root.choose(segment.index) } }
            }
        }
    }
}
