pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// Kit menu / context menu: panel, line border, radius 10, shadow-pop,
// 4 px padding, at least 180 px wide. Use MenuItem and MenuSeparator inside.
T.Menu {
    id: control
    padding: Theme.space4
    margins: Theme.space8
    overlap: 1
    implicitWidth: Math.max(180, contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    delegate: MenuItem {}
    contentItem: ListView {
        implicitHeight: contentHeight
        implicitWidth: {
            let w = 0;
            for (let i = 0; i < count; ++i) {
                const item = control.itemAt(i);
                if (item) w = Math.max(w, item.implicitWidth);
            }
            return w;
        }
        model: control.contentModel
        interactive: Window.window ? contentHeight + control.topPadding + control.bottomPadding > control.height : false
        clip: true
        currentIndex: control.currentIndex
    }
    background: Rectangle {
        radius: Theme.radiusMd
        color: Theme.panel
        border.color: Theme.line
        Shadow { radius: Theme.radiusMd }
    }
    T.Overlay.modal: Rectangle { color: "transparent" }
}
