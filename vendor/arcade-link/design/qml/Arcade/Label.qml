pragma ComponentBehavior: Bound
import QtQuick

// Kit text: Inter at one step of the type scale.
// kind: display, title, titleSm, heading, body, control, small, caption, mono.
Text {
    id: root
    property string kind: "body"
    // Medium weight (500) for row titles and labels, whatever the kind's weight.
    property bool medium: false
    readonly property real lineSize: Theme[root.kind + "Line"] || Theme.bodyLine
    color: Theme.text
    textFormat: Text.PlainText
    font.family: kind === "mono" ? Theme.fontMono : Theme.fontSans
    font.pixelSize: Theme[root.kind + "Size"] || Theme.bodySize
    font.weight: medium ? 500 : (Theme[root.kind + "Weight"] || Theme.bodyWeight)
    font.features: { "cv11": 1 }
    // Wrapped text keeps the kit's line height; single lines keep the font's own.
    lineHeightMode: wrapMode === Text.NoWrap ? Text.ProportionalHeight : Text.FixedHeight
    lineHeight: wrapMode === Text.NoWrap ? 1 : lineSize
}
