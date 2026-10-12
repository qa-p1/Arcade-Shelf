pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: window
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
    property string dockEdge: shelf.dockEdge
    readonly property bool verticalDock: dockEdge === "left" || dockEdge === "right"
    SystemPalette { id: systemPalette; colorGroup: SystemPalette.Active }
    readonly property bool dark: shelf.theme === "dark" || (shelf.theme === "system" && systemPalette.window.hslLightness < 0.5)
    readonly property color paper: shelf.backdrop.active ? (dark ? "#de181720" : "#def8f7fb") : (dark ? "#f5181720" : "#f5f8f7fb")
    readonly property color raised: dark ? "#25222e" : "#ffffff"
    readonly property color tileSurface: dark ? "#211e29" : "#eeedf3"
    readonly property color hoverSurface: dark ? "#302b3c" : "#e9e5f4"
    readonly property color ink: dark ? "#eeeaf5" : "#292432"
    readonly property color muted: dark ? "#a49bb3" : "#746a80"
    readonly property color faint: dark ? "#756d86" : "#978e9f"
    readonly property color outline: dark ? "#3a3347" : "#dcd6e6"
    readonly property color accent: dark ? "#bdadfc" : "#7853ca"
    readonly property color selection: dark ? "#392c51" : "#e7ddfa"
    readonly property int motion: shelf.reducedMotion ? 0 : 180
    property int cursorIndex: -1
    property string contextItem: ""
    property string collectionToEdit: ""
    property string collectionMode: "new"
    property var pendingOffer: ({})
    property int settingsPage: 0
    property bool restoring: true
    property bool resizing: false
    property bool dragHover: dropArea.containsDrag || shelf.nativeDragHover
    property bool previousFocusPanel: shelf.focusPanel
    property bool pulsing: false
    readonly property int selectedCount: shelf.selectedCount

    function iconFor(type) {
        if (type === "folder/reference") return "folder";
        if (type === "text/url") return "url";
        if (type.indexOf("text/") === 0) return "text";
        if (type.indexOf("file/image") === 0) return "image";
        if (type.indexOf("file/video") === 0) return "video";
        if (type.indexOf("file/audio") === 0) return "audio";
        return "file";
    }
    function subtitleFor(item) {
        if (item.available === false) return "Original unavailable";
        if (item.type === "folder/reference") return "Folder";
        // One line only: a snippet's full text belongs to the preview above.
        const firstLine = (item.text || "").split("\n").map(line => line.trim()).find(line => line.length) || "";
        if (item.type === "text/url") return firstLine || "Link";
        if (item.type.indexOf("text/") === 0) return firstLine || "Text";
        let size = Number(item.size || 0);
        const kind = item.type.replace("file/", "");
        return size < 1024 ? kind + " · " + size + " B" : size < 1048576 ? kind + " · " + (size/1024).toFixed(0) + " KB" : kind + " · " + (size/1048576).toFixed(1) + " MB";
    }
    function navigate(delta, modifiers) {
        if (!shelf.items.length) return;
        cursorIndex = Math.max(0, Math.min(shelf.items.length-1, cursorIndex < 0 ? 0 : cursorIndex+delta));
        shelf.select(shelf.items[cursorIndex].id, !!(modifiers & Qt.ControlModifier), !!(modifiers & Qt.ShiftModifier));
        grid.positionViewAtIndex(cursorIndex, GridView.Contain);
    }
    function runOffer(offer) {
        let effects = offer.effects || [];
        if (effects.indexOf("network") >= 0 || effects.indexOf("uploads-content") >= 0 || effects.indexOf("sends-to-device") >= 0 || effects.indexOf("persists") >= 0 || effects.indexOf("clipboard") >= 0 || effects.indexOf("writes-files") >= 0 || effects.indexOf("overwrites-files") >= 0 || effects.indexOf("deletes-files") >= 0) {
            pendingOffer = offer;
            confirmAction.open();
        } else shelf.runPeer(offer.app, offer.action || offer.id);
    }
    function editCollection(id, name, mode) {
        collectionToEdit = id; collectionMode = mode; collectionName.text = name;
        collectionDialog.open();
        collectionName.forceActiveFocus();
    }
    // A deliberate close (Escape, the minus button) leaves the pointer on the
    // edge tab; without a pause, hovering would peek the panel straight back.
    property double peekHeldUntil: 0
    function closePanel() {
        peekHeldUntil = Date.now() + 900;
        edgeHoverTimer.stop();
        shelf.collapse();
    }
    function dockTo(edge) {
        shelf.collapse();
        Qt.callLater(function() { shelf.dock(edge); });
    }
    function moveItem(delta) {
        const rows = shelf.items; let index = -1;
        for(let i=0;i<rows.length;i++) if(rows[i].id === contextItem) index = i;
        if(index < 0 || index+delta < 0 || index+delta >= rows.length) return;
        const before = delta < 0 ? rows[index-1].id : index+2 < rows.length ? rows[index+2].id : "";
        shelf.reorderItem(contextItem, before);
    }
    function itemBeforeAt(px, py) {
        const point = grid.mapFromItem(interaction,px,py);
        const index = grid.indexAt(point.x+grid.contentX,point.y+grid.contentY);
        return index >= 0 && index < shelf.items.length ? shelf.items[index].id : "";
    }
    function savePosition() {
        if (!restoring && !resizing) shelf.saveGeometry(x, y, panelWidth, panelHeight);
    }

    onXChanged: geometryTimer.restart()
    onYChanged: geometryTimer.restart()
    onWidthChanged: if (!restoring && shelf.expanded) { panelWidth = width; geometryTimer.restart(); }
    onHeightChanged: if (!restoring && shelf.expanded) { panelHeight = height; geometryTimer.restart(); }
    onClosing: function(close) { close.accepted = false; shelf.collapse(); }
    onDockEdgeChanged: if (!dockEdge.length) edgeHoverTimer.stop()
    onDarkChanged: if (shelf.backdrop) shelf.backdrop.setDark(dark)
    Component.onCompleted: {
        const geometry = shelf.restoreGeometry();
        panelWidth = Math.max(440, Number(geometry.width || 640));
        panelHeight = Math.max(350, Number(geometry.height || 490));
        if (geometry.x !== undefined) x = Number(geometry.x);
        if (geometry.y !== undefined) y = Number(geometry.y);
        restoring = false;
        if (shelf.backdrop) shelf.backdrop.setDark(dark);
    }
    Timer { id: geometryTimer; interval: 250; onTriggered: window.savePosition() }
    Connections {
        target: shelf
        function onChanged() {
            const focusing = shelf.focusPanel && !window.previousFocusPanel;
            window.previousFocusPanel = shelf.focusPanel;
            if (focusing) {
                window.requestActivate(); interaction.forceActiveFocus();
            }
            if (shelf.focusPanel || !shelf.expanded) peekDismissTimer.stop();
            window.cursorIndex = Math.min(window.cursorIndex, shelf.items.length - 1);
            if (shelf.analysisResult && Object.keys(shelf.analysisResult).length > 0 && !analysisDialog.visible) analysisDialog.open();
        }
        function onPulse() { window.pulsing=true; pulseTimer.restart(); }
        function onNewCollectionRequested() {
            Qt.callLater(function() { window.editCollection("", "", "new"); });
        }
        function onNativeDragHoverChanged() {
            if (shelf.nativeDragHover) {
                peekDismissTimer.stop();
                if (window.dockEdge.length && !shelf.expanded) edgeHoverTimer.restart();
            } else {
                edgeHoverTimer.stop();
                if (shelf.expanded && !shelf.focusPanel && !surfaceHover.hovered && !dropArea.containsDrag) peekDismissTimer.restart();
            }
        }
    }
    Timer { id: pulseTimer; interval: shelf.reducedMotion ? 900 : 650; onTriggered: window.pulsing=false }
    Timer {
        id: edgeHoverTimer
        interval: 260
        onTriggered: if (window.dockEdge.length && !shelf.expanded && (capsuleMouse.containsMouse || window.dragHover)
                         && (window.dragHover || Date.now() >= window.peekHeldUntil)) shelf.peek()
    }
    Timer {
        id: peekDismissTimer
        interval: 320
        onTriggered: if (shelf.expanded && !shelf.focusPanel && !surfaceHover.hovered && !window.dragHover) shelf.collapse()
    }

    component IconButton: ShelfButton {
        foreground: window.ink; hoverSurface: window.hoverSurface; accentColor: window.accent
        reduceMotion: shelf.reducedMotion; compact: true
    }
    component Field: ShelfField {
        foreground: window.ink; mutedColor: window.muted; surface: window.tileSurface
        outline: window.outline; accentColor: window.accent
    }
    component ActionMenu: ShelfMenu {
        foreground: window.ink; mutedColor: window.muted; surface: window.raised
        hoverSurface: window.hoverSurface; outline: window.outline; accentColor: window.accent
    }
    component Modal: ShelfDialog {
        foreground: window.ink; mutedColor: window.muted; surface: window.raised
        outline: window.outline; accentColor: window.accent
    }
    component MenuEntry: MenuItem {
        id: entry
        implicitHeight: 34
        leftPadding: 11; rightPadding: 26
        contentItem: Text { text: entry.text; textFormat: Text.PlainText; color: entry.enabled ? window.ink : window.faint; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        background: Rectangle { radius: 7; color: entry.highlighted ? window.hoverSurface : "transparent" }
        indicator: ShelfIcon { visible: entry.checkable && entry.checked; name: "check"; width: 14; height: 14; color: window.accent; x: parent.width-22; anchors.verticalCenter: parent.verticalCenter }
    }
    component Label: Text { textFormat: Text.PlainText; color: window.ink; font.pixelSize: 12; font.family: "sans-serif" }
    component PreferenceToggle: Switch {
        id: control
        property string description: ""
        property string heading: ""
        implicitHeight: description.length ? 57 : 38
        leftPadding: 0; rightPadding: 52
        text: heading
        contentItem: Column {
            spacing: 5
            Label { text: control.heading; font.weight: Font.Medium }
            Label { text: control.description; color: window.muted; font.pixelSize: 11; wrapMode: Text.WordWrap; width: parent.width }
        }
        indicator: Rectangle {
            x: control.width - width; anchors.verticalCenter: parent.verticalCenter
            width: 34; height: 20; radius: 10
            color: control.checked ? window.accent : window.outline
            Rectangle {
                x: control.checked ? 17 : 3; y: 3; width: 14; height: 14; radius: 7
                color: control.checked && !window.dark ? "white" : control.checked ? "#20162f" : window.muted
                Behavior on x { NumberAnimation { duration: window.motion } }
            }
            border.width: control.activeFocus ? 2 : 0; border.color: window.ink
        }
    }
    component NumberInput: SpinBox {
        id: number
        implicitWidth: 118; implicitHeight: 34
        editable: true
        padding: 0; leftPadding: 31; rightPadding: 31
        font.pixelSize: 12
        contentItem: TextInput {
            text: number.value.toString()
            color: window.ink; selectionColor: window.accent
            horizontalAlignment: Qt.AlignHCenter; verticalAlignment: Qt.AlignVCenter
            readOnly: !number.editable
            validator: number.validator
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            font: number.font
        }
        background: Rectangle { color: window.tileSurface; radius: 8; border.color: number.activeFocus ? window.accent : window.outline }
        up.indicator: Rectangle {
            x: parent.width-width; width: 29; height: parent.height; radius: 8
            color: number.up.pressed ? window.hoverSurface : "transparent"
            ShelfIcon { anchors.centerIn: parent; name: "plus"; width: 14; height: 14; color: window.muted }
        }
        down.indicator: Rectangle {
            width: 29; height: parent.height; radius: 8
            color: number.down.pressed ? window.hoverSurface : "transparent"
            ShelfIcon { anchors.centerIn: parent; name: "minus"; width: 14; height: 14; color: window.muted }
        }
    }

    Item {
        id: interaction
        anchors.fill: parent
        // Focus changes only when the surface opens. A binding to expanded
        // would reassert focus on each controller notification while typing.
        focus: true
        Keys.onPressed: function(event) {
            if (shelf.settingsOpen) return;
            const ctrl = !!(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier));
            if (event.key === Qt.Key_Escape) {
                if (shelf.picking) shelf.cancelPick(); else if (shelf.filter.length) shelf.filter = ""; else window.closePanel();
            } else if (ctrl && event.key === Qt.Key_A) shelf.selectAll();
            else if (ctrl && event.key === Qt.Key_C) shelf.copySelected("content");
            else if (ctrl && event.key === Qt.Key_V) shelf.paste();
            else if (ctrl && event.key === Qt.Key_Z) shelf.undo();
            else if (ctrl && event.key === Qt.Key_F) searchField.forceActiveFocus();
            else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) shelf.removeSelected();
            else if (event.key === Qt.Key_Left) window.navigate(-1, event.modifiers);
            else if (event.key === Qt.Key_Right) window.navigate(1, event.modifiers);
            else if (event.key === Qt.Key_Up) window.navigate(-Math.max(1, Math.floor(grid.width/grid.cellWidth)), event.modifiers);
            else if (event.key === Qt.Key_Down) window.navigate(Math.max(1, Math.floor(grid.width/grid.cellWidth)), event.modifiers);
            else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && shelf.picking) shelf.confirmPick();
            else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && cursorIndex >= 0) shelf.openItem(shelf.items[cursorIndex].id);
            else if (event.key === Qt.Key_Space && window.selectedCount) {
                for (let i=0;i<shelf.offers.length;i++) if (shelf.offers[i].action === "look.preview" || shelf.offers[i].id === "look.preview") { window.runOffer(shelf.offers[i]); break; }
            } else { event.accepted = false; return; }
            event.accepted = true;
        }

        // One native toplevel holds both views: a layer-shell surface cannot
        // initiate ordinary file-manager drag transfers reliably on Wayland.
        Rectangle {
            id: shell
            function updateBackdrop() {
                if (!shelf.backdrop) return;
                shelf.backdrop.setRegion(Qt.rect(x,y,width,height),radius);
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
            anchors.margins: window.dockEdge.length || shelf.compositorRadius >= 0 ? 0 : 6
            radius: window.dockEdge.length ? window.tabThickness / 2 : shelf.compositorRadius >= 0 ? shelf.compositorRadius : shelf.expanded ? 19 : 24
            // The tab's edge side sits flat against the screen edge.
            topLeftRadius: window.dockEdge === "left" || window.dockEdge === "top" ? 0 : radius
            topRightRadius: window.dockEdge === "right" || window.dockEdge === "top" ? 0 : radius
            bottomLeftRadius: window.dockEdge === "left" || window.dockEdge === "bottom" ? 0 : radius
            bottomRightRadius: window.dockEdge === "right" || window.dockEdge === "bottom" ? 0 : radius
            color: window.dockEdge.length ? (window.dark ? "#2e2b36" : "#f2f1f6") : window.paper
            border.width: window.dockEdge.length && !window.dragHover ? 0 : 1
            border.color: window.dragHover || window.pulsing ? window.accent : window.outline
            clip: true
            Behavior on radius { NumberAnimation { duration: window.motion } }
            Behavior on border.color { ColorAnimation { duration: window.motion } }
            HoverHandler {
                id: surfaceHover
                onHoveredChanged: {
                    if (!hovered && shelf.expanded && !shelf.focusPanel && !window.dragHover) peekDismissTimer.restart();
                    else peekDismissTimer.stop();
                }
            }

            Rectangle {
                anchors.fill: parent
                color: window.accent
                opacity: window.dragHover ? 0.11 : window.pulsing ? 0.08 : 0
                radius: parent.radius
                topLeftRadius: parent.topLeftRadius; topRightRadius: parent.topRightRadius
                bottomLeftRadius: parent.bottomLeftRadius; bottomRightRadius: parent.bottomRightRadius
                Behavior on opacity { NumberAnimation { duration: window.motion } }
            }

            Item {
                id: capsule
                objectName: "capsule"
                anchors.fill: parent
                visible: !shelf.expanded
                MouseArea {
                    id: capsuleMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    property real startX: 0
                    property real startY: 0
                    property bool moved: false
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onEntered: if (window.dockEdge.length && !shelf.expanded) edgeHoverTimer.restart()
                    onExited: edgeHoverTimer.stop()
                    onPressed: function(event) { edgeHoverTimer.stop();startX=event.x; startY=event.y; moved=false; }
                    onPositionChanged: function(event) {
                        if(pressed && !moved && Math.abs(event.x-startX)+Math.abs(event.y-startY)>7) {
                            moved=true; shelf.nativeMove();
                        }
                    }
                    onClicked: function(event) {
                        if(event.button === Qt.RightButton) capsuleMenu.popup();
                        else if(!moved) shelf.toggle();
                    }
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                }
                ToolTip {
                    id: capsuleTip
                    parent: capsule
                    popupType: Popup.Window
                    x: (capsule.width-width)/2
                    y: -height-8
                    visible: capsuleMouse.containsMouse && shelf.message.length > 0
                    delay: 500
                    padding: 9
                    implicitWidth: Math.min(320,capsuleTipText.implicitWidth+18)
                    text: shelf.message
                    contentItem: Text {
                        id: capsuleTipText
                        text: capsuleTip.text
                        textFormat: Text.PlainText
                        color: window.ink
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                    background: Rectangle { radius: 8; color: window.raised; border.color: window.outline }
                }
                Row {
                    visible: !window.dockEdge.length
                    anchors.centerIn: parent
                    spacing: 12
                    Rectangle {
                        width: 38; height: 38; radius: 13
                        color: window.dark ? "#352a47" : "#e7ddfa"
                        ShelfIcon { anchors.centerIn: parent; width: 22; height: 22; name: window.dragHover ? "plus" : "shelf"; color: window.accent }
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        Label { text: window.dragHover ? "Drop to collect" : shelf.activeName; font.pixelSize: 12; font.weight: Font.DemiBold; elide: Text.ElideRight; width: 108 }
                        Label { text: shelf.count ? shelf.count + (shelf.count === 1 ? " item held" : " items held") : "Ready when you are"; color: window.muted; font.pixelSize: 11 }
                    }
                    ShelfIcon { name: "chevron"; color: window.faint; width: 14; height: 14; anchors.verticalCenter: parent.verticalCenter }
                }
                Item {
                    visible: window.dockEdge.length > 0
                    anchors.fill: parent
                    ShelfIcon {
                        anchors.centerIn: parent
                        width: window.dragHover ? 18 : 20; height: width
                        name: window.dragHover ? "plus" : "chevron"
                        // Point back toward the screen, the way the panel opens.
                        rotation: window.dragHover ? 0 : window.dockEdge === "right" ? 180 : window.dockEdge === "top" ? 90 : window.dockEdge === "bottom" ? -90 : 0
                        color: window.dragHover ? window.accent : window.ink
                        opacity: window.dragHover || capsuleMouse.containsMouse ? 1 : 0.72
                        Behavior on opacity { NumberAnimation { duration: window.motion } }
                    }
                    // A small dot says the shelf is holding something.
                    Rectangle {
                        visible: shelf.count > 0 && !window.dragHover
                        width: 5; height: 5; radius: 2.5; color: window.accent
                        x: window.verticalDock ? (parent.width-width)/2 : parent.width-14
                        y: window.verticalDock ? parent.height-14 : (parent.height-height)/2
                    }
                }
            }

            Item {
                id: expanded
                anchors.fill: parent
                visible: shelf.expanded
                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: window.motion } }

                Item {
                    id: header
                    x: 18; y: 13; width: parent.width-36; height: 38
                    MouseArea {
                        anchors.fill: parent
                        onPressed: shelf.nativeMove()
                        cursorShape: Qt.OpenHandCursor
                    }
                    Row {
                        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                        spacing: 9
                        ShelfIcon { name: "shelf"; color: window.accent; width: 20; height: 20; anchors.verticalCenter: parent.verticalCenter }
                        Label { text: shelf.picking ? "Choose from Shelf" : "Shelf"; font.pixelSize: 16; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                        Rectangle {
                            width: countLabel.implicitWidth+14; height: 23; radius: 7
                            color: window.tileSurface; anchors.verticalCenter: parent.verticalCenter
                            Label { id: countLabel; text: shelf.count; color: window.muted; font.pixelSize: 11; anchors.centerIn: parent }
                        }
                    }
                    Row {
                        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; spacing: 3
                        IconButton { glyph: "paste"; tooltip: "Collect from clipboard · Ctrl+V"; visible: !shelf.picking; onClicked: shelf.paste() }
                        IconButton { glyph: "more"; tooltip: "Shelf options"; onClicked: surfaceMenu.popup() }
                        IconButton { glyph: "minus"; tooltip: shelf.picking ? "Cancel selection" : "Collapse · Esc"; onClicked: shelf.picking ? shelf.cancelPick() : window.closePanel() }
                    }
                }

                Rectangle { x: 18; y: 60; width: parent.width-36; height: 1; color: window.outline; opacity: 0.55 }

                RowLayout {
                    id: collectionRow
                    x: 18; y: 72; width: parent.width-36; height: 35; spacing: 10
                    ShelfButton {
                        id: collectionButton
                        text: shelf.activeName; glyph: "down"; foreground: window.ink; hoverSurface: window.hoverSurface
                        accentColor: window.accent; surface: window.tileSurface; reduceMotion: shelf.reducedMotion
                        Layout.maximumWidth: Math.min(240, collectionRow.width*0.48)
                        onClicked: collectionsPopup.open()
                    }
                    Item { Layout.fillWidth: true }
                    Item {
                        Layout.preferredWidth: Math.max(120, Math.min(220, collectionRow.width*0.44)); Layout.fillHeight: true
                        Field {
                            id: searchField
                            objectName: "filterField"
                            anchors.fill: parent
                            leftPadding: 33
                            text: shelf.filter
                            placeholderText: "Filter items"
                            onTextEdited: shelf.filter = text
                            Keys.onEscapePressed: { shelf.filter = ""; interaction.forceActiveFocus(); }
                            Keys.onDownPressed: { interaction.forceActiveFocus(); window.navigate(1,0); }
                        }
                        ShelfIcon { x: 10; anchors.verticalCenter: parent.verticalCenter; width: 15; height: 15; name: "search"; color: window.muted }
                    }
                }

                GridView {
                    id: grid
                    objectName: "shelfGrid"
                    x: 15; y: 122
                    width: Math.max(0,parent.width-30); height: Math.max(0,parent.height-y-footer.height-14)
                    clip: true
                    model: shelf.expanded ? shelf.items : []
                    cellWidth: width / Math.max(2, Math.floor(width / 142))
                    cellHeight: 152
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 5 }
                    delegate: Item {
                        id: tile
                        required property var modelData
                        required property int index
                        objectName: "tile-"+modelData.id
                        width: grid.cellWidth; height: grid.cellHeight
                        readonly property bool chosen: { const revision=shelf.selectionRevision; return shelf.selected(modelData.id); }
                        readonly property bool missing: modelData.available === false
                        Accessible.role: Accessible.ListItem
                        Accessible.name: modelData.name + ", " + window.subtitleFor(modelData)
                        Accessible.selected: chosen
                        Accessible.onPressAction: shelf.select(modelData.id,false,false)
                        Rectangle {
                            id: card
                            x: 5; y: 3; width: parent.width-10; height: parent.height-10
                            radius: 12
                            color: tile.chosen ? window.selection : tileMouse.containsMouse ? window.hoverSurface : window.tileSurface
                            border.width: tile.chosen ? 1.5 : 1
                            border.color: tile.chosen ? window.accent : "transparent"
                            Behavior on color { ColorAnimation { duration: window.motion } }
                            Item {
                                id: preview
                                x: 9; y: 9; width: parent.width-18; height: 81
                                clip: true
                                Image {
                                    id: thumbnail
                                    anchors.fill: parent
                                    source: shelf.expanded && !tile.missing && modelData.thumbnail ? modelData.thumbnail : ""
                                    asynchronous: true
                                    cache: true
                                    fillMode: Image.PreserveAspectCrop
                                    sourceSize.width: 256; sourceSize.height: 180
                                    visible: status === Image.Ready
                                }
                                ShelfIcon {
                                    visible: thumbnail.status !== Image.Ready && modelData.type.indexOf("text/") !== 0
                                    anchors.centerIn: parent; width: 36; height: 36
                                    name: window.iconFor(modelData.type)
                                    color: tile.missing ? window.faint : modelData.type === "folder/reference" ? "#c8ad80" : window.muted
                                }
                                Label {
                                    objectName: "previewText-"+modelData.id
                                    visible: modelData.type === "text/plain" || modelData.type === "text/rich"
                                    anchors.fill: parent; anchors.margins: 5
                                    text: modelData.text || "Text"
                                    font.pixelSize: 10; color: window.muted
                                    wrapMode: Text.Wrap; maximumLineCount: 5; elide: Text.ElideRight
                                }
                                Column {
                                    visible: modelData.type === "text/url"
                                    anchors.centerIn: parent; spacing: 5; width: parent.width-12
                                    ShelfIcon { name: "url"; color: window.accent; width: 25; height: 25; anchors.horizontalCenter: parent.horizontalCenter }
                                    Label { text: (modelData.text || "").replace(/^https?:\/\//, "").split("/")[0]; color: window.muted; font.pixelSize: 10; width: parent.width; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter }
                                }
                            }
                            Label { x: 11; y: 99; width: parent.width-22; text: modelData.name; font.weight: Font.Medium; font.pixelSize: 11; elide: Text.ElideMiddle; maximumLineCount: 1 }
                            Label { x: 11; y: 117; width: parent.width-22; text: window.subtitleFor(modelData); color: tile.missing ? "#df8d9b" : window.muted; font.pixelSize: 9; elide: Text.ElideRight; maximumLineCount: 1 }
                            Rectangle {
                                visible: tile.chosen
                                x: parent.width-22; y: 7; width: 16; height: 16; radius: 8; color: window.accent
                                ShelfIcon { anchors.centerIn: parent; name: "check"; color: "#21162f"; width: 11; height: 11 }
                            }
                            ShelfIcon { visible: tile.missing; x: 9; y: 8; name: "warning"; color: "#df8d9b"; width: 15; height: 15 }
                            MouseArea {
                                id: tileMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                property real pressX: 0; property real pressY: 0
                                property bool startedDrag: false
                                onPressed: function(event) {
                                    pressX=event.x; pressY=event.y; startedDrag=false; window.cursorIndex=tile.index;
                                    if(event.button === Qt.RightButton) {
                                        if(!tile.chosen) shelf.select(modelData.id,false,false);
                                        window.contextItem=modelData.id; itemMenu.popup();
                                    } else if(!tile.chosen || (event.modifiers & (Qt.ControlModifier|Qt.MetaModifier|Qt.ShiftModifier))) shelf.select(modelData.id,!!(event.modifiers & (Qt.ControlModifier|Qt.MetaModifier)),!!(event.modifiers & Qt.ShiftModifier));
                                    interaction.forceActiveFocus();
                                }
                                onClicked: function(event) { if(event.button===Qt.LeftButton && !startedDrag && !(event.modifiers & (Qt.ControlModifier|Qt.MetaModifier|Qt.ShiftModifier))) shelf.select(modelData.id,false,false); }
                                onPositionChanged: function(event) {
                                    if(pressed && (pressedButtons & Qt.LeftButton) && !startedDrag && !shelf.picking && Math.abs(event.x-pressX)+Math.abs(event.y-pressY)>9) {
                                        startedDrag=true; shelf.dragSelected();
                                    }
                                }
                                onDoubleClicked: if(!startedDrag && !shelf.picking) shelf.openItem(modelData.id)
                                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                            }
                        }
                    }
                }

                Column {
                    id: emptyState
                    visible: shelf.expanded && shelf.items.length === 0
                    width: Math.min(380,parent.width-72)
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: grid.y+Math.max(5,(grid.height-implicitHeight)/2)
                    spacing: grid.height < 210 ? 7 : 11
                    Rectangle {
                        width: grid.height < 210 ? 48 : 68; height: width; radius: 20; color: window.tileSurface
                        anchors.horizontalCenter: parent.horizontalCenter
                        ShelfIcon { anchors.centerIn: parent; name: shelf.filter.length ? "search" : "shelf"; color: window.accent; width: 31; height: 31 }
                    }
                    Label { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: shelf.filter.length ? "No matching items" : shelf.picking ? "This shelf is empty" : "A little space for what’s next"; font.pixelSize: 15; font.weight: Font.Medium }
                    Label { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: shelf.filter.length ? "Try a different filter." : shelf.picking ? "Choose another collection to continue." : "Drop files, links, or text here."; color: window.muted; font.pixelSize: 12 }
                    Flow {
                        id: sourceFlow
                        visible: !shelf.filter.length && !shelf.picking && shelf.sources.length > 0 && grid.height > 220
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: sourceFlow.visible && shelf.expanded ? shelf.sources : []
                            delegate: ShelfButton {
                                required property var modelData
                                text: modelData.title; foreground: window.ink; surface: window.tileSurface; hoverSurface: window.hoverSurface; accentColor: window.accent
                                reduceMotion: shelf.reducedMotion
                                onClicked: shelf.runSource(modelData.app,modelData.action || modelData.id)
                            }
                        }
                    }
                }

                Item {
                    id: footer
                    anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
                    height: shelf.message.length || shelf.busy ? 79 : 55
                    Rectangle { width: parent.width-36; x: 18; height: 1; color: window.outline; opacity: 0.5 }
                    Row {
                        x: 18; y: 11; spacing: 5
                        IconButton { glyph: "settings"; tooltip: "Preferences"; onClicked: shelf.showSettings() }
                        Label { text: window.selectedCount ? window.selectedCount + " selected" : shelf.activeName; color: window.muted; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter; width: Math.min(170,expanded.width*0.27); elide: Text.ElideRight }
                    }
                    Row {
                        anchors.right: parent.right; anchors.rightMargin: 18; y: 10; spacing: 5
                        IconButton { visible: !shelf.picking && window.selectedCount > 0; glyph: "copy"; tooltip: "Copy selection · Ctrl+C"; onClicked: shelf.copySelected("content") }
                        IconButton { visible: !shelf.picking && window.selectedCount > 0; glyph: "trash"; tooltip: "Remove from Shelf · Delete"; onClicked: shelf.removeSelected() }
                        ShelfButton { visible: !shelf.picking && window.selectedCount > 0 && shelf.offers.length > 0; text: "Actions"; glyph: "down"; foreground: window.ink; hoverSurface: window.hoverSurface; surface: window.tileSurface; accentColor: window.accent; reduceMotion: shelf.reducedMotion; onClicked: { peerMenu.populated=true;peerMenu.popup(); } }
                        ShelfButton { visible: shelf.picking; text: "Cancel"; foreground: window.ink; hoverSurface: window.hoverSurface; reduceMotion: shelf.reducedMotion; onClicked: shelf.cancelPick() }
                    ShelfButton { objectName: "pickerConfirm"; visible: shelf.picking; text: window.selectedCount ? "Use " + window.selectedCount + (window.selectedCount === 1 ? " item" : " items") : "Choose items"; accent: true; accentColor: window.accent; reduceMotion: shelf.reducedMotion; enabled: window.selectedCount > 0; onClicked: shelf.confirmPick() }
                    }
                    RowLayout {
                        x: 20; y: 51; width: parent.width-40; height: 20; spacing: 8
                        visible: shelf.message.length > 0 || shelf.busy
                        Label { Layout.fillWidth: true; text: shelf.message; color: window.muted; font.pixelSize: 10; elide: Text.ElideRight }
                        ShelfButton { visible: shelf.undoAvailable && !shelf.busy; text: "Undo"; glyph: "undo"; compact: true; implicitHeight: 22; foreground: window.accent; hoverSurface: window.hoverSurface; reduceMotion: shelf.reducedMotion; onClicked: shelf.undo() }
                        ShelfButton { visible: shelf.busy; text: "Cancel"; compact: true; implicitHeight: 22; foreground: window.accent; hoverSurface: window.hoverSurface; onClicked: shelf.cancelJob() }
                    }
                    Rectangle { visible: shelf.busy; x: 18; y: 75; width: (parent.width-36)*Math.max(0,Math.min(1,shelf.progress)); height: 2; color: window.accent; radius: 1; Behavior on width { NumberAnimation { duration: window.motion } } }
                }
            }

            // External MIME drops are accepted here, never converted to an
            // in-process Drag token. Qt's native data device remains in charge.
            DropArea {
                id: dropArea
                objectName: "shelfDropArea"
                anchors.fill: parent
                enabled: !shelf.picking
                onEntered: function(drag) {
                    drag.accepted = drag.hasUrls || drag.hasText || drag.hasHtml;
                    peekDismissTimer.stop();
                    if (drag.accepted && window.dockEdge.length && !shelf.expanded) edgeHoverTimer.restart();
                }
                onExited: {
                    edgeHoverTimer.stop();
                    if (shelf.expanded && !shelf.focusPanel && !surfaceHover.hovered && !shelf.nativeDragHover) peekDismissTimer.restart();
                }
                onDropped: function(drop) {
                    edgeHoverTimer.stop();
                    if (!(drop.hasUrls || drop.hasText || drop.hasHtml)) return;
                    shelf.acceptDrop(drop.urls, drop.hasText ? drop.text : "", drop.hasHtml ? drop.html : "");
                    drop.acceptProposedAction();
                }
            }
            Rectangle {
                visible: shelf.expanded && window.dragHover
                anchors.fill: parent; anchors.margins: 9; radius: 13
                color: window.dark ? "#ed211b30" : "#eeeae2f6"
                border.color: window.accent; border.width: 2
                Column {
                    anchors.centerIn: parent; spacing: 12
                    ShelfIcon { name: "plus"; color: window.accent; width: 38; height: 38; anchors.horizontalCenter: parent.horizontalCenter }
                    Label { text: "Drop to collect"; font.pixelSize: 18; font.weight: Font.Medium; anchors.horizontalCenter: parent.horizontalCenter }
                    Label { text: "Keep the originals right where they are."; color: window.muted; anchors.horizontalCenter: parent.horizontalCenter }
                }
            }
        }

        // Resize strips stay outside the content hit region.
        MouseArea { visible: shelf.expanded; x: 0; y: 12; width: 6; height: parent.height-24; cursorShape: Qt.SizeHorCursor; onPressed: shelf.nativeResize(Qt.LeftEdge) }
        MouseArea { visible: shelf.expanded; x: parent.width-6; y: 12; width: 6; height: parent.height-24; cursorShape: Qt.SizeHorCursor; onPressed: shelf.nativeResize(Qt.RightEdge) }
        MouseArea { visible: shelf.expanded; x: 12; y: 0; width: parent.width-24; height: 6; cursorShape: Qt.SizeVerCursor; onPressed: shelf.nativeResize(Qt.TopEdge) }
        MouseArea { visible: shelf.expanded; x: 12; y: parent.height-6; width: parent.width-24; height: 6; cursorShape: Qt.SizeVerCursor; onPressed: shelf.nativeResize(Qt.BottomEdge) }
        MouseArea { visible: shelf.expanded; x: parent.width-14; y: parent.height-14; width: 14; height: 14; cursorShape: Qt.SizeFDiagCursor; onPressed: shelf.nativeResize(Qt.RightEdge | Qt.BottomEdge) }
    }

    ActionMenu {
        id: capsuleMenu
        MenuEntry { text: "Open " + shelf.activeName; onTriggered: shelf.show() }
        MenuEntry { text: "Collect from clipboard"; onTriggered: shelf.paste() }
        MenuEntry { text: "Undo last collection"; visible: shelf.undoAvailable; onTriggered: shelf.undo() }
        MenuSeparator {}
        MenuEntry { text: "Dock left"; onTriggered: window.dockTo("left") }
        MenuEntry { text: "Dock right"; onTriggered: window.dockTo("right") }
        MenuEntry { text: "Dock at top"; onTriggered: window.dockTo("top") }
        MenuEntry { text: "Dock at bottom"; onTriggered: window.dockTo("bottom") }
        MenuEntry { text: "Undock"; visible: window.dockEdge.length > 0; onTriggered: shelf.dock("") }
        MenuSeparator {}
        MenuEntry { text: "Preferences"; onTriggered: shelf.showSettings() }
    }
    ActionMenu {
        id: surfaceMenu
        MenuEntry { text: "New collection…"; onTriggered: window.editCollection("","","new") }
        MenuEntry { text: "Rename collection…"; onTriggered: window.editCollection(shelf.activeShelf,shelf.activeName,"rename") }
        MenuEntry { text: "Select all"; enabled: shelf.items.length > 0; onTriggered: shelf.selectAll() }
        MenuSeparator {}
        MenuEntry { text: "Dock left"; onTriggered: window.dockTo("left") }
        MenuEntry { text: "Dock right"; onTriggered: window.dockTo("right") }
        MenuEntry { text: "Always on top"; checkable: true; checked: shelf.alwaysOnTop; onTriggered: shelf.alwaysOnTop=!shelf.alwaysOnTop }
        MenuSeparator {}
        MenuEntry { text: "Preferences"; onTriggered: shelf.showSettings() }
    }
    ActionMenu {
        id: itemMenu
        MenuEntry { text: "Open in default app"; onTriggered: shelf.openItem(window.contextItem) }
        MenuEntry { text: "Reveal original"; onTriggered: shelf.revealItem(window.contextItem) }
        MenuSeparator {}
        MenuEntry { text: "Copy"; onTriggered: shelf.copySelected("content") }
        MenuEntry { text: "Copy paths"; onTriggered: shelf.copySelected("paths") }
        MenuSeparator {}
        MenuEntry { text: "Move earlier"; onTriggered: window.moveItem(-1) }
        MenuEntry { text: "Move later"; onTriggered: window.moveItem(1) }
        MenuSeparator {}
        MenuEntry { text: "Remove from Shelf"; onTriggered: shelf.removeSelected() }
    }
    ActionMenu {
        id: peerMenu
        property bool populated: false
        onClosed: populated=false
        Instantiator {
            model: peerMenu.populated ? shelf.offers : []
            delegate: MenuEntry {
                required property var modelData
                text: modelData.title
                enabled: modelData.enabled !== false && !shelf.busy
                onTriggered: window.runOffer(modelData)
            }
            onObjectAdded: function(index, object) { peerMenu.insertItem(index,object); }
            onObjectRemoved: function(index, object) { peerMenu.removeItem(object); }
        }
    }

    Popup {
        id: collectionsPopup
        focus: true
        x: 24; y: 116
        width: Math.min(300,window.width-48)
        height: Math.min(350,collectionsContent.implicitHeight+18)
        padding: 9
        background: Rectangle { color: window.raised; border.color: window.outline; radius: 13 }
        contentItem: ScrollView {
            clip: true
            contentWidth: availableWidth
        Column {
            id: collectionsContent
            width: collectionsPopup.availableWidth
            spacing: 3
            Repeater {
                model: collectionsPopup.visible ? shelf.shelves : []
                delegate: Item {
                    id: collection
                    required property var modelData
                    width: collectionsContent.width; height: 40
                    Rectangle { anchors.fill: parent; radius: 8; color: modelData.id===shelf.activeShelf ? window.selection : collectionMouse.containsMouse ? window.hoverSurface : "transparent" }
                    MouseArea { id: collectionMouse; anchors.fill: parent; hoverEnabled: true; onClicked: { shelf.switchShelf(modelData.id); collectionsPopup.close(); } }
                    ShelfIcon { x: 10; anchors.verticalCenter: parent.verticalCenter; width: 16; height: 16; name: modelData.pinned ? "pin" : "shelf"; color: modelData.id===shelf.activeShelf ? window.accent : window.muted }
                    Label { x: 35; width: parent.width-100; anchors.verticalCenter: parent.verticalCenter; text: modelData.name; elide: Text.ElideRight; font.pixelSize: 12 }
                    Label { anchors.right: collectionMore.left; anchors.rightMargin: 5; anchors.verticalCenter: parent.verticalCenter; text: modelData.count || 0; color: window.muted; font.pixelSize: 10 }
                    IconButton {
                        id: collectionMore
                        anchors.right: parent.right; anchors.rightMargin: 3; anchors.verticalCenter: parent.verticalCenter
                        glyph: "more"; tooltip: "Collection options"
                        onClicked: { collectionOptions.targetCollection=modelData; collectionOptions.popup(); }
                    }
                }
            }
            Rectangle { width: parent.width; height: 1; color: window.outline; opacity: 0.6 }
            ShelfButton { width: parent.width; text: "New collection"; glyph: "plus"; foreground: window.accent; hoverSurface: window.hoverSurface; reduceMotion: shelf.reducedMotion; onClicked: { collectionsPopup.close();window.editCollection("","","new"); } }
        }
        }
    }
    ActionMenu {
        id: collectionOptions
        property var targetCollection: ({})
        MenuEntry { text: "Rename…"; onTriggered: { collectionsPopup.close();window.editCollection(collectionOptions.targetCollection.id,collectionOptions.targetCollection.name,"rename"); } }
        MenuEntry { text: collectionOptions.targetCollection.pinned ? "Unpin" : "Pin collection"; onTriggered: shelf.pinShelf(collectionOptions.targetCollection.id,!collectionOptions.targetCollection.pinned) }
        MenuEntry { text: "Move up"; onTriggered: shelf.moveShelf(collectionOptions.targetCollection.id,-1) }
        MenuEntry { text: "Move down"; onTriggered: shelf.moveShelf(collectionOptions.targetCollection.id,1) }
        MenuSeparator {}
        MenuEntry { text: "Delete collection…"; enabled: shelf.shelves.length > 1; onTriggered: { window.collectionToEdit=collectionOptions.targetCollection.id; collectionsPopup.close();deleteCollection.open(); } }
    }

    Modal {
        id: collectionDialog
        title: window.collectionMode === "new" ? "New collection" : "Rename collection"
        contentItem: ColumnLayout {
            spacing: 15
            Field { id: collectionName; Layout.fillWidth: true; placeholderText: "Collection name"; maximumLength: 80; onAccepted: if(text.trim().length) collectionSave.clicked() }
            PreferenceToggle { id: temporaryToggle; visible: window.collectionMode === "new"; Layout.fillWidth: true; heading: "Temporary collection"; description: "Unpinned items follow your retention setting."; checked: false }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ShelfButton { text: "Cancel"; foreground: window.ink; hoverSurface: window.hoverSurface; onClicked: collectionDialog.close() }
                ShelfButton {
                    id: collectionSave
                    text: window.collectionMode === "new" ? "Create" : "Save"; accent: true; accentColor: window.accent
                    enabled: collectionName.text.trim().length > 0
                    onClicked: {
                        if(window.collectionMode === "new") shelf.createShelf(collectionName.text.trim(),temporaryToggle.checked);
                        else shelf.renameShelf(window.collectionToEdit,collectionName.text.trim());
                        collectionDialog.close();
                    }
                }
            }
        }
    }
    Modal {
        id: deleteCollection
        title: "Delete this collection?"
        contentItem: ColumnLayout {
            spacing: 18
            Label { Layout.fillWidth: true; text: "Staged items will be removed. Original files stay in place."; color: window.muted; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ShelfButton { text: "Keep"; foreground: window.ink; hoverSurface: window.hoverSurface; onClicked: deleteCollection.close() }
                ShelfButton { text: "Delete collection"; destructive: true; surface: window.tileSurface; hoverSurface: window.hoverSurface; onClicked: { shelf.deleteShelf(window.collectionToEdit);deleteCollection.close(); } }
            }
        }
    }
    Modal {
        id: confirmAction
        title: window.pendingOffer.title || "Send selection"
        contentItem: ColumnLayout {
            spacing: 17
            Label { Layout.fillWidth: true; text: window.selectedCount + (window.selectedCount === 1 ? " selected item" : " selected items") + " will be shared with " + (window.pendingOffer.appName || window.pendingOffer.app || "this app") + "."; wrapMode: Text.WordWrap }
            Label { Layout.fillWidth: true; text: "Effects: " + (window.pendingOffer.effects || []).join(", "); color: window.muted; font.pixelSize: 11; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ShelfButton { text: "Cancel"; foreground: window.ink; hoverSurface: window.hoverSurface; onClicked: confirmAction.close() }
                ShelfButton { text: "Continue"; accent: true; accentColor: window.accent; onClicked: { const o=window.pendingOffer;confirmAction.close();shelf.runPeer(o.app,o.action || o.id); } }
            }
        }
    }
    Modal {
        id: analysisDialog
        title: shelf.analysisResult ? shelf.analysisResult.title || "Inspection results" : "Inspection results"
        width: Math.min(500,window.width-32)
        height: Math.min(380,window.height-32)
        onClosed: shelf.dismissAnalysis()
        contentItem: ColumnLayout {
            spacing: 14
            Label { Layout.fillWidth: true; visible: text.length > 0; text: shelf.analysisResult ? shelf.analysisResult.message || "" : ""; color: window.muted; wrapMode: Text.WordWrap }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true
                TextArea {
                    readOnly: true; selectByMouse: true
                    text: shelf.analysisResult ? shelf.analysisResult.formatted || "" : ""
                    textFormat: TextEdit.PlainText
                    color: window.ink; selectionColor: window.accent; selectedTextColor: "#20162f"
                    wrapMode: TextEdit.Wrap
                    font.pixelSize: 12
                    background: Rectangle { color: window.tileSurface; radius: 10 }
                }
            }
            ShelfButton { Layout.alignment: Qt.AlignRight; text: "Done"; accent: true; accentColor: window.accent; onClicked: analysisDialog.close() }
        }
    }

    Popup {
        id: settings
        objectName: "preferencesPopup"
        focus: true
        visible: shelf.settingsOpen
        modal: true
        closePolicy: Popup.CloseOnEscape
        anchors.centerIn: parent
        width: Math.min(590,window.width-28)
        height: Math.min(440,window.height-28)
        padding: 18
        onClosed: shelf.settingsOpen=false
        background: Rectangle { color: window.raised; radius: 16; border.color: window.outline }
        Overlay.modal: Rectangle { color: "#8007060c" }
        contentItem: ColumnLayout {
            spacing: 16
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Preferences"; font.pixelSize: 18; font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                IconButton { glyph: "close"; tooltip: "Close preferences"; onClicked: shelf.settingsOpen=false }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 5
                Repeater {
                    model: ["General", "Storage", "Connected apps", "About"]
                    delegate: ShelfButton {
                        required property string modelData
                        required property int index
                        text: modelData; foreground: window.settingsPage===index ? window.accent : window.muted
                        surface: window.settingsPage===index ? window.selection : "transparent"; hoverSurface: window.hoverSurface
                        onClicked: window.settingsPage=index
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: window.outline; opacity: 0.6 }
            ScrollView {
                objectName: "preferencesScroll"
                Layout.fillWidth: true; Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: settings.availableWidth
                    spacing: 16
                    ColumnLayout {
                        visible: window.settingsPage === 0; Layout.fillWidth: true; spacing: 15
                        Label { text: "APPEARANCE"; font.pixelSize: 9; font.letterSpacing: 1.3; color: window.muted }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "Theme"; Layout.fillWidth: true }
                            Repeater {
                                model: ["system","dark","light"]
                                delegate: ShelfButton {
                                    required property string modelData
                                    objectName: "theme-"+modelData
                                    text: modelData.charAt(0).toUpperCase()+modelData.slice(1)
                                    glyph: modelData === "system" ? "monitor" : modelData === "dark" ? "moon" : "sun"
                                    foreground: shelf.theme===modelData ? window.accent : window.muted
                                    surface: shelf.theme===modelData ? window.selection : window.tileSurface
                                    hoverSurface: window.hoverSurface
                                    onClicked: shelf.theme=modelData
                                }
                            }
                        }
                        PreferenceToggle { objectName: "alwaysTopToggle"; Layout.fillWidth: true; heading: "Always on top"; description: "Keep the floating shelf above ordinary windows."; checked: shelf.alwaysOnTop; onToggled: shelf.alwaysOnTop=checked }
                        PreferenceToggle { objectName: "reducedMotionToggle"; Layout.fillWidth: true; heading: "Reduce motion"; checked: shelf.reducedMotion; onToggled: shelf.reducedMotion=checked }
                        PreferenceToggle { Layout.fillWidth: true; heading: "Start at login"; checked: !!shelf.preferences.startAtLogin; onToggled: shelf.setStartAtLogin(checked) }
                        Label { text: "SHORTCUT"; font.pixelSize: 9; font.letterSpacing: 1.3; color: window.muted }
                        Field {
                            id: shortcutField
                            objectName: "shortcutField"
                            property bool draftDirty: false
                            Layout.fillWidth: true
                            placeholderText: "Ctrl+Alt+S"
                            onTextEdited: draftDirty=true
                            onEditingFinished: {
                                const draft=text;
                                if (draftDirty && draft !== shelf.shortcut) shelf.shortcut=draft;
                                draftDirty=false;
                            }
                            Binding {
                                target: shortcutField
                                property: "text"
                                value: shelf.shortcut
                                when: !shortcutField.draftDirty
                                restoreMode: Binding.RestoreNone
                            }
                        }
                        Label { Layout.fillWidth: true; visible: shelf.shortcutWarning.length > 0; text: shelf.shortcutWarning; color: "#df8d9b"; wrapMode: Text.WordWrap; font.pixelSize: 11 }
                        Label { Layout.fillWidth: true; text: shelf.platformStatus; color: window.muted; wrapMode: Text.WordWrap; font.pixelSize: 11 }
                    }
                    ColumnLayout {
                        visible: window.settingsPage === 1; Layout.fillWidth: true; spacing: 15
                        Label { text: "Shelf holds references to existing files. Only collected text, images, and temporary outputs use local storage."; color: window.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "Owned storage quota"; Layout.fillWidth: true }
                            NumberInput { from: 1; to: 4096; stepSize: 64; value: Math.max(1,Math.floor(Number(shelf.preferences.quotaBytes || 536870912)/1048576)); onValueModified: shelf.setQuota(value) }
                            Label { text: "MiB"; color: window.muted }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "Temporary item retention"; Layout.fillWidth: true }
                            NumberInput { from: 0; to: 3650; value: Number(shelf.preferences.retentionDays === undefined ? 7 : shelf.preferences.retentionDays); onValueModified: shelf.setRetention(value) }
                            Label { text: "days"; color: window.muted }
                        }
                        Label { text: "Set retention to 0 to keep temporary items. Pinned collections are kept. Removing an item never deletes its original file."; color: window.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 11 }
                    }
                    ColumnLayout {
                        visible: window.settingsPage === 2; Layout.fillWidth: true; spacing: 15
                        PreferenceToggle { Layout.fillWidth: true; heading: "Arcade Link"; description: "Allow other Arcade apps to add content and open the picker."; checked: shelf.linkEnabled; onToggled: shelf.linkEnabled=checked }
                        Repeater {
                            model: shelf.settingsOpen && window.settingsPage === 2 ? shelf.peers : []
                            delegate: Item {
                                id: peerRow
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: 57
                                PreferenceToggle {
                                    visible: peerRow.modelData.installed !== false
                                    anchors.fill: parent
                                    heading: peerRow.modelData.name || peerRow.modelData.id
                                    description: peerRow.modelData.linkEnabled === false ? "Arcade Link is disabled in this app" : "Available on this device"
                                    checked: peerRow.modelData.enabled !== false
                                    enabled: shelf.linkEnabled
                                    onToggled: shelf.setPeerEnabled(peerRow.modelData.id,checked)
                                }
                                Column {
                                    visible: peerRow.modelData.installed === false
                                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; spacing: 5
                                    Label { text: peerRow.modelData.name || peerRow.modelData.id; font.weight: Font.Medium }
                                    Label { text: "Not installed"; color: window.muted; font.pixelSize: 11 }
                                }
                                ShelfButton {
                                    visible: peerRow.modelData.installed === false
                                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                    text: "Get"; glyph: "external"; foreground: window.accent; surface: window.tileSurface; hoverSurface: window.hoverSurface
                                    onClicked: shelf.getPeer(peerRow.modelData.id)
                                }
                            }
                        }
                        Label { visible: shelf.peers.length === 0; Layout.fillWidth: true; text: "Installed Arcade apps appear here when they publish an Arcade Link manifest."; color: window.muted; wrapMode: Text.WordWrap }
                    }
                    ColumnLayout {
                        visible: window.settingsPage === 3; Layout.fillWidth: true; spacing: 12
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            width: 58; height: 58; radius: 18; color: window.selection
                            ShelfIcon { anchors.centerIn: parent; name: "shelf"; color: window.accent; width: 30; height: 30 }
                        }
                        Label { Layout.alignment: Qt.AlignHCenter; text: "Arcade Shelf"; font.pixelSize: 20; font.weight: Font.DemiBold }
                        Label { Layout.alignment: Qt.AlignHCenter; text: (shelf.preferences.version || "") + " · Collect. Hold. Continue."; color: window.muted }
                        Label { Layout.fillWidth: true; text: "Built with Qt 6, SQLite, and Arcade Link. Your collections stay on this device."; color: window.muted; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter }
                        ShelfButton { Layout.alignment: Qt.AlignHCenter; text: "Project on GitHub"; glyph: "external"; foreground: window.accent; hoverSurface: window.hoverSurface; onClicked: Qt.openUrlExternally("https://github.com/qa-p1/Arcade-Shelf") }
                        Rectangle { Layout.fillWidth: true; height: 1; color: window.outline; opacity: 0.6 }
                        Label { text: "BACKUP AND RESTORE"; font.pixelSize: 9; font.letterSpacing: 1.3; color: window.muted }
                        Label {
                            Layout.fillWidth: true
                            text: "Owned content currently uses " + Number(shelf.ownedContentBytes).toLocaleString() + " bytes. Export asks before copying it. Referenced original files are never copied."
                            color: window.muted; wrapMode: Text.WordWrap; font.pixelSize: 11
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ShelfButton { Layout.fillWidth: true; text: "Export backup…"; foreground: window.ink; surface: window.tileSurface; hoverSurface: window.hoverSurface; onClicked: shelf.exportPortableSettings() }
                            ShelfButton { Layout.fillWidth: true; text: "Import backup…"; foreground: window.ink; surface: window.tileSurface; hoverSurface: window.hoverSurface; onClicked: shelf.importPortableSettings() }
                        }
                    }
                }
            }
        }
    }
}
