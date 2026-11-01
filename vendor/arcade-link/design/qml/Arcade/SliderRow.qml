pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// Slider row (STANDARD §5.1): label and description, the value with its unit
// right-aligned on the label line, the slider full width below. Arrow keys step.
ListRow {
    id: root
    property alias value: slider.value
    property alias from: slider.from
    property alias to: slider.to
    property alias stepSize: slider.stepSize
    property alias slider: slider
    property string unit: ""
    // The value as shown (override for other units or precision).
    function formatValue(v: real): string { return String(Math.round(v)) }
    signal moved(real value)
    stacked: true
    meta: formatValue(slider.value) + (unit ? " " + unit : "")
    Slider {
        id: slider
        width: root.width - Theme.space14 * 2
        snapMode: T.Slider.SnapAlways
        onMoved: root.moved(value)
    }
}
