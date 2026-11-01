pragma ComponentBehavior: Bound
import QtQuick

// A key combination as chips (["Ctrl", "Shift", "K"]), with alternatives
// after "or".
Row {
    id: root
    property var keys: []
    property var alternatives: []
    property bool small: false
    spacing: Theme.space4
    Accessible.role: Accessible.StaticText
    Accessible.name: [root.keys.join("+")].concat(root.alternatives.map(a => a.join("+"))).join(" or ")
    Repeater {
        model: [root.keys].concat(root.alternatives)
        Row {
            id: combo
            required property var modelData
            required property int index
            spacing: Theme.space4
            Label {
                visible: combo.index > 0
                text: "or"
                kind: "caption"
                color: Theme.muted
                anchors.verticalCenter: parent.verticalCenter
            }
            Repeater {
                model: combo.modelData
                KeyChip {
                    required property string modelData
                    text: modelData
                    small: root.small
                }
            }
        }
    }
}
