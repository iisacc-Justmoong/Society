pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Qt.labs.folderlistmodel
import LVRS 1.0 as LV
import Society

Item {
    id: root

    required property string path
    property string heading: qsTr("Files")
    property bool imagesOnly: false
    property bool touchNavigation: Qt.platform.os === "ios" || Qt.platform.os === "android"
    readonly property int count: grid.count
    readonly property string selectedPath: directoryModel && grid.currentIndex >= 0
                                           ? directoryModel.get(grid.currentIndex, "filePath") : ""
    readonly property FolderListModel directoryModel: modelLoader.item as FolderListModel
    readonly property bool loading: directoryModel !== null
                                    && directoryModel.status === FolderListModel.Loading
    property var pendingViewState: null

    signal activated(string path, bool isDirectory)

    function activateCurrent(): void {
        if (directoryModel && grid.currentIndex >= 0)
            activated(selectedPath, directoryModel.isFolder(grid.currentIndex))
    }

    function loadModel(): void {
        modelLoader.folderUrl = location.folderUrl
        modelLoader.filterImages = root.imagesOnly
        modelLoader.active = modelLoader.folderUrl.toString().length > 0
    }

    function resetModel(): void {
        pendingViewState = null
        modelLoader.active = false
        Qt.callLater(root.loadModel)
    }

    function rememberView(): void {
        // FolderListModel can publish its first count before inserting rows into the view.
        if (pendingViewState || !directoryModel || grid.count === 0)
            return
        pendingViewState = { model: directoryModel, path: root.path,
                             selectedPath: root.selectedPath, contentY: grid.contentY }
    }

    function restoreView(): void {
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
        grid.forceLayout()
        grid.contentY = Math.max(grid.originY, Math.min(state.contentY,
            grid.originY + Math.max(0, grid.contentHeight - grid.height)))
    }

    onImagesOnlyChanged: resetModel()
    Component.onCompleted: resetModel()

    onPathChanged: {
        pendingViewState = null
        grid.currentIndex = -1
        grid.positionViewAtBeginning()
    }

    DirectoryLocation {
        id: location
        path: root.path
        onFolderUrlChanged: root.resetModel()
    }

    // A disabled Loader avoids FolderListModel's implicit working-directory fallback.
    Loader {
        id: modelLoader
        objectName: "fileModelLoader"
        property url folderUrl: ""
        property bool filterImages: false
        active: false
        sourceComponent: FolderListModel {
            // Snapshot both settings before starting this model's asynchronous scan.
            // Reusing a model while changing its path and filters can retain old rows.
            folder: modelLoader.folderUrl
            showDirs: !modelLoader.filterImages
            showFiles: true
            nameFilters: modelLoader.filterImages ? ["*.png", "*.jpg", "*.jpeg", "*.webp"] : []
            caseSensitive: false
            showDirsFirst: true
            showDotAndDotDot: false
            showHidden: false
            sortField: FolderListModel.Name
            sortCaseSensitive: false
        }
    }

    Connections {
        target: root.directoryModel
        function onModelAboutToBeReset(): void { root.rememberView() }
        function onRowsAboutToBeRemoved(): void { root.rememberView() }
        function onRowsAboutToBeInserted(): void { root.rememberView() }
        function onModelReset(): void { Qt.callLater(root.restoreView) }
        function onRowsRemoved(): void { Qt.callLater(root.restoreView) }
        function onRowsInserted(): void { Qt.callLater(root.restoreView) }
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
                    text: qsTr("Grid view")
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
                anchors.margins: 16
                anchors.rightMargin: 24
                readonly property int columns: Math.max(1, Math.floor(width / 160))
                cellWidth: width / columns
                cellHeight: 176
                clip: true
                model: root.directoryModel
                currentIndex: -1
                keyNavigationEnabled: true
                boundsBehavior: Flickable.StopAtBounds
                activeFocusOnTab: true
                focus: true

                Keys.onEscapePressed: currentIndex = -1
                Keys.onReturnPressed: root.activateCurrent()
                Keys.onEnterPressed: root.activateCurrent()

                Controls.ScrollBar.vertical: Controls.ScrollBar {
                    objectName: "fileScrollBar"
                    policy: Controls.ScrollBar.AsNeeded
                }

                delegate: Item {
                    id: entry
                    required property int index
                    required property string fileName
                    required property string filePath
                    required property url fileUrl
                    required property string fileSuffix
                    required property bool fileIsDir
                    objectName: "fileTile"
                    width: grid.cellWidth
                    height: grid.cellHeight

                    LV.AbstractButton {
                        id: tile
                        anchors.fill: parent
                        anchors.margins: 6
                        horizontalPadding: 10
                        verticalPadding: 10
                        focusPolicy: Qt.NoFocus
                        activeFocusOnTab: false
                        text: entry.fileName
                        Accessible.name: entry.fileName
                        Accessible.role: Accessible.ListItem
                        Accessible.selected: grid.currentIndex === entry.index

                        onClicked: {
                            grid.currentIndex = entry.index
                            grid.forceActiveFocus()
                            if (root.touchNavigation)
                                root.activated(entry.filePath, entry.fileIsDir)
                        }
                        onDoubleClicked: {
                            if (!root.touchNavigation)
                                root.activated(entry.filePath, entry.fileIsDir)
                        }

                        background: Rectangle {
                            radius: LV.Theme.radiusMd
                            color: grid.currentIndex === entry.index ? LV.Theme.accentTint
                                 : tile.hovered ? LV.Theme.panelBackground06 : "transparent"
                            border.width: grid.currentIndex === entry.index ? 1 : 0
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
                                    source: !entry.fileIsDir
                                            && /^(png|jpe?g|webp|gif|bmp|svg)$/i.test(entry.fileSuffix)
                                            ? entry.fileUrl : ""
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
                text: qsTr("Large icons")
            }
        }
    }
}
