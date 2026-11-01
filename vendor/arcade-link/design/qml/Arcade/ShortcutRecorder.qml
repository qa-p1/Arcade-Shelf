pragma ComponentBehavior: Bound
import QtQuick

// The shortcut recorder (STANDARD §5.1). Shows `sequence` ("Ctrl+Alt+S") as
// key chips; click or Enter records ("Press keys…"); Esc cancels; Backspace
// clears when `allowEmpty`. A modifier is required except for F1–F24.
// `captured(sequence)` fires with a valid combo: apply it, then set
// `conflict` ("Used by Arcade Find › Search") to show the warn notice with
// "Use anyway" / "Choose another". `note` is the desktop note from Link.
Column {
    id: root
    property string sequence: ""
    property string defaultSequence: ""
    property bool allowEmpty: false
    property string conflict: ""
    property string note: ""
    property bool recording: false
    readonly property bool mac: Qt.platform.os === "osx" || Qt.platform.os === "macos"
    signal captured(string sequence)
    signal cleared()
    signal resetRequested()
    signal useAnyway()
    signal chooseAnother()
    spacing: Theme.space8
    width: parent ? parent.width : 360

    // "Ctrl+Alt+S" → display keys, in platform order and names.
    function displayKeys(seq) {
        if (!seq) return [];
        const parts = seq.split("+");
        if (!mac) return parts;
        const map = { "Ctrl": "⌃", "Alt": "⌥", "Shift": "⇧", "Super": "⌘", "Meta": "⌘", "Cmd": "⌘" };
        return parts.map(p => map[p] || p);
    }
    function keyName(event) {
        const k = event.key;
        if (k >= Qt.Key_F1 && k <= Qt.Key_F24) return "F" + (k - Qt.Key_F1 + 1);
        if (k >= Qt.Key_A && k <= Qt.Key_Z) return String.fromCharCode(k);
        if (k >= Qt.Key_0 && k <= Qt.Key_9) return String.fromCharCode(k);
        const named = {};
        named[Qt.Key_Space] = "Space"; named[Qt.Key_Tab] = "Tab"; named[Qt.Key_Return] = "Enter"; named[Qt.Key_Enter] = "Enter";
        named[Qt.Key_Left] = "Left"; named[Qt.Key_Right] = "Right"; named[Qt.Key_Up] = "Up"; named[Qt.Key_Down] = "Down";
        named[Qt.Key_Home] = "Home"; named[Qt.Key_End] = "End"; named[Qt.Key_PageUp] = "PageUp"; named[Qt.Key_PageDown] = "PageDown";
        named[Qt.Key_Insert] = "Insert"; named[Qt.Key_Delete] = "Delete"; named[Qt.Key_Comma] = ","; named[Qt.Key_Period] = ".";
        named[Qt.Key_Slash] = "/"; named[Qt.Key_Semicolon] = ";"; named[Qt.Key_Apostrophe] = "'"; named[Qt.Key_BracketLeft] = "[";
        named[Qt.Key_BracketRight] = "]"; named[Qt.Key_Minus] = "-"; named[Qt.Key_Equal] = "="; named[Qt.Key_QuoteLeft] = "`";
        named[Qt.Key_Backslash] = "\\";
        return named[k] || "";
    }
    function sequenceFor(event) {
        const name = keyName(event);
        if (!name) return "";
        const m = event.modifiers;
        // Qt swaps Control and Meta on macOS: ControlModifier is ⌘ there.
        const ctrl = mac ? (m & Qt.MetaModifier) : (m & Qt.ControlModifier);
        const sup = mac ? (m & Qt.ControlModifier) : (m & Qt.MetaModifier);
        const parts = [];
        if (ctrl) parts.push("Ctrl");
        if (m & Qt.AltModifier) parts.push("Alt");
        if (m & Qt.ShiftModifier) parts.push("Shift");
        if (sup) parts.push("Super");
        if (!parts.length && !/^F\d+$/.test(name)) return "";
        if (!parts.length || parts.every(p => p === "Shift")) { if (!/^F\d+$/.test(name)) return "" }
        parts.push(name);
        return parts.join("+");
    }

    Row {
        spacing: Theme.space10
        Rectangle {
            id: field
            width: Math.max(140, chips.implicitWidth + Theme.space8 * 2)
            height: Math.max(Theme.control, chips.implicitHeight + Theme.space2 * 2)
            radius: Theme.radiusSm
            color: Theme.panel
            border.color: root.recording ? Theme.accent : Theme.lineStrong
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: root.recording ? "Press keys" : "Shortcut " + (root.sequence || "none")
            Accessible.onPressAction: root.recording = true
            Rectangle {
                visible: root.recording
                anchors.fill: parent
                anchors.margins: -3
                radius: parent.radius + 3
                color: "transparent"
                border.width: 3
                border.color: Theme.accentSoft
                z: -1
            }
            FocusRing { visible: field.activeFocus && !root.recording; controlRadius: Theme.radiusSm }
            Row {
                id: chips
                x: Theme.space8
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space6
                KeyCombo {
                    visible: !root.recording && root.sequence !== ""
                    keys: root.displayKeys(root.sequence)
                    anchors.verticalCenter: parent.verticalCenter
                }
                Label {
                    visible: root.recording || root.sequence === ""
                    text: root.recording ? "Press keys…" : "No shortcut"
                    kind: "control"
                    font.weight: 400
                    color: Theme.muted
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: { field.forceActiveFocus(); root.recording = true } }
            Keys.onPressed: function(event) {
                if (!root.recording) {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) { root.recording = true; event.accepted = true }
                    return;
                }
                event.accepted = true;
                if (event.key === Qt.Key_Escape && event.modifiers === Qt.NoModifier) { root.recording = false; return }
                if (event.key === Qt.Key_Backspace && event.modifiers === Qt.NoModifier) {
                    if (root.allowEmpty) { root.recording = false; root.cleared() }
                    return;
                }
                const seq = root.sequenceFor(event);
                if (!seq) return;
                root.recording = false;
                root.captured(seq);
            }
            onActiveFocusChanged: if (!activeFocus) root.recording = false
        }
        LinkButton {
            visible: root.defaultSequence !== "" && root.sequence !== root.defaultSequence
            text: "Reset to default"
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.resetRequested()
        }
    }
    Notice {
        visible: root.conflict !== ""
        tone: "warn"
        text: root.conflict
        Button { text: "Choose another"; small: true; onClicked: { root.chooseAnother(); field.forceActiveFocus(); root.recording = true } }
        Button { text: "Use anyway"; small: true; onClicked: root.useAnyway() }
    }
    Label {
        visible: root.note !== ""
        width: parent.width
        text: root.note
        kind: "small"
        color: Theme.muted
        wrapMode: Text.Wrap
    }
}
