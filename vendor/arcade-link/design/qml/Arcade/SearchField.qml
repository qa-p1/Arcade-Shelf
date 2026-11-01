pragma ComponentBehavior: Bound
import QtQuick

// Kit search field: leading search icon, clear button, 36 px (`height` 44
// in an overlay header, 56 in a launcher). Esc clears first; with nothing to
// clear it emits `escaped` so the page or overlay can close or go back.
FocusScope {
    id: root
    property alias text: input.text
    property alias placeholderText: input.placeholderText
    property alias input: input
    property bool overlay: false
    signal edited(string text)
    signal accepted()
    signal escaped()
    implicitWidth: 240
    implicitHeight: overlay ? Theme.overlaySearch : Theme.search
    Accessible.role: Accessible.EditableText
    Accessible.name: placeholderText

    function clear() { input.clear(); root.edited("") }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSm
        color: Theme.panel
        border.color: input.activeFocus ? Theme.accent : Theme.lineStrong
        Rectangle {
            visible: input.activeFocus
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: "transparent"
            border.width: 3
            border.color: Theme.accentSoft
            z: -1
        }
    }
    Icon {
        id: lens
        name: "search"
        color: Theme.muted
        x: Theme.space12
        anchors.verticalCenter: parent.verticalCenter
    }
    TextField {
        id: input
        focus: true
        x: lens.x + lens.width + Theme.space8 - leftPadding
        width: clearButton.x - x + (clearButton.visible ? -Theme.space4 : Theme.space12 - rightPadding)
        anchors.verticalCenter: parent.verticalCenter
        font.pixelSize: Theme.bodySize
        background: null
        onTextEdited: root.edited(text)
        onAccepted: root.accepted()
        Keys.onEscapePressed: function(event) {
            if (text.length) root.clear(); else root.escaped();
            event.accepted = true;
        }
    }
    Button {
        id: clearButton
        visible: input.length > 0
        variant: "ghost"
        iconName: "x"
        small: true
        width: 20; height: 20
        x: parent.width - width - Theme.space8
        anchors.verticalCenter: parent.verticalCenter
        focusPolicy: Qt.NoFocus
        Accessible.name: "Clear"
        onClicked: { root.clear(); input.forceActiveFocus() }
    }
}
