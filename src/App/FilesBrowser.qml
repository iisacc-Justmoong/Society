pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV
import Society
import "Files"

Item {
    id: root
    objectName: "filesBrowser"
    required property string path
    property var breadcrumbs: []
    property string query: ""
    property bool listMode: true
    property int sortColumn: 0
    property bool descending: false
    property var entries: []
    property string selectedPath: ""
    property string downloadStatus: ""
    readonly property StorageDirectoryModel directoryModel: modelLoader.item as StorageDirectoryModel
    readonly property bool loading: directoryModel !== null && directoryModel.status === StorageDirectoryModel.Loading
    readonly property int count: entries.length
    readonly property int folderCount: entries.filter(function(entry) { return entry.directory }).length
    readonly property var selectedEntry: entries.find(function(entry) { return entry.path === root.selectedPath }) || null
    readonly property var selectedDetails: selectedEntry && selectedEntry.resident ? location.fileDetails(selectedPath) : ({})
    readonly property bool showInspector: width >= 1000
    readonly property var tableRows: entries.map(function(entry) {
        return [entry.name, root.formatDate(entry.modified), entry.kind, entry.directory ? "—" : root.formatSize(entry.bytes)]
    })
    signal activated(string path, bool isDirectory)

    function formatSize(bytes) {
        if (bytes < 1024) return qsTr("%1 B").arg(bytes)
        const units = ["KB", "MB", "GB", "TB"]
        let value = bytes / 1024
        let unit = 0
        while (value >= 1024 && unit < units.length - 1) { value /= 1024; ++unit }
        return value.toFixed(value < 10 ? 1 : 0) + " " + units[unit]
    }
    function formatDate(value) {
        if (!value || !isFinite(new Date(value).getTime())) return "—"
        return Qt.formatDateTime(value, "dd MMM yyyy, HH:mm")
    }
    function fileKind(suffix, directory) {
        if (directory) return qsTr("Folder")
        const kinds = { pdf: qsTr("PDF document"), md: qsTr("Markdown"), txt: qsTr("Text document"),
            csv: qsTr("CSV document"), png: qsTr("PNG image"), jpg: qsTr("JPEG image"), jpeg: qsTr("JPEG image"),
            svg: qsTr("SVG image"), webp: qsTr("WebP image"), gif: qsTr("GIF image"), mov: qsTr("QuickTime movie"),
            mp4: qsTr("MPEG-4 video"), docx: qsTr("Word document"), fig: qsTr("Figma document"), zip: qsTr("ZIP archive") }
        return kinds[suffix] || (suffix ? qsTr("%1 file").arg(suffix.toUpperCase()) : qsTr("File"))
    }
    function rebuildEntries() {
        if (!directoryModel) { entries = []; return }
        const next = []
        const needle = query.trim().toLocaleLowerCase()
        for (let i = 0; i < directoryModel.count; ++i) {
            const name = String(directoryModel.get(i, "fileName"))
            if (needle && name.toLocaleLowerCase().indexOf(needle) < 0) continue
            const directory = directoryModel.get(i, "fileIsDir") === true
            const suffix = String(directoryModel.get(i, "fileSuffix")).toLowerCase()
            next.push({ name: name, path: directoryModel.get(i, "filePath"), sourceIndex: i,
                directory: directory, bytes: Number(directoryModel.get(i, "fileSize")),
                modified: directoryModel.get(i, "fileModified"), resident: directoryModel.get(i, "fileResident") === true,
                kind: fileKind(suffix, directory),
                icon: directory ? "nodesfolder" : ["png", "jpg", "jpeg", "webp", "svg", "gif"].indexOf(suffix) >= 0 ? "fileTypesimage" : "fileTypestext" })
        }
        next.sort(function(a, b) {
            if (a.directory !== b.directory) return a.directory ? -1 : 1
            let order = 0
            if (root.sortColumn === 1) order = (new Date(a.modified).getTime() || 0) - (new Date(b.modified).getTime() || 0)
            else if (root.sortColumn === 2) order = a.kind.localeCompare(b.kind)
            else if (root.sortColumn === 3) order = a.bytes - b.bytes
            if (!order) order = a.name.localeCompare(b.name)
            return root.descending ? -order : order
        })
        // A changed directory snapshot must not move the viewport or select another path.
        const oldY = tableScroll.contentY
        entries = next
        if (!next.some(function(entry) { return entry.path === root.selectedPath })) selectedPath = ""
        Qt.callLater(function() {
            if (tableScroll && oldY > 0) tableScroll.contentY = Math.max(0, Math.min(oldY, tableScroll.contentHeight - tableScroll.height))
        })
    }
    function selectEntry(index) {
        if (index >= 0 && index < entries.length) selectedPath = entries[index].path
    }
    function openSelected() {
        if (!selectedEntry || !directoryModel) return
        downloadStatus = selectedEntry.resident ? "" : qsTr("Downloading from the Society host…")
        directoryModel.activate(selectedEntry.sourceIndex)
    }
    function setSort(column) {
        descending = sortColumn === column ? !descending : false
        sortColumn = column
        rebuildEntries()
    }
    function revealSelected() {
        if (selectedEntry) Qt.openUrlExternally(location.folderUrl)
    }
    function openMenu(index, item, x, y) {
        if (index >= 0) selectEntry(index)
        fileMenu.backgroundOnly = index < 0
        fileMenu.filePath = selectedPath
        fileMenu.folderPath = path
        fileMenu.directory = selectedEntry ? selectedEntry.directory : false
        fileMenu.openFor(item, x, y)
    }
    function moveSelection(delta) {
        const index = entries.findIndex(function(entry) { return entry.path === root.selectedPath })
        const next = Math.max(0, Math.min(count - 1, index + delta))
        selectEntry(next)
        const top = filesTable.implicitHeight - filesTable.totalBodyHeight() + filesTable.rowY(next)
        if (top < tableScroll.contentY) tableScroll.contentY = top
        else if (top + filesTable.rowHeight > tableScroll.contentY + tableScroll.height)
            tableScroll.contentY = top + filesTable.rowHeight - tableScroll.height
    }
    onQueryChanged: rebuildEntries()
    onPathChanged: {
        entries = []; selectedPath = ""; query = ""; downloadStatus = ""
        fileMenu.close(); tableScroll.contentY = 0; tableScroll.contentX = 0
    }
    DirectoryLocation { id: location; path: root.path }
    Loader {
        id: modelLoader
        active: location.folderUrl.toString().length > 0
        sourceComponent: StorageDirectoryModel {
            folder: location.folderUrl
            showDirs: true
            showFiles: true
            showHidden: false
            showDotAndDotDot: false
            showDirsFirst: true
            sortCaseSensitive: false
            sortField: StorageDirectoryModel.Name
        }
        onLoaded: root.rebuildEntries()
    }
    Connections {
        target: root.directoryModel
        function onContentsChanged() { root.rebuildEntries() }
        function onStatusChanged() { if (!root.loading) root.rebuildEntries() }
        function onActivated(path, directory) { root.downloadStatus = ""; root.activated(path, directory) }
        function onDownloadFailed(error) { root.downloadStatus = error }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: LV.Theme.gap24
        spacing: LV.Theme.gap24
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: LV.Theme.gap24
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: LV.Theme.gap24
                RowLayout {
                    objectName: "filesToolbar"
                    Layout.fillWidth: true
                    spacing: LV.Theme.gap8
                    LV.LabelSegmentedControl {
                        objectName: "filesViewMode"
                        forceBorderlessTone: false
                        LV.LabelButton {
                            objectName: "filesListMode"
                            text: qsTr("List")
                            tone: root.listMode ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                            onClicked: root.listMode = true
                        }
                        LV.LabelButton {
                            objectName: "filesGridMode"
                            text: qsTr("Grid")
                            tone: root.listMode ? LV.AbstractButton.Borderless : LV.AbstractButton.Default
                            onClicked: root.listMode = false
                        }
                    }
                    LV.LabelMenuButton {
                        id: sortButton
                        objectName: "filesSort"
                        text: qsTr("Sort")
                        tone: LV.AbstractButton.Default
                        onClicked: sortMenu.openFor(sortButton, 0, sortButton.height)
                    }
                    Item { Layout.fillWidth: true }
                    LV.InputField {
                        objectName: "filesSearch"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 205
                        Layout.maximumWidth: 205
                        search: true
                        placeholderText: qsTr("Search Files")
                        text: root.query
                        onTextEdited: root.query = text
                    }
                }
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Flickable {
                        id: tableScroll
                        objectName: "filesTableScroll"
                        anchors.fill: parent
                        visible: root.listMode
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        contentWidth: filesTable.width
                        contentHeight: filesTable.implicitHeight
                        activeFocusOnTab: true
                        Controls.ScrollBar.vertical: Controls.ScrollBar {}
                        Controls.ScrollBar.horizontal: Controls.ScrollBar {}
                        Keys.onDownPressed: root.moveSelection(1)
                        Keys.onUpPressed: root.moveSelection(-1)
                        Keys.onReturnPressed: root.openSelected()
                        Keys.onEnterPressed: root.openSelected()
                        Keys.onEscapePressed: root.selectedPath = ""
                        LV.Table {
                            id: filesTable
                            objectName: "filesTable"
                            width: Math.max(728, tableScroll.width)
                            columns: [qsTr("Name"), qsTr("Date modified"), qsTr("Kind"), qsTr("Size")]
                            rows: root.tableRows
                            // Table owns a mutable geometry model; reapply widths when
                            // the viewport changes instead of relying on its write-back binding.
                            function fitColumns() { columnWidths = [width - 504, 196, 188, 120] }
                            onWidthChanged: Qt.callLater(fitColumns)
                            Component.onCompleted: fitColumns()
                            backgroundColor: "transparent"
                            borderWidth: 0
                            resizeHandlesVisible: false
                            selectionEnabled: false
                            deleteContextMenuEnabled: false
                            // Heights are supplied by LVRS Table / TableHeader / TableCellItem.
                            headerDelegate: Component {
                                Rectangle {
                                    required property var modelData
                                    color: LV.Theme.panelBackground04
                                    LV.Label {
                                        id: heading
                                        anchors.fill: parent
                                        anchors.leftMargin: LV.Theme.gap12
                                        anchors.rightMargin: LV.Theme.gap12
                                        verticalAlignment: Text.AlignVCenter
                                        horizontalAlignment: modelData.index === 3 ? Text.AlignRight : Text.AlignLeft
                                        style: description
                                        text: modelData.text
                                        elide: Text.ElideRight
                                    }
                                    Image {
                                        visible: root.sortColumn === modelData.index
                                        anchors.verticalCenter: parent.verticalCenter
                                        x: modelData.index === 3 ? parent.width - heading.contentWidth - width - 20
                                                                  : Math.min(parent.width - width - 12, heading.contentWidth + 20)
                                        width: LV.Theme.iconSm; height: LV.Theme.iconSm
                                        source: LV.Theme.iconPath("generalchevronDown")
                                        rotation: root.descending ? 0 : 180
                                    }
                                    MouseArea { anchors.fill: parent; onClicked: root.setSort(modelData.index) }
                                }
                            }
                            cellDelegate: Component {
                                LV.TableCellItem {
                                    id: cell
                                    required property var modelData
                                    readonly property var entry: root.entries[modelData.rowIndex] || ({})
                                    objectName: "filesCell_" + modelData.rowIndex + "_" + modelData.columnIndex
                                    text: ""
                                    showDivider: false
                                    selected: entry.path === root.selectedPath
                                    selectionColor: LV.Theme.accent
                                    Rectangle { anchors.fill: parent; z: -1; color: cell.modelData.rowIndex % 2 ? LV.Theme.panelBackground04 : "transparent" }
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: LV.Theme.gap12
                                        anchors.rightMargin: LV.Theme.gap12
                                        spacing: LV.Theme.gap8
                                        Image {
                                            visible: cell.modelData.columnIndex === 0
                                            Layout.preferredWidth: LV.Theme.iconSm
                                            Layout.preferredHeight: LV.Theme.iconSm
                                            source: LV.Theme.iconPath(cell.entry.icon || "fileTypestext")
                                        }
                                        LV.Label {
                                            Layout.fillWidth: true
                                            style: body
                                            text: cell.modelData.text || ""
                                            textFormat: Text.PlainText
                                            color: cell.selected || cell.modelData.columnIndex === 0 ? LV.Theme.bodyColor : LV.Theme.descriptionColor
                                            horizontalAlignment: cell.modelData.columnIndex === 3 ? Text.AlignRight : Text.AlignLeft
                                            elide: Text.ElideRight
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        onClicked: function(mouse) {
                                            tableScroll.forceActiveFocus()
                                            root.selectEntry(cell.modelData.rowIndex)
                                            if (mouse.button === Qt.RightButton) root.openMenu(cell.modelData.rowIndex, cell, mouse.x, mouse.y)
                                        }
                                        onDoubleClicked: root.openSelected()
                                        onPressAndHold: function(mouse) { root.openMenu(cell.modelData.rowIndex, cell, mouse.x, mouse.y) }
                                    }
                                }
                            }
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: function(point) {
                                if (point.position.y + tableScroll.contentY > filesTable.implicitHeight)
                                    root.openMenu(-1, tableScroll, point.position.x, point.position.y)
                            }
                        }
                    }
                    GridView {
                        id: filesGrid
                        objectName: "filesBrowserGrid"
                        anchors.fill: parent
                        visible: !root.listMode
                        clip: true
                        model: root.entries
                        cellWidth: width / Math.max(1, Math.floor(width / 160))
                        cellHeight: cellWidth
                        Controls.ScrollBar.vertical: Controls.ScrollBar {}
                        delegate: LV.AbstractButton {
                            id: tile
                            required property var modelData
                            required property int index
                            width: filesGrid.cellWidth - 12
                            height: filesGrid.cellHeight - 12
                            showFocusRing: false
                            cornerRadius: 0
                            background: Rectangle { color: tile.modelData.directory ? "transparent" : LV.Theme.panelBackground04; radius: 0 }
                            contentItem: ColumnLayout {
                                Image { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 48; Layout.preferredHeight: 48; source: LV.Theme.iconPath(tile.modelData.icon) }
                                LV.Label { Layout.fillWidth: true; text: tile.modelData.name; textFormat: Text.PlainText; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; color: tile.modelData.path === root.selectedPath ? LV.Theme.accent : LV.Theme.bodyColor }
                            }
                            onClicked: root.selectEntry(index)
                            onDoubleClicked: root.openSelected()
                            TapHandler { acceptedButtons: Qt.RightButton; onTapped: function(point) { root.openMenu(tile.index, tile, point.position.x, point.position.y) } }
                        }
                    }
                    LV.Label {
                        objectName: "filesEmptyState"
                        visible: root.count === 0
                        anchors.centerIn: parent
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        style: description
                        text: root.loading ? qsTr("Loading files…") : root.query.length ? qsTr("No matching files") : qsTr("This folder is empty")
                    }
                }
            }
            Controls.ScrollView {
                visible: root.showInspector
                Layout.preferredWidth: 296
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: LV.Theme.gap12
                    LV.Label { text: qsTr("Selected file"); style: header2; Layout.bottomMargin: LV.Theme.gap12 }
                    LV.Label { visible: !root.selectedEntry; text: qsTr("Select a file to view its details."); style: description }
                    LV.ListItem {
                        id: resource
                        objectName: "selectedFileResource"
                        visible: root.selectedEntry !== null
                        Layout.fillWidth: true
                        type: LV.ListItem.Resource
                        listBackgroundColor: LV.Theme.panelBackground04
                        label: root.selectedEntry ? root.selectedEntry.name : ""
                        description: root.selectedEntry ? root.selectedEntry.kind : ""
                        previewIconName: root.selectedEntry ? root.selectedEntry.icon : "fileTypestext"
                        dateText: root.selectedEntry ? root.formatDate(root.selectedEntry.modified) : ""
                        metadata1: root.selectedEntry && !root.selectedEntry.directory ? root.formatSize(root.selectedEntry.bytes) : "—"
                        metadata2: root.selectedEntry && root.selectedEntry.resident ? qsTr("Local") : qsTr("Remote")
                        statusText: root.selectedEntry && root.selectedEntry.resident ? qsTr("Available locally") : qsTr("Download on open")
                        showMoreMenu: false
                        primaryAction: ({ text: qsTr("Open"), enabled: !!root.selectedEntry, onTriggered: function() { root.openSelected() } })
                        secondaryAction: ({ text: qsTr("Reveal"), tone: LV.AbstractButton.Default, enabled: !!root.selectedEntry, onTriggered: function() { root.revealSelected() } })
                    }
                    Repeater {
                        model: !root.selectedEntry ? [] : [
                            { label: qsTr("Modified"), value: root.formatDate(root.selectedEntry.modified), icon: "" },
                            { label: qsTr("Created"), value: root.formatDate(root.selectedDetails.created), icon: "" },
                            { label: qsTr("Contents"), value: root.selectedDetails.contents || root.selectedEntry.kind, icon: "" },
                            { label: qsTr("Location"), value: root.path, icon: "nodesfolder" },
                            { label: Qt.platform.os === "osx" ? qsTr("Finder access") : qsTr("File manager access"), value: root.selectedEntry.resident ? qsTr("Available on this device") : qsTr("Download when opened"), icon: "application" }
                        ]
                        LV.ListItem {
                            required property var modelData
                            Layout.fillWidth: true
                            type: LV.ListItem.Navigation
                            label: modelData.label
                            description: modelData.value
                            iconName: modelData.icon
                            showLeadingIcon: modelData.icon.length > 0
                            showValue: false
                            showTrailingIcon: false
                        }
                    }
                }
            }
        }
        LV.Label {
            visible: text.length > 0
            Layout.fillWidth: true
            style: description
            text: root.downloadStatus || fileMenu.actions.errorString || location.errorString
            wrapMode: Text.Wrap
            sizeToContentHeight: true
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: LV.Theme.strokeThin; color: LV.Theme.panelBackground08 }
        RowLayout {
            Layout.fillWidth: true
            LV.Label { objectName: "filesStatus"; style: caption; text: qsTr("%1 items · %2 folders · %3 files · %4 selected").arg(root.count).arg(root.folderCount).arg(root.count - root.folderCount).arg(root.selectedEntry ? 1 : 0) }
            Item { Layout.fillWidth: true }
            LV.Label { visible: root.width >= 760; style: caption; text: root.breadcrumbs.map(function(entry) { return entry.name }).join(" / ") || "Society / Files"; elide: Text.ElideMiddle }
        }
    }
    LV.ContextMenu {
        id: sortMenu
        items: [{ label: qsTr("Name") }, { label: qsTr("Date modified") }, { label: qsTr("Kind") }, { label: qsTr("Size") }]
        onItemTriggered: function(index) { root.setSort(index) }
    }
    FileActionMenu {
        id: fileMenu
        objectName: "filesContextMenu"
        onOpenRequested: root.openSelected()
        onRevealRequested: root.revealSelected()
        onModified: if (root.directoryModel) root.directoryModel.refresh()
    }
    Shortcut { sequences: [StandardKey.Copy]; enabled: root.visible && tableScroll.activeFocus && !!root.selectedEntry; onActivated: { fileMenu.filePath = root.selectedPath; fileMenu.actions.copy() } }
    Shortcut { sequences: [StandardKey.Paste]; enabled: root.visible && tableScroll.activeFocus && fileMenu.actions.canPaste; onActivated: fileMenu.actions.paste(root.path) }
}
