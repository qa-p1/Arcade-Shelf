pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Templates as T
import Arcade

// Shelf's one frameless window, in three modes: the capsule (216×76), a tab
// docked on a screen edge (30×92) and the expanded panel (Template B). Settings
// is a separate normal window (SettingsWindow.qml), created only while open.
Window {
    id: window
    required property ShelfController shelf
    objectName: "shelfWindow"
    visible: shelf.visible
    title: "Arcade Shelf"
    color: "transparent"
    // On Wayland the compositor decides focus (Hyprland focuses on click or
    // hover). The flag would only make Qt drop that keyboard focus, leaving a
    // clicked-open tab deaf to Escape, so it applies to other platforms only.
    flags: Qt.Window | Qt.FramelessWindowHint | (shelf.alwaysOnTop ? Qt.WindowStaysOnTopHint : 0) | (!shelf.focusPanel && Qt.platform.pluginName !== "wayland" ? Qt.WindowDoesNotAcceptFocus : 0)
    minimumWidth: shelf.expanded ? 440 : dockEdge.length ? (verticalDock ? tabThickness : tabLength) : 216
    minimumHeight: shelf.expanded ? 350 : dockEdge.length ? (verticalDock ? tabLength : tabThickness) : 76
    // The capsule and tab have one fixed size. Saying so lets tiling
    // compositors (Hyprland, Sway) map them floating rather than as a tile.
    maximumWidth: shelf.expanded ? 16777215 : targetWidth
    maximumHeight: shelf.expanded ? 16777215 : targetHeight
    // A docked Shelf is a slim handle on the screen edge, like a stashed
    // picture-in-picture: flat against the edge, rounded toward the screen.
    readonly property int tabThickness: 30
    readonly property int tabLength: 92
    readonly property int targetWidth: shelf.expanded ? panelWidth : dockEdge.length ? (verticalDock ? tabThickness : tabLength) : 216
    readonly property int targetHeight: shelf.expanded ? panelHeight : dockEdge.length ? (verticalDock ? tabLength : tabThickness) : 76
    width: targetWidth
    height: targetHeight
    // Collapsing updates the size and minimum-size bindings in no fixed order.
    // A size applied while the old, larger minimum is still in force is
    // clamped to it (a docked tab stayed panel-tall); reapply once it drops.
    onMinimumWidthChanged: if (width !== targetWidth) width = Qt.binding(() => targetWidth)
    onMinimumHeightChanged: if (height !== targetHeight) height = Qt.binding(() => targetHeight)

    property int panelWidth: 640
    property int panelHeight: 490
    readonly property string dockEdge: shelf.dockEdge
    readonly property bool verticalDock: dockEdge === "left" || dockEdge === "right"
    // System follows the desktop's colour scheme, or the palette where the
    // platform does not report one.
    SystemPalette { id: systemPalette; colorGroup: SystemPalette.Active }
    readonly property bool dark: shelf.theme === "dark" || (shelf.theme === "system"
        && (Application.styleHints.colorScheme === Qt.ColorScheme.Dark
            || (Application.styleHints.colorScheme === Qt.ColorScheme.Unknown && systemPalette.window.hslLightness < 0.5)))
    Binding { target: Theme; property: "mode"; value: window.dark ? "dark" : "light" }
    Binding { target: Theme; property: "reducedMotion"; value: window.shelf.reducedMotion }
    // Shelf's documented exception to opaque overlays: over the platform
    // backdrop the surface is the panel colour at 87 %.
    readonly property color paper: shelf.backdropActive ? Qt.alpha(Theme.panel, 0.87) : Theme.panel
    property int cursorIndex: -1
    property string contextItem: ""
    property string collectionToEdit: ""
    property string collectionMode: "new"
    property string collectionDraft: ""
    property var pendingOffer: ({})
    property bool restoring: true
    property bool resizing: false
    readonly property bool dragHover: dropArea.containsDrag || shelf.nativeDragHover
    property bool previousFocusPanel: shelf.focusPanel
    property bool pulsing: false
    readonly property int selectedCount: shelf.selectedCount
    readonly property var lookPreview: {
        for (let i = 0; i < shelf.offers.length; i++) {
            const offer = shelf.offers[i];
            if (offer.action === "look.preview" || offer.id === "look.preview") return offer;
        }
        return null;
    }

    function iconFor(type: string): string {
        if (type === "folder/reference") return "folder";
        if (type === "text/url") return "link";
        if (type.indexOf("text/") === 0) return "type";
        if (type.indexOf("file/image") === 0) return "image";
        if (type.indexOf("file/video") === 0) return "video";
        if (type.indexOf("file/audio") === 0) return "audio";
        return "file";
    }
    function subtitleFor(item: var): string {
        if (item.available === false) return "Original unavailable";
        if (item.type === "folder/reference") return "Folder";
        // One line only: a snippet's full text belongs to the preview above.
        const firstLine = (item.text || "").split("\n").map(line => line.trim()).find(line => line.length) || "";
        if (item.type === "text/url") return firstLine || "Link";
        if (item.type.indexOf("text/") === 0) return firstLine || "Text";
        return item.type.replace("file/", "") + " · " + Format.formatSize(Number(item.size || 0));
    }
    function countLabel(n: int): string {
        return Format.formatCount(n) + (n === 1 ? " item" : " items");
    }
    function navigate(delta: int, modifiers: int) {
        if (!shelf.items.length) return;
        cursorIndex = Math.max(0, Math.min(shelf.items.length - 1, cursorIndex < 0 ? 0 : cursorIndex + delta));
        shelf.select(shelf.items[cursorIndex].id, !!(modifiers & Qt.ControlModifier), !!(modifiers & Qt.ShiftModifier));
        grid.positionViewAtIndex(cursorIndex, GridView.Contain);
    }
    function runOffer(offer: var) {
        const effects = offer.effects || [];
        const confirm = ["network", "uploads-content", "sends-to-device", "persists", "clipboard", "writes-files", "overwrites-files", "deletes-files"];
        if (effects.some(effect => confirm.indexOf(effect) >= 0)) {
            pendingOffer = offer;
            (confirmActionLoader.get() as Dialog).open();
        } else shelf.runPeer(offer.app, offer.action || offer.id);
    }
    function editCollection(id: string, name: string, mode: string) {
        collectionToEdit = id; collectionMode = mode; collectionDraft = name;
        (collectionDialogLoader.get() as Dialog).open();
    }
    // A deliberate close (Escape, the collapse button) leaves the pointer on
    // the edge tab; without a pause, hovering would peek the panel straight back.
    property double peekHeldUntil: 0
    function closePanel() {
        peekHeldUntil = Date.now() + 900;
        edgeHoverTimer.stop();
        shelf.collapse();
    }
    function dockTo(edge: string) {
        if (!edge.length) { shelf.dock(""); return; }
        shelf.collapse();
        Qt.callLater(function() { window.shelf.dock(edge); });
    }
    function moveItem(delta: int) {
        const rows = shelf.items; let index = -1;
        for (let i = 0; i < rows.length; i++) if (rows[i].id === contextItem) index = i;
        if (index < 0 || index + delta < 0 || index + delta >= rows.length) return;
        const before = delta < 0 ? rows[index - 1].id : index + 2 < rows.length ? rows[index + 2].id : "";
        shelf.reorderItem(contextItem, before);
    }
    function savePosition() {
        if (!restoring && !resizing) shelf.saveGeometry(x, y, panelWidth, panelHeight);
    }

    onXChanged: geometryTimer.restart()
    onYChanged: geometryTimer.restart()
    onWidthChanged: if (!restoring && shelf.expanded) { panelWidth = width; geometryTimer.restart(); }
    onHeightChanged: if (!restoring && shelf.expanded) { panelHeight = height; geometryTimer.restart(); }
    onClosing: function(close) { close.accepted = false; window.shelf.collapse(); }
    onDockEdgeChanged: if (!dockEdge.length) edgeHoverTimer.stop()
    onDarkChanged: shelf.setBackdropDark(dark)
    Component.onCompleted: {
        Theme.appId = "arcade.shelf";
        const geometry = shelf.restoreGeometry();
        panelWidth = Math.max(440, Number(geometry.width || 640));
        panelHeight = Math.max(350, Number(geometry.height || 490));
        if (geometry.x !== undefined) x = Number(geometry.x);
        if (geometry.y !== undefined) y = Number(geometry.y);
        restoring = false;
        shelf.setBackdropDark(dark);
    }
    // Menus and dialogs are built the first time they open (they are most of
    // the window's objects), then kept, so idle memory stays at 0.3's level.
    component Lazy: Loader {
        active: false
        function get(): QtObject { active = true; return item; }
    }
    Timer { id: geometryTimer; interval: 250; onTriggered: window.savePosition() }
    Connections {
        target: window.shelf
        function onChanged() {
            const focusing = window.shelf.focusPanel && !window.previousFocusPanel;
            window.previousFocusPanel = window.shelf.focusPanel;
            if (focusing) {
                window.requestActivate(); interaction.forceActiveFocus();
            }
            if (window.shelf.focusPanel || !window.shelf.expanded) peekDismissTimer.stop();
            window.cursorIndex = Math.min(window.cursorIndex, window.shelf.items.length - 1);
            if (window.shelf.analysisResult && Object.keys(window.shelf.analysisResult).length > 0) { const d = analysisDialogLoader.get() as Dialog; if (!d.visible) d.open(); }
        }
        function onPulse() { window.pulsing = true; pulseTimer.restart(); }
        function onNewCollectionRequested() {
            Qt.callLater(function() { window.editCollection("", "", "new"); });
        }
        function onNativeDragHoverChanged() {
            if (window.shelf.nativeDragHover) {
                peekDismissTimer.stop();
                if (window.dockEdge.length && !window.shelf.expanded) edgeHoverTimer.restart();
            } else {
                edgeHoverTimer.stop();
                if (window.shelf.expanded && !window.shelf.focusPanel && !surfaceHover.hovered && !dropArea.containsDrag) peekDismissTimer.restart();
            }
        }
    }
    Timer { id: pulseTimer; interval: window.shelf.reducedMotion ? 900 : 650; onTriggered: window.pulsing = false }
    Timer {
        id: edgeHoverTimer
        interval: 260
        // A drag always peeks; plain hover only when "Open docked Shelf on hover" is on.
        onTriggered: if (window.dockEdge.length && !window.shelf.expanded && (capsuleMouse.containsMouse || window.dragHover)
                         && (window.dragHover || (window.shelf.hoverPeek && Date.now() >= window.peekHeldUntil))) window.shelf.peek()
    }
    Timer {
        id: peekDismissTimer
        interval: 320
        onTriggered: if (window.shelf.expanded && !window.shelf.focusPanel && !surfaceHover.hovered && !window.dragHover) window.shelf.collapse()
    }

    Item {
        id: interaction
        anchors.fill: parent
        // Focus changes only when the surface opens. A binding to expanded
        // would reassert focus on each controller notification while typing.
        focus: true
        Keys.onPressed: function(event) {
            const shelf = window.shelf;
            const ctrl = !!(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier));
            if (event.key === Qt.Key_Escape) {
                if (shelf.picking) shelf.cancelPick(); else if (shelf.filter.length) shelf.filter = ""; else window.closePanel();
            } else if (ctrl && event.key === Qt.Key_A) shelf.selectAll();
            else if (ctrl && event.key === Qt.Key_C) shelf.copySelected("content");
            else if (ctrl && event.key === Qt.Key_V) shelf.paste();
            else if (ctrl && event.key === Qt.Key_Z) shelf.undo();
            else if (ctrl && event.key === Qt.Key_F) searchField.forceActiveFocus();
            else if (ctrl && event.key === Qt.Key_Comma) shelf.showSettings();
            else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) shelf.removeSelected();
            else if (event.key === Qt.Key_Left) window.navigate(-1, event.modifiers);
            else if (event.key === Qt.Key_Right) window.navigate(1, event.modifiers);
            else if (event.key === Qt.Key_Up) window.navigate(-grid.columns, event.modifiers);
            else if (event.key === Qt.Key_Down) window.navigate(grid.columns, event.modifiers);
            else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && shelf.picking) shelf.confirmPick();
            else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && window.cursorIndex >= 0) shelf.openItem(shelf.items[window.cursorIndex].id);
            else if (event.key === Qt.Key_Space && window.selectedCount && window.lookPreview) window.runOffer(window.lookPreview);
            else { event.accepted = false; return; }
            event.accepted = true;
        }

        // The kit's shadow-pop, inside the window's margin. A compositor that
        // rounds the whole window (Hyprland) draws its own, and a docked tab
        // sits flat on the screen edge.
        Item {
            x: shell.x; y: shell.y; width: shell.width; height: shell.height
            visible: shell.anchors.margins > 0
            Shadow { radius: shell.radius }
        }

        // One native toplevel holds both views: a layer-shell surface cannot
        // initiate ordinary file-manager drag transfers reliably on Wayland.
        Rectangle {
            id: shell
            function updateBackdrop() {
                window.shelf.setBackdropRegion(Qt.rect(x, y, width, height), radius);
            }
            Component.onCompleted: updateBackdrop()
            onXChanged: updateBackdrop()
            onYChanged: updateBackdrop()
            onWidthChanged: updateBackdrop()
            onHeightChanged: updateBackdrop()
            onRadiusChanged: updateBackdrop()
            anchors.fill: parent
            // A compositor that rounds and blurs the whole window (Hyprland)
            // gets a surface that fills it, or its blur shows square margins.
            anchors.margins: window.dockEdge.length || window.shelf.compositorRadius >= 0 ? 0 : 6
            radius: window.dockEdge.length ? window.tabThickness / 2 : window.shelf.compositorRadius >= 0 ? window.shelf.compositorRadius : Theme.radiusLg
            // The tab's edge side sits flat against the screen edge.
            topLeftRadius: window.dockEdge === "left" || window.dockEdge === "top" ? 0 : radius
            topRightRadius: window.dockEdge === "right" || window.dockEdge === "top" ? 0 : radius
            bottomLeftRadius: window.dockEdge === "left" || window.dockEdge === "bottom" ? 0 : radius
            bottomRightRadius: window.dockEdge === "right" || window.dockEdge === "bottom" ? 0 : radius
            color: window.paper
            border.width: 1
            border.color: window.dragHover || window.pulsing ? Theme.accent : Theme.line
            clip: true
            Behavior on border.color { ColorAnimation { duration: Theme.durationBase } }
            HoverHandler {
                id: surfaceHover
                onHoveredChanged: {
                    if (!hovered && window.shelf.expanded && !window.shelf.focusPanel && !window.dragHover) peekDismissTimer.restart();
                    else peekDismissTimer.stop();
                }
            }

            // The drag-hover pulse: accent-soft over the whole surface.
            Rectangle {
                anchors.fill: parent
                color: Theme.accentSoft
                opacity: window.dragHover ? 1 : window.pulsing ? 0.7 : 0
                visible: opacity > 0
                radius: parent.radius
                topLeftRadius: parent.topLeftRadius; topRightRadius: parent.topRightRadius
                bottomLeftRadius: parent.bottomLeftRadius; bottomRightRadius: parent.bottomRightRadius
                Behavior on opacity { NumberAnimation { duration: Theme.durationBase } }
            }

            Item {
                id: capsule
                objectName: "capsule"
                anchors.fill: parent
                visible: !window.shelf.expanded
                MouseArea {
                    id: capsuleMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    property real startX: 0
                    property real startY: 0
                    property bool moved: false
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onEntered: if (window.dockEdge.length && !window.shelf.expanded) edgeHoverTimer.restart()
                    onExited: edgeHoverTimer.stop()
                    onPressed: function(event) { edgeHoverTimer.stop(); startX = event.x; startY = event.y; moved = false; }
                    onPositionChanged: function(event) {
                        if (pressed && !moved && Math.abs(event.x - startX) + Math.abs(event.y - startY) > 7) {
                            moved = true; window.shelf.nativeMove();
                        }
                    }
                    onClicked: function(event) {
                        if (event.button === Qt.RightButton) (capsuleMenuLoader.get() as Menu).popup();
                        else if (!moved) window.shelf.toggle();
                    }
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                }
                ToolTip {
                    parent: capsule
                    popupType: T.Popup.Window
                    visible: capsuleMouse.containsMouse && window.shelf.message.length > 0
                    text: window.shelf.message
                    y: -height - Theme.space8
                }
                // Capsule: the Shelf mark, the collection and what it holds.
                RowLayout {
                    visible: !window.dockEdge.length
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space12
                    anchors.rightMargin: Theme.space12
                    spacing: Theme.space10
                    Rectangle {
                        Layout.preferredWidth: Theme.tile; Layout.preferredHeight: Theme.tile
                        radius: Theme.radiusTile
                        color: Theme.accentSoft
                        Glyph { anchors.centerIn: parent; size: 20; color: Theme.accentInk; visible: !window.dragHover }
                        Icon { anchors.centerIn: parent; name: "plus"; size: 20; color: Theme.accentInk; visible: window.dragHover }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            text: window.dragHover ? "Drop to collect" : window.shelf.activeName
                            kind: "body"; medium: true
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: window.shelf.count ? "Click to open" : "Drop files, text or links"
                            kind: "small"; color: Theme.muted
                            elide: Text.ElideRight
                        }
                    }
                    Badge {
                        visible: window.shelf.count > 0 && !window.dragHover
                        text: Format.formatCount(window.shelf.count)
                        tone: "accent"
                    }
                }
                // Docked tab: a chevron toward the screen and an accent dot
                // while the shelf holds something.
                Item {
                    visible: window.dockEdge.length > 0
                    anchors.fill: parent
                    Icon {
                        anchors.centerIn: parent
                        size: window.dragHover ? 16 : 18
                        name: window.dragHover ? "plus" : "chevronRight"
                        // Point back toward the screen, the way the panel opens.
                        rotation: window.dragHover ? 0 : window.dockEdge === "right" ? 180 : window.dockEdge === "top" ? 90 : window.dockEdge === "bottom" ? -90 : 0
                        color: window.dragHover ? Theme.accentInk : capsuleMouse.containsMouse ? Theme.text : Theme.muted
                    }
                    Dot {
                        visible: window.shelf.count > 0 && !window.dragHover
                        tone: "accent"
                        x: window.verticalDock ? (parent.width - width) / 2 : parent.width - 14
                        y: window.verticalDock ? parent.height - 14 : (parent.height - height) / 2
                    }
                }
            }

            Item {
                id: expanded
                anchors.fill: parent
                visible: window.shelf.expanded
                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.durationBase } }

                // Header: collection switcher, count, filter, then actions.
                Item {
                    id: header
                    x: Theme.space12; y: Theme.space10
                    width: parent.width - Theme.space24; height: Theme.control
                    MouseArea {
                        anchors.fill: parent
                        onPressed: window.shelf.nativeMove()
                        cursorShape: Qt.OpenHandCursor
                    }
                    RowLayout {
                        anchors.fill: parent
                        spacing: Theme.space6
                        Button {
                            id: collectionButton
                            objectName: "collectionButton"
                            // Menu buttons: the chevron follows the label.
                            LayoutMirroring.enabled: true; LayoutMirroring.childrenInherit: true
                            small: true
                            variant: "ghost"
                            text: window.shelf.picking ? "Choose from " + window.shelf.activeName : window.shelf.activeName
                            iconName: "chevronDown"
                            tooltip: "Collections"
                            Layout.maximumWidth: Math.max(120, header.width * 0.34)
                            onClicked: (collectionMenuLoader.get() as Menu).popup(collectionButton, 0, collectionButton.height + Theme.space4)
                        }
                        Badge { text: Format.formatCount(window.shelf.count) }
                        Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
                        SearchField {
                            id: searchField
                            objectName: "filterField"
                            Layout.preferredWidth: Math.max(120, Math.min(220, header.width * 0.34))
                            Layout.preferredHeight: Theme.controlSm
                            placeholderText: "Filter items"
                            text: window.shelf.filter
                            onEdited: text => window.shelf.filter = text
                            onEscaped: interaction.forceActiveFocus()
                            Keys.onDownPressed: { interaction.forceActiveFocus(); window.navigate(1, 0); }
                        }
                        Button {
                            visible: window.shelf.undoAvailable && !window.shelf.busy && !window.shelf.picking
                            small: true; variant: "ghost"
                            text: "Undo"; iconName: "undo"
                            tooltip: "Undo · Ctrl+Z"
                            onClicked: window.shelf.undo()
                        }
                        Button {
                            visible: window.shelf.busy
                            small: true
                            text: "Cancel"
                            onClicked: window.shelf.cancelJob()
                        }
                        Button {
                            id: actionsButton
                            objectName: "actionsButton"
                            LayoutMirroring.enabled: true; LayoutMirroring.childrenInherit: true
                            visible: !window.shelf.picking
                            enabled: window.selectedCount > 0
                            small: true
                            text: "Actions"; iconName: "chevronDown"
                            onClicked: (actionsMenuLoader.get() as Menu).popup(actionsButton, 0, actionsButton.height + Theme.space4)
                        }
                        Button {
                            id: moreButton
                            small: true; variant: "ghost"
                            iconName: "more"; tooltip: "Shelf options"
                            onClicked: (surfaceMenuLoader.get() as Menu).popup(moreButton, 0, moreButton.height + Theme.space4)
                        }
                        Button {
                            small: true; variant: "ghost"
                            iconName: "minimize"
                            tooltip: window.shelf.picking ? "Cancel · Esc" : "Close · Esc"
                            onClicked: window.shelf.picking ? window.shelf.cancelPick() : window.closePanel()
                        }
                    }
                }
                ProgressBar {
                    objectName: "busyProgress"
                    visible: window.shelf.busy
                    x: header.x; width: header.width
                    y: header.y + header.height + Theme.space6
                    from: 0; to: 1
                    indeterminate: window.shelf.progress < 0
                    value: Math.max(0, Math.min(1, window.shelf.progress))
                }
                Divider { x: 0; width: parent.width; y: header.y + header.height + Theme.space12 }

                GridView {
                    id: grid
                    objectName: "shelfGrid"
                    readonly property int columns: Math.max(2, Math.floor(width / 142))
                    x: Theme.space8; y: header.y + header.height + Theme.space16
                    width: Math.max(0, parent.width - Theme.space16)
                    height: Math.max(0, parent.height - y - footer.height)
                    clip: true
                    model: window.shelf.expanded ? window.shelf.items : []
                    cellWidth: width / columns
                    cellHeight: 152
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Item {
                        id: tile
                        required property var modelData
                        required property int index
                        objectName: "tile-" + modelData.id
                        width: grid.cellWidth; height: grid.cellHeight
                        readonly property bool chosen: { const revision = window.shelf.selectionRevision; return window.shelf.selected(modelData.id); }
                        readonly property bool missing: modelData.available === false
                        readonly property bool isText: modelData.type === "text/plain" || modelData.type === "text/rich"
                        Accessible.role: Accessible.ListItem
                        Accessible.name: modelData.name + ", " + window.subtitleFor(modelData)
                        Accessible.selected: chosen
                        Accessible.onPressAction: window.shelf.select(modelData.id, false, false)
                        Rectangle {
                            id: card
                            x: Theme.space4; y: Theme.space4
                            width: parent.width - Theme.space8; height: parent.height - Theme.space8
                            radius: Theme.radiusMd
                            color: tile.chosen ? Theme.accentSoft : tileMouse.containsMouse ? Theme.hover : Theme.sunken
                            border.width: tile.chosen ? 2 : 0
                            border.color: Theme.accent
                            Item {
                                id: preview
                                x: Theme.space8; y: Theme.space8
                                width: parent.width - Theme.space16; height: 82
                                clip: true
                                Image {
                                    id: thumbnail
                                    anchors.fill: parent
                                    source: window.shelf.expanded && !tile.missing && tile.modelData.thumbnail ? tile.modelData.thumbnail : ""
                                    asynchronous: true
                                    cache: true
                                    fillMode: Image.PreserveAspectCrop
                                    sourceSize.width: 256; sourceSize.height: 180
                                    visible: status === Image.Ready
                                }
                                Icon {
                                    visible: thumbnail.status !== Image.Ready && !tile.isText && tile.modelData.type !== "text/url"
                                    anchors.centerIn: parent; size: 32
                                    name: window.iconFor(tile.modelData.type)
                                    color: tile.missing ? Theme.faint : Theme.muted
                                }
                                Label {
                                    objectName: "previewText-" + tile.modelData.id
                                    visible: tile.isText
                                    anchors.fill: parent; anchors.margins: Theme.space4
                                    text: tile.modelData.text || "Text"
                                    textFormat: Text.PlainText
                                    kind: "caption"; color: Theme.muted
                                    wrapMode: Text.Wrap; maximumLineCount: 4; elide: Text.ElideRight
                                }
                                Column {
                                    visible: tile.modelData.type === "text/url"
                                    anchors.centerIn: parent; spacing: Theme.space4; width: parent.width - Theme.space12
                                    Icon { name: "link"; color: Theme.accentInk; size: 24; anchors.horizontalCenter: parent.horizontalCenter }
                                    Label {
                                        width: parent.width
                                        text: (tile.modelData.text || "").replace(/^https?:\/\//, "").split("/")[0]
                                        kind: "caption"; color: Theme.muted
                                        elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter
                                    }
                                }
                            }
                            Label {
                                id: nameLabel
                                x: Theme.space10; y: 98; width: parent.width - Theme.space20
                                text: tile.modelData.name
                                kind: "body"; medium: true
                                elide: Text.ElideMiddle; maximumLineCount: 1
                            }
                            Label {
                                x: Theme.space10; y: 118; width: parent.width - Theme.space20
                                text: window.subtitleFor(tile.modelData)
                                kind: "caption"; color: tile.missing ? Theme.dangerInk : Theme.muted
                                elide: Text.ElideRight; maximumLineCount: 1
                            }
                            Rectangle {
                                visible: tile.chosen
                                x: parent.width - 24; y: Theme.space8; width: 16; height: 16; radius: 8; color: Theme.accent
                                Icon { anchors.centerIn: parent; name: "check"; color: Theme.accentText; size: 11 }
                            }
                            Icon { visible: tile.missing; x: Theme.space8; y: Theme.space8; name: "alert"; color: Theme.dangerInk; size: 14 }
                            MouseArea {
                                id: tileMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                property real pressX: 0; property real pressY: 0
                                property bool startedDrag: false
                                onPressed: function(event) {
                                    pressX = event.x; pressY = event.y; startedDrag = false; window.cursorIndex = tile.index;
                                    if (event.button === Qt.RightButton) {
                                        if (!tile.chosen) window.shelf.select(tile.modelData.id, false, false);
                                        window.contextItem = tile.modelData.id; (itemMenuLoader.get() as Menu).popup();
                                    } else if (!tile.chosen || (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.ShiftModifier)))
                                        window.shelf.select(tile.modelData.id, !!(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)), !!(event.modifiers & Qt.ShiftModifier));
                                    interaction.forceActiveFocus();
                                }
                                onClicked: function(event) {
                                    if (event.button === Qt.LeftButton && !startedDrag && !(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.ShiftModifier)))
                                        window.shelf.select(tile.modelData.id, false, false);
                                }
                                onPositionChanged: function(event) {
                                    if (pressed && (pressedButtons & Qt.LeftButton) && !startedDrag && !window.shelf.picking && Math.abs(event.x - pressX) + Math.abs(event.y - pressY) > 9) {
                                        startedDrag = true; window.shelf.dragSelected();
                                    }
                                }
                                onDoubleClicked: if (!startedDrag && !window.shelf.picking) window.shelf.openItem(tile.modelData.id)
                                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                            }
                            // The full name, where the label had to shorten it.
                            ToolTip {
                                visible: tileMouse.containsMouse && !tileMouse.pressed && nameLabel.truncated
                                text: tile.modelData.name
                            }
                        }
                    }
                }

                EmptyState {
                    id: emptyState
                    objectName: "emptyState"
                    visible: window.shelf.expanded && window.shelf.items.length === 0
                    dashed: false
                    width: Math.min(420, parent.width - Theme.space48)
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: grid.y + Math.max(0, (grid.height - height) / 2)
                    iconName: window.shelf.filter.length ? "search" : "plus"
                    title: window.shelf.filter.length ? "Nothing matches \u201c" + window.shelf.filter + "\u201d" : window.shelf.picking ? "This collection is empty" : "Drop to collect"
                    text: window.shelf.filter.length ? "Try a different filter." : window.shelf.picking ? "Choose another collection to continue."
                        : "Keep the originals right where they are. Paste (Ctrl+V) or drop files, text and links."
                    Repeater {
                        model: !window.shelf.filter.length && !window.shelf.picking && grid.height > 220 && window.shelf.expanded ? window.shelf.sources : []
                        delegate: Button {
                            required property var modelData
                            small: true
                            text: modelData.title
                            onClicked: window.shelf.runSource(modelData.app, modelData.action || modelData.id)
                        }
                    }
                }

                // Footer: the hint bar, or the picker's choice.
                Item {
                    id: footer
                    anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
                    height: window.shelf.picking ? 52 : Theme.hintBar + 1
                    Divider { width: parent.width }
                    HintBar {
                        objectName: "hintBar"
                        visible: !window.shelf.picking
                        y: 1; width: parent.width
                        hints: [{ keys: ["↵"], label: "Open" }, { keys: ["Space"], label: "Preview in Look" },
                                { keys: ["Del"], label: "Remove" }, { keys: ["Esc"], label: "Close" }]
                        trailing: window.shelf.message.length ? window.shelf.message
                            : window.selectedCount ? Format.formatCount(window.selectedCount) + " selected" : ""
                    }
                    RowLayout {
                        visible: window.shelf.picking
                        anchors.fill: parent
                        anchors.leftMargin: Theme.space12; anchors.rightMargin: Theme.space12
                        spacing: Theme.space8
                        Label {
                            Layout.fillWidth: true
                            text: window.shelf.pickMultiple ? "Select one or more items." : "Select an item."
                            kind: "small"; color: Theme.muted; elide: Text.ElideRight
                        }
                        Button { text: "Cancel"; onClicked: window.shelf.cancelPick() }
                        Button {
                            objectName: "pickerConfirm"
                            variant: "primary"
                            text: window.selectedCount ? "Use " + window.countLabel(window.selectedCount) : "Choose items"
                            enabled: window.selectedCount > 0
                            onClicked: window.shelf.confirmPick()
                        }
                    }
                }
            }

            // External MIME drops are accepted here, never converted to an
            // in-process Drag token. Qt's native data device remains in charge.
            DropArea {
                id: dropArea
                objectName: "shelfDropArea"
                anchors.fill: parent
                enabled: !window.shelf.picking
                onEntered: function(drag) {
                    drag.accepted = drag.hasUrls || drag.hasText || drag.hasHtml;
                    peekDismissTimer.stop();
                    if (drag.accepted && window.dockEdge.length && !window.shelf.expanded) edgeHoverTimer.restart();
                }
                onExited: {
                    edgeHoverTimer.stop();
                    if (window.shelf.expanded && !window.shelf.focusPanel && !surfaceHover.hovered && !window.shelf.nativeDragHover) peekDismissTimer.restart();
                }
                onDropped: function(drop) {
                    edgeHoverTimer.stop();
                    if (!(drop.hasUrls || drop.hasText || drop.hasHtml)) return;
                    window.shelf.acceptDrop(drop.urls, drop.hasText ? drop.text : "", drop.hasHtml ? drop.html : "");
                    drop.acceptProposedAction();
                }
            }
            // The panel's drop target while something is dragged over it.
            Rectangle {
                visible: window.shelf.expanded && window.dragHover
                anchors.fill: parent; anchors.margins: Theme.space8
                radius: Theme.radiusLg
                color: Theme.accentSoft
                border.color: Theme.accent; border.width: 2
                Column {
                    anchors.centerIn: parent; spacing: Theme.space8
                    Icon { name: "plus"; color: Theme.accentInk; size: 32; anchors.horizontalCenter: parent.horizontalCenter }
                    Label { text: "Drop to collect"; kind: "titleSm"; anchors.horizontalCenter: parent.horizontalCenter }
                    Label { text: "Keep the originals right where they are."; color: Theme.muted; anchors.horizontalCenter: parent.horizontalCenter }
                }
            }
        }

        // Resize strips stay outside the content hit region.
        MouseArea { visible: window.shelf.expanded; x: 0; y: 12; width: 6; height: parent.height - 24; cursorShape: Qt.SizeHorCursor; onPressed: window.shelf.nativeResize(Qt.LeftEdge) }
        MouseArea { visible: window.shelf.expanded; x: parent.width - 6; y: 12; width: 6; height: parent.height - 24; cursorShape: Qt.SizeHorCursor; onPressed: window.shelf.nativeResize(Qt.RightEdge) }
        MouseArea { visible: window.shelf.expanded; x: 12; y: 0; width: parent.width - 24; height: 6; cursorShape: Qt.SizeVerCursor; onPressed: window.shelf.nativeResize(Qt.TopEdge) }
        MouseArea { visible: window.shelf.expanded; x: 12; y: parent.height - 6; width: parent.width - 24; height: 6; cursorShape: Qt.SizeVerCursor; onPressed: window.shelf.nativeResize(Qt.BottomEdge) }
        MouseArea { visible: window.shelf.expanded; x: parent.width - 14; y: parent.height - 14; width: 14; height: 14; cursorShape: Qt.SizeFDiagCursor; onPressed: window.shelf.nativeResize(Qt.RightEdge | Qt.BottomEdge) }
    }

    // Capsule and tab menu (right click).
    Lazy {
        id: capsuleMenuLoader
        sourceComponent: Component {
            Menu {
                id: capsuleMenu
                MenuItem { text: "Open " + window.shelf.activeName; onTriggered: window.shelf.show() }
                MenuItem { text: "Collect from clipboard"; iconName: "clipboard"; onTriggered: window.shelf.paste() }
                MenuItem { text: "Undo"; iconName: "undo"; shortcut: "Ctrl+Z"; enabled: window.shelf.undoAvailable; onTriggered: window.shelf.undo() }
                MenuSeparator {}
                DockMenu { shelf: window.shelf; onDockRequested: edge => window.dockTo(edge) }
                MenuSeparator {}
                MenuItem { text: "Settings"; iconName: "settings"; shortcut: "Ctrl+,"; onTriggered: window.shelf.showSettings() }
            }
        }
    }
    // The panel's window menu (⋯).
    Lazy {
        id: surfaceMenuLoader
        sourceComponent: Component {
            Menu {
                id: surfaceMenu
                MenuItem { text: "Collect from clipboard"; iconName: "clipboard"; shortcut: "Ctrl+V"; onTriggered: window.shelf.paste() }
                MenuItem { text: "Select all"; shortcut: "Ctrl+A"; enabled: window.shelf.items.length > 0; onTriggered: window.shelf.selectAll() }
                MenuSeparator {}
                DockMenu { shelf: window.shelf; onDockRequested: edge => window.dockTo(edge) }
                MenuItem { text: "Always on top"; iconName: "pin"; checkable: true; checked: window.shelf.alwaysOnTop; onTriggered: window.shelf.alwaysOnTop = !window.shelf.alwaysOnTop }
                MenuSeparator {}
                MenuItem { text: "Settings"; iconName: "settings"; shortcut: "Ctrl+,"; onTriggered: window.shelf.showSettings() }
            }
        }
    }
    // Collections: switch, then manage the open one.
    Lazy {
        id: collectionMenuLoader
        sourceComponent: Component {
            Menu {
                id: collectionMenu
                // Filled just before it opens, so it has its final size when shown.
                property bool populated: false
                onAboutToShow: populated = true
                onClosed: populated = false
                Instantiator {
                    model: collectionMenu.populated ? window.shelf.shelves : []
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.name + "  ·  " + Format.formatCount(modelData.count || 0)
                        iconName: modelData.pinned ? "pin" : ""
                        checkable: true
                        checked: modelData.id === window.shelf.activeShelf
                        onTriggered: window.shelf.switchShelf(modelData.id)
                    }
                    onObjectAdded: (index, object) => collectionMenu.insertItem(index, object as MenuItem)
                    onObjectRemoved: (index, object) => collectionMenu.removeItem(object as MenuItem)
                }
                MenuSeparator {}
                MenuItem { text: "New collection…"; iconName: "plus"; onTriggered: window.editCollection("", "", "new") }
                MenuItem { text: "Rename collection…"; iconName: "edit"; onTriggered: window.editCollection(window.shelf.activeShelf, window.shelf.activeName, "rename") }
                MenuItem {
                    readonly property bool pinned: window.shelf.shelves.some(s => s.id === window.shelf.activeShelf && s.pinned)
                    text: pinned ? "Unpin collection" : "Pin collection"
                    iconName: "pin"
                    onTriggered: window.shelf.pinShelf(window.shelf.activeShelf, !pinned)
                }
                MenuItem { text: "Move up"; onTriggered: window.shelf.moveShelf(window.shelf.activeShelf, -1) }
                MenuItem { text: "Move down"; onTriggered: window.shelf.moveShelf(window.shelf.activeShelf, 1) }
                MenuSeparator {}
                MenuItem {
                    text: "Delete collection…"; iconName: "trash"; danger: true
                    enabled: window.shelf.shelves.length > 1
                    onTriggered: { window.collectionToEdit = window.shelf.activeShelf; (deleteCollectionLoader.get() as Dialog).open(); }
                }
            }
        }
    }
    // One item (right click), acting on the selection.
    Lazy {
        id: itemMenuLoader
        sourceComponent: Component {
            Menu {
                id: itemMenu
                MenuItem { text: "Open"; shortcut: "Enter"; onTriggered: window.shelf.openItem(window.contextItem) }
                MenuItem { text: "Show in folder"; iconName: "showInFolder"; onTriggered: window.shelf.revealItem(window.contextItem) }
                MenuItem { text: "Preview in Look"; iconName: "eye"; shortcut: "Space"; visible: !!window.lookPreview; height: visible ? implicitHeight : 0; onTriggered: window.runOffer(window.lookPreview) }
                MenuSeparator {}
                MenuItem { text: "Copy"; iconName: "copy"; shortcut: "Ctrl+C"; onTriggered: window.shelf.copySelected("content") }
                MenuItem { text: "Copy path"; onTriggered: window.shelf.copySelected("paths") }
                MenuSeparator {}
                MenuItem { text: "Move earlier"; onTriggered: window.moveItem(-1) }
                MenuItem { text: "Move later"; onTriggered: window.moveItem(1) }
                MenuSeparator {}
                MenuItem { text: "Remove from Shelf"; iconName: "trash"; shortcut: "Del"; danger: true; onTriggered: window.shelf.removeSelected() }
            }
        }
    }
    // Actions (header): the selection, then what other Arcade apps offer.
    Lazy {
        id: actionsMenuLoader
        sourceComponent: Component {
            Menu {
                id: actionsMenu
                property bool populated: false
                onAboutToShow: populated = true
                onClosed: populated = false
                MenuItem { text: "Copy"; iconName: "copy"; shortcut: "Ctrl+C"; onTriggered: window.shelf.copySelected("content") }
                MenuItem { text: "Copy path"; onTriggered: window.shelf.copySelected("paths") }
                MenuSeparator { visible: window.shelf.offers.length > 0 }
                Instantiator {
                    model: actionsMenu.populated ? window.shelf.offers : []
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.title
                        enabled: modelData.enabled !== false && !window.shelf.busy
                        onTriggered: window.runOffer(modelData)
                    }
                    onObjectAdded: (index, object) => actionsMenu.insertItem(3 + index, object as MenuItem)
                    onObjectRemoved: (index, object) => actionsMenu.removeItem(object as MenuItem)
                }
                MenuSeparator {}
                MenuItem { text: "Remove from Shelf"; iconName: "trash"; shortcut: "Del"; danger: true; onTriggered: window.shelf.removeSelected() }
            }
        }
    }

    Lazy {
        id: collectionDialogLoader
        sourceComponent: Component {
            Dialog {
                id: collectionDialog
                title: window.collectionMode === "new" ? "New collection" : "Rename collection"
                onAboutToShow: { collectionName.text = window.collectionDraft; temporaryToggle.checked = false; }
                onOpened: collectionName.forceActiveFocus()
                Column {
                    width: parent.width
                    spacing: Theme.space12
                    TextField {
                        id: collectionName
                        width: parent.width
                        placeholderText: "Collection name"
                        maximumLength: 80
                        onAccepted: if (text.trim().length) collectionSave.clicked()
                    }
                    CheckBox {
                        id: temporaryToggle
                        visible: window.collectionMode === "new"
                        text: "Temporary: unpinned items follow your retention setting"
                    }
                }
                actions: [
                    Button { text: "Cancel"; onClicked: collectionDialog.close() },
                    Button {
                        id: collectionSave
                        variant: "primary"
                        text: window.collectionMode === "new" ? "Create" : "Save"
                        enabled: collectionName.text.trim().length > 0
                        onClicked: {
                            if (window.collectionMode === "new") window.shelf.createShelf(collectionName.text.trim(), temporaryToggle.checked);
                            else window.shelf.renameShelf(window.collectionToEdit, collectionName.text.trim());
                            collectionDialog.close();
                        }
                    }
                ]
            }
        }
    }
    Lazy {
        id: deleteCollectionLoader
        sourceComponent: Component {
            Dialog {
                id: deleteCollection
                title: "Delete this collection?"
                body: "Its items leave Shelf. The original files stay where they are."
                destructive: true
                actions: [
                    Button { text: "Keep"; onClicked: deleteCollection.close() },
                    Button {
                        variant: "destructive"
                        text: "Delete collection"
                        onClicked: { window.shelf.deleteShelf(window.collectionToEdit); deleteCollection.close(); }
                    }
                ]
            }
        }
    }
    Lazy {
        id: confirmActionLoader
        sourceComponent: Component {
            Dialog {
                id: confirmAction
                title: window.pendingOffer.title || "Send selection"
                body: (window.selectedCount === 1 ? "1 selected item goes to " : Format.formatCount(window.selectedCount) + " selected items go to ")
                    + (window.pendingOffer.appName || window.pendingOffer.app || "this app") + ". This action:"
                Column {
                    width: parent.width
                    spacing: Theme.space4
                    Repeater {
                        model: window.pendingOffer.effects || []
                        delegate: Row {
                            id: effectRow
                            required property string modelData
                            spacing: Theme.space8
                            Dot { anchors.verticalCenter: parent.verticalCenter; tone: "warn" }
                            Label {
                                kind: "small"
                                text: ({ "network": "Uses the network", "uploads-content": "Uploads the content", "sends-to-device": "Sends it to another device",
                                         "persists": "Keeps a copy", "clipboard": "Changes the clipboard", "writes-files": "Writes files",
                                         "overwrites-files": "Can overwrite files", "deletes-files": "Can delete files" })[effectRow.modelData] || effectRow.modelData
                            }
                        }
                    }
                }
                actions: [
                    Button { text: "Cancel"; onClicked: confirmAction.close() },
                    Button {
                        variant: "primary"
                        text: "Continue"
                        onClicked: { const offer = window.pendingOffer; confirmAction.close(); window.shelf.runPeer(offer.app, offer.action || offer.id); }
                    }
                ]
            }
        }
    }
    Lazy {
        id: analysisDialogLoader
        sourceComponent: Component {
            Dialog {
                id: analysisDialog
                title: window.shelf.analysisResult.title || "Inspection results"
                body: window.shelf.analysisResult.message || ""
                onClosed: window.shelf.dismissAnalysis()
                Flickable {
                    width: parent.width
                    height: Math.min(260, analysisText.implicitHeight + Theme.space16)
                    contentHeight: analysisText.implicitHeight + Theme.space16
                    clip: true
                    ScrollBar.vertical: ScrollBar {}
                    Rectangle { anchors.fill: parent; radius: Theme.radiusMd; color: Theme.sunken; z: -1 }
                    TextEdit {
                        id: analysisText
                        x: Theme.space8; y: Theme.space8
                        width: parent.width - Theme.space16
                        readOnly: true; selectByMouse: true
                        text: window.shelf.analysisResult.formatted || ""
                        textFormat: TextEdit.PlainText
                        color: Theme.text; selectionColor: Theme.accent; selectedTextColor: Theme.accentText
                        wrapMode: TextEdit.Wrap
                        font.family: Theme.fontMono; font.pixelSize: Theme.monoSize
                    }
                }
                actions: [Button { variant: "primary"; text: "Done"; onClicked: analysisDialog.close() }]
            }
        }
    }

    // Settings: its own normal window, created while open.
    Loader {
        id: settingsLoader
        active: window.shelf.settingsOpen
        sourceComponent: SettingsWindow { shelf: window.shelf }
    }
    Connections {
        target: window.shelf
        function onSettingsRaiseRequested() {
            const settings = settingsLoader.item as SettingsWindow;
            if (settings) { settings.raise(); settings.requestActivate(); }
        }
    }
}
