pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import LVRS 1.0 as LV
import "../Gallery"

Item {
    id: root
    required property var controller
    property bool touchNavigation: Qt.platform.os === "ios" || Qt.platform.os === "android"
    property string selectedId: ""
    property var pendingViewState: null
    property bool initialPositionPending: true
    objectName: "photosView"

    function resetView(): void {
        pendingViewState = null
        initialPositionPending = true
        Qt.callLater(root.restoreView)
    }

    function rememberView(): void {
        // Coalesce partial catalog updates before the grid has restored its layout.
        if (initialPositionPending || pendingViewState || grid.count === 0) return
        pendingViewState = { contentY: grid.contentY, atEnd: grid.atYEnd }
    }

    function restoreView(): void {
        if (!root.visible || !root.controller || grid.width <= 0 || grid.height <= 0
                || grid.count !== root.controller.entries.length) return
        if (grid.count === 0) {
            pendingViewState = null
            initialPositionPending = true
            return
        }
        grid.forceLayout()
        if (initialPositionPending) {
            grid.positionViewAtEnd()
            initialPositionPending = false
            pendingViewState = null
            return
        }
        const state = pendingViewState
        pendingViewState = null
        if (!state) return
        if (state.atEnd)
            grid.positionViewAtEnd()
        else
            grid.contentY = Math.max(grid.originY, Math.min(state.contentY,
                grid.originY + Math.max(0, grid.contentHeight - grid.height)))
    }

    function showInformation(entry): void {
        selectedId = entry.id
        info.fileName = entry.name
        info.preview = entry.previewStamp.length > 0 ? entry.preview : ""
        let bytes = 0
        for (const resource of entry.resources) bytes += Number(resource.size)
        info.fields = [
            { label: qsTr("Type"), value: entry.media === "video" ? qsTr("Video") : qsTr("Photo") },
            { label: qsTr("Size"), value: info.formatSize(bytes) },
            { label: qsTr("Created"), value: Qt.formatDateTime(new Date(entry.created), "yyyy-MM-dd HH:mm") }
        ]
        info.open()
    }
    onVisibleChanged: {
        if (visible) resetView()
        else info.close()
    }
    onControllerChanged: resetView()
    Component.onCompleted: resetView()
    onSelectedIdChanged: if (selectedId.length === 0) info.close()
    Connections {
        target: root.controller
        function onEntriesAboutToChange() { root.rememberView() }
        function onEntriesChanged() {
            if (root.selectedId.length > 0 && !root.controller.entries.some(function(entry) { return entry.id === root.selectedId }))
                root.selectedId = ""
            Qt.callLater(root.restoreView)
        }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        LV.Label { text: qsTr("Photos"); style: title2; Layout.fillWidth: true; Layout.leftMargin: 16; Layout.topMargin: 16 }
        Flow {
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            spacing: 8
            LV.PushButton {
                objectName: "connectPhotoLibrary"
                height: Math.max(implicitHeight, root.touchNavigation ? 44 : 0)
                visible: root.controller && root.controller.access !== "full" && root.controller.access !== "unsupported"
                text: root.controller && root.controller.access === "limited" ? qsTr("Manage access…")
                    : root.width < 600 ? qsTr("Connect…") : qsTr("Connect photo library…")
                enabled: root.controller && !root.controller.busy
                onClicked: root.controller.connectLibrary()
            }
            LV.PushButton {
                objectName: "addPhotos"
                height: Math.max(implicitHeight, root.touchNavigation ? 44 : 0)
                text: root.width < 600 ? qsTr("Add…") : qsTr("Add photos or videos…")
                enabled: root.controller && root.controller.active && !root.controller.busy
                onClicked: picker.open()
            }
            LV.PushButton {
                height: Math.max(implicitHeight, root.touchNavigation ? 44 : 0)
                text: qsTr("Refresh")
                enabled: root.controller && root.controller.active && !root.controller.busy
                onClicked: root.controller.refresh()
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            GridView {
                id: grid
                objectName: "photoGrid"
                anchors.fill: parent
                clip: true
                cellWidth: width / zoom.columns
                cellHeight: cellWidth
                interactive: !zoom.interacting
                model: root.controller ? root.controller.entries : []
                currentIndex: -1
                boundsBehavior: Flickable.StopAtBounds
                onCountChanged: Qt.callLater(root.restoreView)
                onHeightChanged: if (root.initialPositionPending) Qt.callLater(root.restoreView)
                onWidthChanged: if (root.initialPositionPending) Qt.callLater(root.restoreView)
                Controls.ScrollBar.vertical: Controls.ScrollBar {}
                delegate: GalleryTile {
                    id: tile
                    required property var modelData
                    width: grid.cellWidth - 2
                    height: width
                    name: modelData.name
                    preview: modelData.previewStamp.length > 0 ? modelData.preview : ""
                    previewObjectName: "photoPreview-" + modelData.id
                    video: modelData.media === "video"
                    selected: root.selectedId === modelData.id
                    onClicked: root.showInformation(modelData)
                }
                LV.Label {
                    anchors.centerIn: parent
                    visible: grid.count === 0
                    width: Math.min(380, parent.width)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: root.controller && root.controller.busy ? qsTr("Reading your photo library…")
                        : qsTr("Connect your photo library or add photos and videos here. New items also appear in your device’s photo library.")
                    style: description
                }
            }
            GalleryZoom { id: zoom; view: grid; enabled: root.visible }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 12
            LV.Label {
                objectName: "photoStatus"
                Layout.fillWidth: true
                style: description
                text: root.controller ? root.controller.status : ""
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
            }
        }
    }
    GalleryInfo {
        id: info
        canTrash: true
        busy: root.controller ? root.controller.busy : false
        onOpenRequested: if (root.selectedId.length > 0) root.controller.openPhoto(root.selectedId)
        onTrashRequested: if (root.selectedId.length > 0) {
            root.controller.trashPhoto(root.selectedId)
            info.close()
        }
    }
    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (drop.hasUrls && root.controller && !root.controller.busy) {
                root.controller.addFiles(drop.urls)
                drop.acceptProposedAction()
            }
        }
    }
    FileDialog {
        id: picker
        title: qsTr("Add photos and videos")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Photos and videos (*.jpg *.jpeg *.png *.heic *.heif *.webp *.gif *.tif *.tiff *.dng *.mov *.mp4 *.m4v *.avi *.mkv *.webm)"), qsTr("All files (*)")]
        onAccepted: root.controller.addFiles(selectedFiles)
    }
}
