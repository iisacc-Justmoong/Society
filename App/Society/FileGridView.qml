pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import LVRS 1.0 as LV
import Society
import "Gallery"
import "Files"

Item {
    id: root

    required property string path
    property string heading: qsTr("Files")
    property bool imagesOnly: false
    property bool chronological: false
    property bool deletedView: false
    property bool selectionMode: false
    property var selectedPaths: []
    readonly property int selectedCount: selectedPaths.length
    property var batchPending: []
    property var batchInFlight: []
    property var batchErrors: []
    property string batchAction: ""
    property string batchFolder: ""
    property string batchStatus: ""
    property url batchDestination: ""
    property int batchSucceeded: 0
    property bool batchRunning: false
    property bool touchNavigation: Qt.platform.os === "ios" || Qt.platform.os === "android"
    readonly property int count: grid.count
    readonly property string selectedPath: directoryModel && grid.currentIndex >= 0
                                           ? directoryModel.get(grid.currentIndex, "filePath") : ""
    readonly property StorageDirectoryModel directoryModel: modelLoader.item as StorageDirectoryModel
    readonly property bool loading: directoryModel !== null
                                    && directoryModel.status === StorageDirectoryModel.Loading
    property var pendingViewState: null
    property bool initialPositionPending: true
    property string downloadStatus: ""

    signal activated(string path, bool isDirectory)

    function toggleSelection(path: string): void {
        if (!selectionMode || batchRunning || !path.length) return
        const paths = selectedPaths.slice()
        const index = paths.indexOf(path)
        if (index < 0) paths.push(path)
        else paths.splice(index, 1)
        selectedPaths = paths
        batchStatus = ""
    }

    function pruneSelection(): void {
        if (!selectionMode || batchRunning || !directoryModel || loading || grid.count !== directoryModel.count) return
        const present = {}
        for (let i = 0; i < directoryModel.count; ++i) present[directoryModel.get(i, "filePath")] = true
        const paths = selectedPaths.filter(function(path) { return present[path] === true })
        if (paths.length !== selectedPaths.length) selectedPaths = paths
    }

    function startDeletedBatch(action, paths, destination): void {
        if (batchRunning || !deletedView || !paths || paths.length === 0) return
        batchAction = action
        batchDestination = destination || ""
        batchFolder = path
        batchPending = paths.slice()
        batchInFlight = []
        batchErrors = []
        batchSucceeded = 0
        batchRunning = true
        batchStatus = action === "remove" ? qsTr("Removing items from Deleted…") : qsTr("Putting items back…")
        processDeletedBatch()
    }

    function restoreSelectionTo(folder): void { startDeletedBatch("restoreTo", selectedPaths, folder) }

    function processDeletedBatch(): void {
        if (!batchRunning) return
        while (batchPending.length > 0 && batchInFlight.length < 4) {
            const current = batchPending[0]
            batchPending = batchPending.slice(1)
            batchInFlight = batchInFlight.concat([current])
            deletedActions.path = current
            if (batchAction === "restore") deletedActions.restore()
            else if (batchAction === "restoreTo") deletedActions.restoreTo(batchDestination)
            else deletedActions.remove()
        }
        if (batchPending.length === 0 && batchInFlight.length === 0) {
            batchRunning = false
            if (directoryModel && path === batchFolder) directoryModel.refresh()
            const summary = batchAction === "remove" ? qsTr("Removed %1 item(s) from Deleted.").arg(batchSucceeded)
                                                      : qsTr("Put back %1 item(s).").arg(batchSucceeded)
            const visibleErrors = batchErrors.slice(0, 3)
            const remaining = batchErrors.length - visibleErrors.length
            batchStatus = batchErrors.length ? summary + "\n" + visibleErrors.join("\n")
                + (remaining > 0 ? "\n" + qsTr("%1 more item(s) could not be processed.").arg(remaining) : "") : summary
            return
        }
    }

    FileActions {
        id: deletedActions
        objectName: "deletedBatchActions"
        onOperationFinished: function(source, result, error) {
            if (!root.batchRunning || !root.batchInFlight.includes(source)) return
            root.batchInFlight = root.batchInFlight.filter(function(path) { return path !== source })
            if (error.length) {
                const errors = root.batchErrors.slice()
                errors.push(source.substring(source.lastIndexOf("/") + 1) + ": " + error)
                root.batchErrors = errors
            } else {
                if (root.path === root.batchFolder)
                    root.selectedPaths = root.selectedPaths.filter(function(path) { return path !== source })
                ++root.batchSucceeded
            }
            Qt.callLater(root.processDeletedBatch)
        }
    }

    function openMenu(index, item, x, y) {
        if (!directoryModel || index < 0 || (selectionMode && !batchRunning)) return
        grid.currentIndex = index
        fileMenu.backgroundOnly = false
        fileMenu.filePath = directoryModel.get(index, "filePath")
        fileMenu.folderPath = root.path
        fileMenu.directory = directoryModel.get(index, "fileIsDir")
        fileMenu.openFor(item, x, y)
    }

    function openEntry(index: int): void {
        if (!directoryModel || index < 0 || index >= directoryModel.count) return
        downloadStatus = directoryModel.get(index, "fileResident")
                       ? "" : qsTr("Downloading from the Society host…")
        directoryModel.activate(index)
    }

    function showInformation(index: int): void {
        if (!directoryModel || index < 0 || index >= directoryModel.count) return
        grid.currentIndex = index
        info.fileName = directoryModel.get(index, "fileName")
        info.preview = directoryModel.get(index, "fileThumbnailUrl")
        info.fields = [
            { label: qsTr("Availability"), value: directoryModel.get(index, "fileResident") ? qsTr("On this device") : qsTr("Download when opened") },
            { label: qsTr("Size"), value: info.formatSize(directoryModel.get(index, "fileSize")) },
            { label: qsTr("Modified"), value: Qt.formatDateTime(directoryModel.get(index, "fileModified"), "yyyy-MM-dd HH:mm") },
            { label: qsTr("Location"), value: selectedPath }
        ]
        info.open()
    }

    function activateCurrent(): void {
        if (directoryModel && grid.currentIndex >= 0) {
            if (imagesOnly) showInformation(grid.currentIndex)
            else openEntry(grid.currentIndex)
        }
    }

    function loadModel(): void {
        modelLoader.folderUrl = location.folderUrl
        modelLoader.filterImages = root.imagesOnly
        modelLoader.sortChronologically = root.chronological
        modelLoader.active = modelLoader.folderUrl.toString().length > 0
    }

    function resetModel(): void {
        pendingViewState = null
        initialPositionPending = true
        grid.currentIndex = -1
        modelLoader.active = false
        Qt.callLater(root.loadModel)
    }

    function rememberView(): void {
        // StorageDirectoryModel can publish its first count before inserting rows into the view.
        if (initialPositionPending || pendingViewState || !directoryModel || grid.count === 0)
            return
        pendingViewState = { model: directoryModel, path: root.path,
                             selectedPath: root.selectedPath, contentY: grid.contentY }
    }

    function restoreView(): void {
        if (!directoryModel || directoryModel.status !== StorageDirectoryModel.Ready
                || grid.count !== directoryModel.count || grid.width <= 0 || grid.height <= 0)
            return
        if (initialPositionPending) {
            // Wait for asynchronous rows and layout before locating the newest file.
            if (grid.count === 0)
                return
            grid.forceLayout()
            grid.currentIndex = -1
            if (root.chronological)
                grid.positionViewAtEnd()
            else
                grid.positionViewAtBeginning()
            initialPositionPending = false
            return
        }
        const state = pendingViewState
        pendingViewState = null
        if (!state || state.model !== directoryModel || state.path !== root.path)
            return
        let selectedIndex = -1
        for (let i = 0; state.selectedPath.length > 0 && i < directoryModel.count; ++i) {
            if (directoryModel.get(i, "filePath") === state.selectedPath) {
                selectedIndex = i
                break
            }
        }
        grid.currentIndex = selectedIndex
        if (selectedIndex < 0) info.close()
        grid.forceLayout()
        grid.contentY = Math.max(grid.originY, Math.min(state.contentY,
            grid.originY + Math.max(0, grid.contentHeight - grid.height)))
    }

    onImagesOnlyChanged: resetModel()
    onChronologicalChanged: resetModel()
    Component.onCompleted: resetModel()

    onPathChanged: {
        fileMenu.close()
        downloadStatus = ""
        info.close()
        pendingViewState = null
        initialPositionPending = true
        grid.currentIndex = -1
        selectionMode = false
        selectedPaths = []
        if (!batchRunning) { batchStatus = ""; batchErrors = [] }
        grid.positionViewAtBeginning()
    }
    onDeletedViewChanged: if (!deletedView) { selectionMode = false; selectedPaths = [] }

    DirectoryLocation {
        id: location
        path: root.path
        onFolderUrlChanged: root.resetModel()
    }

    // A disabled Loader avoids StorageDirectoryModel's implicit working-directory fallback.
    Loader {
        id: modelLoader
        objectName: "fileModelLoader"
        property url folderUrl: ""
        property bool filterImages: false
        property bool sortChronologically: false
        active: false
        sourceComponent: StorageDirectoryModel {
            // Snapshot the location, filters and order before the asynchronous scan.
            // Reusing a model while changing its path and filters can retain old rows.
            folder: modelLoader.folderUrl
            showDirs: !modelLoader.filterImages
            showFiles: true
            nameFilters: modelLoader.filterImages ? ["*.png", "*.jpg", "*.jpeg", "*.webp"] : []
            caseSensitive: false
            showDirsFirst: true
            showDotAndDotDot: false
            showHidden: false
            sortField: modelLoader.sortChronologically ? StorageDirectoryModel.Time : StorageDirectoryModel.Name
            sortReversed: modelLoader.sortChronologically
            sortCaseSensitive: false
        }
    }

    Connections {
        target: root.directoryModel
        function onActivated(path: string, directory: bool): void {
            root.downloadStatus = ""
            root.activated(path, directory)
        }
        function onDownloadFailed(error: string): void {
            root.downloadStatus = qsTr("Could not download this file. Select it again to retry.")
        }
        function onContentsAboutToChange(): void { root.rememberView() }
        function onContentsChanged(): void {
            // Finish the whole row diff before restoring, in the same event turn.
            root.restoreView()
            Qt.callLater(root.restoreView)
            Qt.callLater(root.pruneSelection)
        }
        function onStatusChanged(): void {
            if (!root.loading) Qt.callLater(root.restoreView)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 24
            spacing: 10

            RowLayout {
                Layout.fillWidth: true

                LV.Label {
                    Layout.fillWidth: true
                    style: title2
                    text: root.heading
                }

                LV.LabelButton {
                    objectName: "deletedSelect"
                    visible: root.deletedView
                    text: root.selectionMode ? qsTr("Done") : qsTr("Select")
                    enabled: !root.batchRunning && (root.selectionMode || root.count > 0)
                    onClicked: {
                        root.selectionMode = !root.selectionMode
                        if (!root.selectionMode) root.selectedPaths = []
                        root.batchStatus = ""
                    }
                }
                LV.LabelButton {
                    objectName: "deletedDelete"
                    visible: root.deletedView
                    text: qsTr("Delete")
                    enabled: root.selectionMode && root.selectedPaths.length > 0 && !root.batchRunning
                    onClicked: { deleteSelected.paths = root.selectedPaths.slice(); deleteSelected.open() }
                }
                LV.LabelButton {
                    objectName: "deletedPutBack"
                    visible: root.deletedView
                    text: qsTr("Put back")
                    enabled: root.selectionMode && root.selectedPaths.length > 0 && !root.batchRunning
                    onClicked: root.startDeletedBatch("restore", root.selectedPaths)
                }

                Image {
                    visible: !root.deletedView
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    source: LV.Theme.iconPath("imagegrid")
                    sourceSize: Qt.size(32, 32)
                }

                LV.Label {
                    visible: !root.deletedView
                    style: description
                    text: root.imagesOnly ? qsTr("Gallery") : qsTr("Grid view")
                }
            }

            LV.Label {
                objectName: "folderPath"
                visible: !root.imagesOnly
                Layout.fillWidth: true
                style: description
                text: root.path.length > 0 ? root.path : qsTr("No folder selected")
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
            }

            LV.Label {
                objectName: "fileDownloadStatus"
                visible: root.batchStatus.length > 0 || root.downloadStatus.length > 0 || fileMenu.actions.status.length > 0
                Layout.fillWidth: true
                style: description
                text: root.batchStatus || (fileMenu.actions.busy || fileMenu.actions.errorString.length > 0 ? fileMenu.actions.status : root.downloadStatus || fileMenu.actions.status)
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                sizeToContentHeight: true
            }
            LV.LabelButton {
                objectName: "deletedChooseDestination"
                visible: root.deletedView && root.selectionMode && !root.batchRunning
                    && root.batchAction !== "remove" && root.batchErrors.length > 0 && root.selectedCount > 0
                text: qsTr("Choose destination…")
                onClicked: restoreDestination.open()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: LV.Theme.panelBackground10
        }

        Item {
            id: viewport
            Layout.fillWidth: true
            Layout.fillHeight: true

            GridView {
                id: grid
                objectName: "fileGrid"
                anchors.fill: parent
                anchors.margins: root.imagesOnly ? 0 : 16
                anchors.rightMargin: root.imagesOnly ? 0 : 24
                readonly property int columns: root.imagesOnly ? zoom.columns : Math.max(1, Math.floor(width / 160))
                readonly property int fileRowHeight: 176
                cellWidth: width / columns
                cellHeight: root.imagesOnly ? cellWidth : Math.max(cellWidth, fileRowHeight)
                interactive: !zoom.interacting
                clip: true
                model: root.directoryModel
                currentIndex: -1
                keyNavigationEnabled: true
                boundsBehavior: Flickable.StopAtBounds
                activeFocusOnTab: true
                focus: true
                onCountChanged: Qt.callLater(root.restoreView)
                onHeightChanged: if (root.initialPositionPending) Qt.callLater(root.restoreView)
                onWidthChanged: if (root.initialPositionPending) Qt.callLater(root.restoreView)

                Keys.onEscapePressed: {
                    if (root.selectionMode && !root.batchRunning) { root.selectionMode = false; root.selectedPaths = [] }
                    else currentIndex = -1
                }
                Keys.onSpacePressed: if (root.selectionMode && root.selectedPath.length) root.toggleSelection(root.selectedPath)
                Keys.onReturnPressed: if (root.selectionMode) root.toggleSelection(root.selectedPath); else root.activateCurrent()
                Keys.onEnterPressed: if (root.selectionMode) root.toggleSelection(root.selectedPath); else root.activateCurrent()

                Controls.ScrollBar.vertical: Controls.ScrollBar {
                    objectName: "fileScrollBar"
                    policy: Controls.ScrollBar.AsNeeded
                }

                TapHandler {
                    enabled: !root.selectionMode || root.batchRunning
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    acceptedButtons: Qt.RightButton
                    onTapped: function(eventPoint) {
                        if (grid.indexAt(eventPoint.position.x + grid.contentX, eventPoint.position.y + grid.contentY) >= 0) return
                        fileMenu.backgroundOnly = true
                        fileMenu.folderPath = root.path
                        fileMenu.openFor(grid, eventPoint.position.x, eventPoint.position.y)
                    }
                }

                delegate: Item {
                    id: entry
                    required property int index
                    required property string fileName
                    required property string filePath
                    required property url fileUrl
                    required property url filePreviewUrl
                    required property url fileThumbnailUrl
                    required property bool fileResident
                    required property string fileSuffix
                    required property bool fileIsDir
                    objectName: "fileTile"
                    width: grid.cellWidth
                    height: grid.cellHeight

                    TapHandler {
                        enabled: !root.selectionMode || root.batchRunning
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        acceptedButtons: Qt.RightButton
                        onTapped: function(eventPoint) { root.openMenu(entry.index, entry, eventPoint.position.x, eventPoint.position.y) }
                    }
                    TapHandler {
                        enabled: !root.selectionMode || root.batchRunning
                        acceptedDevices: PointerDevice.TouchScreen
                        onLongPressed: root.openMenu(entry.index, entry, 0, entry.height)
                    }

                    GalleryTile {
                        visible: root.imagesOnly
                        width: grid.cellWidth - 2
                        height: width
                        name: entry.fileName
                        // Old rows can outlive the Files → gallery filter change.
                        preview: root.imagesOnly && !entry.fileIsDir
                                 && ["png", "jpg", "jpeg", "webp"].includes(entry.fileSuffix.toLowerCase())
                                 ? entry.fileThumbnailUrl : ""
                        previewObjectName: "galleryThumbnail"
                        selected: grid.currentIndex === entry.index
                        onClicked: if (!fileMenu.visible) root.showInformation(entry.index)
                    }

                    LV.AbstractButton {
                        id: tile
                        objectName: "fileEntryButton"
                        visible: !root.imagesOnly
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 6
                        height: entry.fileIsDir ? width : grid.fileRowHeight - 12
                        horizontalPadding: 10
                        verticalPadding: 10
                        showFocusRing: !entry.fileIsDir
                        focusPolicy: Qt.NoFocus
                        activeFocusOnTab: false
                        text: entry.fileName
                        Accessible.name: entry.fileName
                        Accessible.role: Accessible.ListItem
                        Accessible.selected: root.selectionMode ? root.selectedPaths.includes(entry.filePath) : grid.currentIndex === entry.index

                        onClicked: {
                            if (fileMenu.visible) return
                            grid.currentIndex = entry.index
                            grid.forceActiveFocus()
                            if (root.selectionMode)
                                root.toggleSelection(entry.filePath)
                            else if (root.touchNavigation)
                                root.openEntry(entry.index)
                        }
                        onDoubleClicked: {
                            if (!root.selectionMode && !root.touchNavigation)
                                root.openEntry(entry.index)
                        }

                        background: Rectangle {
                            radius: entry.fileIsDir ? 0 : LV.Theme.radiusMd
                            color: entry.fileIsDir ? "transparent"
                                 : (root.selectionMode ? root.selectedPaths.includes(entry.filePath) : grid.currentIndex === entry.index) ? LV.Theme.accentTint
                                 : tile.hovered ? LV.Theme.panelBackground06 : "transparent"
                            border.width: !entry.fileIsDir && (root.selectionMode ? root.selectedPaths.includes(entry.filePath) : grid.currentIndex === entry.index) ? 1 : 0
                            border.color: LV.Theme.accent
                        }

                        contentItem: ColumnLayout {
                            spacing: 10

                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                Image {
                                    id: thumbnail
                                    objectName: "fileThumbnail"
                                    anchors.fill: parent
                                    source: !root.imagesOnly && !entry.fileIsDir
                                            && /^(png|jpe?g|webp|gif|bmp|svg)$/i.test(entry.fileSuffix)
                                            ? entry.fileThumbnailUrl : ""
                                    sourceSize: Qt.size(256, 256)
                                    asynchronous: true
                                    autoTransform: true
                                    fillMode: Image.PreserveAspectFit
                                    visible: status === Image.Ready
                                }

                                Image {
                                    objectName: "fileIcon"
                                    anchors.centerIn: parent
                                    width: 76
                                    height: 76
                                    sourceSize: Qt.size(152, 152)
                                    source: LV.Theme.iconPath(entry.fileIsDir
                                                            ? "nodesfolder" : "fileTypesunknown")
                                    fillMode: Image.PreserveAspectFit
                                    visible: !thumbnail.visible
                                }
                            }

                            LV.Label {
                                objectName: "fileName"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                style: body
                                text: entry.fileName
                                color: entry.fileIsDir && ((root.selectionMode ? root.selectedPaths.includes(entry.filePath) : grid.currentIndex === entry.index) || tile.hovered || tile.down)
                                       ? LV.Theme.accent : LV.Theme.bodyColor
                                textFormat: Text.PlainText
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WrapAnywhere
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                    }
                    LV.CheckBox {
                        objectName: "deletedSelectionCheck"
                        z: 2
                        anchors.top: tile.top
                        anchors.right: tile.right
                        anchors.margins: 8
                        visible: root.selectionMode && root.deletedView
                        enabled: !root.batchRunning
                        checked: root.selectedPaths.includes(entry.filePath)
                        Accessible.name: qsTr("Select %1").arg(entry.fileName)
                        onClicked: root.toggleSelection(entry.filePath)
                    }
                }
            }

            GalleryZoom {
                id: zoom
                enabled: root.imagesOnly && root.visible
                view: grid
            }

            ColumnLayout {
                objectName: "fileGridEmptyState"
                anchors.centerIn: parent
                width: Math.min(320, parent.width - 48)
                spacing: 12
                visible: root.count === 0

                Image {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 56
                    Layout.preferredHeight: 56
                    Layout.bottomMargin: 6
                    source: LV.Theme.iconPath("imagegrid")
                    sourceSize: Qt.size(112, 112)
                    opacity: 0.5
                }

                LV.Label {
                    objectName: "emptyStateTitle"
                    Layout.fillWidth: true
                    style: header
                    horizontalAlignment: Text.AlignHCenter
                    text: location.errorString.length > 0 ? qsTr("Folder unavailable")
                        : root.loading ? qsTr("Loading files…")
                        : root.path.length === 0 ? qsTr("No folder selected")
                        : root.imagesOnly ? qsTr("No generated images yet")
                        : qsTr("This folder is empty")
                }

                LV.Label {
                    Layout.fillWidth: true
                    style: description
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    sizeToContentHeight: true
                    text: location.errorString.length > 0 ? location.errorString
                        : root.loading ? qsTr("Preparing your files.")
                        : root.path.length === 0 ? qsTr("Your files will appear here in a grid.")
                        : root.imagesOnly ? qsTr("Generated images are saved here automatically.")
                        : qsTr("Files added to this folder will appear here.")
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: LV.Theme.panelBackground10
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.topMargin: 12
            Layout.bottomMargin: 12

            LV.Label {
                objectName: "fileCount"
                Layout.fillWidth: true
                style: caption
                text: root.selectionMode ? qsTr("%1 selected").arg(root.selectedPaths.length)
                     : root.count === 1 ? qsTr("1 item") : qsTr("%1 items").arg(root.count)
            }

            LV.Label {
                style: caption
                text: root.imagesOnly ? qsTr("Drag sideways or pinch to zoom") : qsTr("Large icons")
            }
        }
    }
    GalleryInfo {
        id: info
        onOpenRequested: root.openEntry(grid.currentIndex)
    }
    FileActionMenu {
        id: fileMenu
        objectName: "fileContextMenu"
        onOpenRequested: root.openEntry(grid.currentIndex)
        onRevealRequested: function(path) { root.activated(path, true) }
        onModified: { if (root.directoryModel) root.directoryModel.refresh(); info.close() }
    }
    LV.Sheet {
        id: deleteSelected
        objectName: "deleteSelectedSheet"
        property var paths: []
        title: qsTr("Remove selected items from Deleted?")
        preferredHeight: 260
        LV.VStack {
            width: parent.width
            spacing: 16
            LV.Label {
                Layout.fillWidth: true
                text: qsTr("Remove %1 item(s) from Deleted? Any managed originals and recorded history are retained.").arg(deleteSelected.paths.length)
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                sizeToContentHeight: true
            }
            LV.HStack {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                LV.LabelButton { text: qsTr("Cancel"); onClicked: deleteSelected.close() }
                LV.LabelButton {
                    objectName: "confirmDeleteSelected"
                    text: qsTr("Remove from Deleted")
                    onClicked: { const paths = deleteSelected.paths; deleteSelected.close(); root.startDeletedBatch("remove", paths) }
                }
            }
        }
    }
    Dialogs.FolderDialog {
        id: restoreDestination
        objectName: "deletedDestinationDialog"
        title: qsTr("Choose a folder in this Society container")
        onAccepted: root.restoreSelectionTo(selectedFolder)
    }
    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: root.visible && grid.activeFocus && root.selectedPath.length > 0
                 && (fileMenu.actions.activeCount === 0 || !fileMenu.actions.busyForPath(root.selectedPath))
        onActivated: { fileMenu.filePath = root.selectedPath; fileMenu.actions.copy() }
    }
    Shortcut {
        sequences: [StandardKey.Paste]
        enabled: root.visible && grid.activeFocus && fileMenu.actions.canPaste
        onActivated: fileMenu.actions.paste(root.path)
    }
}
