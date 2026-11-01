pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

// A 1 px line (horizontal unless `vertical`), sized for layouts too.
Rectangle {
    property bool vertical: false
    color: Theme.line
    implicitWidth: vertical ? 1 : 0
    implicitHeight: vertical ? 0 : 1
    Layout.fillWidth: !vertical
    Layout.fillHeight: vertical
    Layout.preferredHeight: vertical ? -1 : 1
    Layout.preferredWidth: vertical ? 1 : -1
}
