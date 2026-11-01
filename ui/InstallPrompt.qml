pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import Arcade

// The first-run install / update prompt (STANDARD §5.1): the kit dialog with
// Link's shared strings, options and button order. AppInstaller.cpp creates it
// only while it asks, then reads `clicked` and `checks`.
Window {
    id: root
    required property string heading
    required property var paragraphs   // body, then notes
    required property var options      // [{ label, checked }]
    required property var buttons      // the main action first
    property int clicked: -1
    property var checks: []
    signal finished()

    title: "Install — Arcade Shelf"
    flags: Qt.Dialog
    width: 460
    height: content.implicitHeight + 44
    minimumWidth: width; maximumWidth: width
    minimumHeight: height; maximumHeight: height
    color: Theme.panel
    visible: true
    Component.onCompleted: {
        Theme.appId = "arcade.shelf";
        checks = options.map(option => !!option.checked);
    }
    onClosing: finished()

    Column {
        id: content
        x: 22; y: 22
        width: parent.width - 44
        spacing: Theme.space10
        Row {
            spacing: Theme.space12
            GlyphTile { appId: "arcade.shelf"; size: Theme.tile }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                width: content.width - Theme.tile - Theme.space12
                text: root.heading
                kind: "titleSm"
                wrapMode: Text.Wrap
            }
        }
        Repeater {
            model: root.paragraphs
            delegate: DialogBody {
                required property string modelData
                width: content.width
                text: modelData
            }
        }
        Column {
            visible: root.options.length > 0
            width: content.width
            topPadding: Theme.space4
            Repeater {
                model: root.options
                delegate: CheckBox {
                    required property var modelData
                    required property int index
                    objectName: "installOption-" + index
                    text: modelData.label
                    checked: !!modelData.checked
                    onToggled: { const next = root.checks.slice(); next[index] = checked; root.checks = next; }
                }
            }
        }
        // The main action is rightmost and the default, as in every Arcade prompt.
        Flow {
            width: content.width
            topPadding: Theme.space10
            layoutDirection: Qt.RightToLeft
            spacing: Theme.space8
            Repeater {
                model: root.buttons
                delegate: Button {
                    required property string modelData
                    required property int index
                    objectName: "installButton-" + index
                    text: modelData
                    variant: index === 0 ? "primary" : "default"
                    onClicked: { root.clicked = index; root.close(); }
                }
            }
        }
    }
    Shortcut { sequence: "Return"; onActivated: { root.clicked = 0; root.close(); } }
    Shortcut { sequence: "Esc"; onActivated: root.close() }
}
