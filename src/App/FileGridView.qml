pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
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

    function openMenu(index, item, x, y) {
        if (!directoryModel || index < 0) return
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
        info.preview = directoryModel.get(index, "filePreviewUrl")
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
        grid.positionViewAtBeginning()
    }

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

                Image {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    source: LV.Theme.iconPath("imagegrid")
                    sourceSize: Qt.size(32, 32)
                }

                LV.Label {
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
                visible: root.downloadStatus.length > 0 || fileMenu.actions.status.length > 0
                Layout.fillWidth: true
                style: description
                text: fileMenu.actions.busy || fileMenu.actions.errorString.length > 0 ? fileMenu.actions.status : root.downloadStatus || fileMenu.actions.status
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
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

                Keys.onEscapePressed: currentIndex = -1
                Keys.onReturnPressed: root.activateCurrent()
                Keys.onEnterPressed: root.activateCurrent()

                Controls.ScrollBar.vertical: Controls.ScrollBar {
                    objectName: "fileScrollBar"
                    policy: Controls.ScrollBar.AsNeeded
                }

                TapHandler {
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
                    required property bool fileResident
                    required property string fileSuffix
                    required property bool fileIsDir
                    objectName: "fileTile"
                    width: grid.cellWidth
                    height: grid.cellHeight

                    TapHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        acceptedButtons: Qt.RightButton
                        onTapped: function(eventPoint) { root.openMenu(entry.index, entry, eventPoint.position.x, eventPoint.position.y) }
                    }
                    TapHandler {
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
                                 ? entry.filePreviewUrl : ""
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
                        Accessible.selected: grid.currentIndex === entry.index

                        onClicked: {
                            if (fileMenu.visible) return
                            grid.currentIndex = entry.index
                            grid.forceActiveFocus()
                            if (root.touchNavigation)
                                root.openEntry(entry.index)
                        }
                        onDoubleClicked: {
                            if (!root.touchNavigation)
                                root.openEntry(entry.index)
                        }

                        background: Rectangle {
                            radius: entry.fileIsDir ? 0 : LV.Theme.radiusMd
                            color: entry.fileIsDir ? "transparent"
                                 : grid.currentIndex === entry.index ? LV.Theme.accentTint
                                 : tile.hovered ? LV.Theme.panelBackground06 : "transparent"
                            border.width: !entry.fileIsDir && grid.currentIndex === entry.index ? 1 : 0
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
                                            ? entry.filePreviewUrl : ""
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
                                color: entry.fileIsDir && (grid.currentIndex === entry.index || tile.hovered || tile.down)
                                       ? LV.Theme.accent : LV.Theme.bodyColor
                                textFormat: Text.PlainText
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WrapAnywhere
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
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
                text: root.count === 1 ? qsTr("1 item") : qsTr("%1 items").arg(root.count)
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
    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: root.visible && grid.activeFocus && root.selectedPath.length > 0 && !fileMenu.actions.busy
        onActivated: { fileMenu.filePath = root.selectedPath; fileMenu.actions.copy() }
    }
    Shortcut {
        sequences: [StandardKey.Paste]
        enabled: root.visible && grid.activeFocus && fileMenu.actions.canPaste && !fileMenu.actions.busy
        onActivated: fileMenu.actions.paste(root.path)
    }
}
