pragma ComponentBehavior: Bound
import QtQuick

// A whole-number input with its unit: Up/Down step, typing commits on Enter
// or when focus leaves; out-of-range values are clamped.
Row {
    id: root
    property int value: 0
    property int from: 0
    property int to: 100
    property int stepSize: 1
    property string unit: ""
    property alias field: field
    signal valueModified(int value)
    spacing: Theme.space8
    Accessible.role: Accessible.SpinBox
    Accessible.name: unit

    function commit(v) {
        const clamped = Math.max(from, Math.min(to, Math.round(v)));
        field.text = String(clamped);
        if (clamped !== value) { value = clamped; valueModified(clamped); }
    }

    TextField {
        id: field
        width: 88
        horizontalAlignment: TextInput.AlignRight
        inputMethodHints: Qt.ImhDigitsOnly
        validator: IntValidator { bottom: Math.min(0, root.from); top: root.to }
        text: String(root.value)
        onEditingFinished: root.commit(Number(text || root.from))
        Keys.onUpPressed: root.commit(root.value + root.stepSize)
        Keys.onDownPressed: root.commit(root.value - root.stepSize)
    }
    Label {
        visible: root.unit !== ""
        text: root.unit
        color: Theme.muted
        anchors.verticalCenter: parent.verticalCenter
    }
}
