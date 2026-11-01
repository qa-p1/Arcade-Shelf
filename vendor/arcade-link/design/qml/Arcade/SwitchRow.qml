pragma ComponentBehavior: Bound
import QtQuick

// A settings row whose whole width toggles its switch.
ListRow {
    id: root
    property alias checked: toggle.checked
    property alias control: toggle
    signal toggled(bool checked)
    clickable: enabled
    opacity: enabled ? 1 : 0.6
    Accessible.role: Accessible.CheckBox
    Accessible.checkable: true
    Accessible.checked: toggle.checked
    onClicked: { toggle.toggle(); root.toggled(toggle.checked) }
    Switch {
        id: toggle
        text: root.title
        enabled: root.enabled
        onToggled: root.toggled(checked)
    }
}
