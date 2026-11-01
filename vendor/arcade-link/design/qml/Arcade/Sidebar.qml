pragma ComponentBehavior: Bound
import QtQuick

// Template A sidebar: 212 px on bg with a right line; the brand (28 px mark
// tile + app name), NavItem tabs (its children, Up/Down move between them),
// a footer slot and "Version x.y.z". `collapsed` (below 760 px) shows icons.
Rectangle {
    id: root
    property string appName: ""
    property string appId: Theme.appId
    property string version: ""
    property bool collapsed: false
    property int currentIndex: 0
    default property alias items: tabColumn.data
    property alias footer: footerSlot.data
    implicitWidth: collapsed ? Theme.sidebarCollapsed : Theme.sidebar
    color: Theme.bg
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.line }

    Row {
        id: brand
        x: root.collapsed ? (root.width - Theme.tileSm) / 2 : Theme.space12 + Theme.space8
        y: Theme.space16
        spacing: Theme.space10
        Rectangle {
            width: Theme.tileSm; height: Theme.tileSm
            radius: Theme.radiusSm
            color: Theme.text
            Glyph { anchors.centerIn: parent; appId: root.appId; color: Theme.bg }
        }
        Label {
            visible: !root.collapsed
            text: root.appName
            kind: "heading"
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    Column {
        id: tabColumn
        x: Theme.space12
        y: brand.y + brand.height + Theme.space24
        width: root.width - Theme.space12 * 2
        spacing: Theme.space2
        // Up/Down move between tabs (the focused tab passes the key here).
        Keys.onUpPressed: root.focusTab(root.currentIndex - 1)
        Keys.onDownPressed: root.focusTab(root.currentIndex + 1)
    }
    function focusTab(i) {
        const tabs = tabColumn.children;
        if (i < 0 || i >= tabs.length) return;
        currentIndex = i;
        tabs[i].forceActiveFocus();
    }
    function syncTabs() {
        const tabs = tabColumn.children;
        for (let i = 0; i < tabs.length; ++i) tabs[i].checked = i === currentIndex;
    }
    onCurrentIndexChanged: syncTabs()
    Component.onCompleted: {
        const tabs = tabColumn.children;
        for (let i = 0; i < tabs.length; ++i) {
            const index = i;
            tabs[i].clicked.connect(() => { root.currentIndex = index; root.syncTabs(); });
            tabs[i].collapsed = Qt.binding(() => root.collapsed);
        }
        syncTabs();
    }
    Column {
        id: foot
        x: Theme.space12 + Theme.space8
        width: root.width - x * 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.space14
        spacing: Theme.space10
        Rectangle { width: parent.width + Theme.space8 * 2; x: -Theme.space8; height: 1; color: Theme.line }
        Column { id: footerSlot; width: parent.width; spacing: Theme.space10 }
        Item {
            visible: root.version !== "" && !root.collapsed
            width: parent.width
            height: versionText.implicitHeight
            Label { text: "Version"; kind: "caption"; color: Theme.muted }
            Label { id: versionText; anchors.right: parent.right; text: root.version; kind: "caption"; color: Theme.muted; font.features: { "tnum": 1, "cv11": 1 } }
        }
    }
}
