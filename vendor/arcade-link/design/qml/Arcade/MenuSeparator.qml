pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// 1 px line with 4 px above and below, between menu groups.
T.MenuSeparator {
    topPadding: Theme.space4
    bottomPadding: Theme.space4
    implicitHeight: topPadding + bottomPadding + 1
    contentItem: Rectangle { implicitHeight: 1; color: Theme.line }
}
