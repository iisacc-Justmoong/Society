pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society

Item {
    id: root
    objectName: "modelsView"
    required property StorageModels catalog
    property bool touchNavigation: false
    readonly property int contentInset: touchNavigation && width < 760 ? 16 : 24
    property bool importing: false
    property bool importEnabled: true
    property string importStatus: ""
    property string importError: ""
    property string selectedPath: ""
    property var savedPositions: null
    readonly property var sections: [
        { key: "image", title: qsTr("Image models") },
        { key: "video", title: qsTr("Video models") },
        { key: "audio", title: qsTr("Audio models") },
        { key: "language", title: qsTr("Language models") }
    ]
    signal importRequested()
    signal cancelImportRequested()
    signal fileRequested(string path)
    signal folderRequested(string path)
    signal browseFoldersRequested()

    component ModelCategory: LV.VStack {
        id: category
        required property var modelData
        readonly property var rows: root.catalog.groups[modelData.key] || []
        property alias list: cards
        objectName: "modelCategory" + modelData.key
        Layout.fillWidth: true
        spacing: 24
        LV.HStack {
            objectName: "modelHeading" + category.modelData.key
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            spacing: 12
            LV.Label {
                Layout.fillWidth: true
                style: header
                text: category.modelData.title
                elide: Text.ElideRight
            }
            LV.LabelButton {
                objectName: "importModels" + category.modelData.key
                Layout.minimumHeight: root.touchNavigation ? 44 : 0
                text: qsTr("Import models…")
                tone: LV.AbstractButton.Default
                enabled: root.importEnabled
                Accessible.name: qsTr("Import models for %1").arg(category.modelData.title)
                onClicked: root.importRequested()
            }
        }
        Item {
            Layout.fillWidth: true
            implicitHeight: 280
            ListView {
                id: cards
                objectName: "modelCards" + category.modelData.key
                anchors.fill: parent
                orientation: ListView.Horizontal
                model: category.rows
                spacing: 8
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                activeFocusOnTab: true
                keyNavigationEnabled: true
                currentIndex: -1
                function selectCurrent() {
                    if (currentIndex >= 0 && currentIndex < count)
                        root.selectedPath = category.rows[currentIndex].path
                }
                Keys.onReturnPressed: selectCurrent()
                Keys.onEnterPressed: selectCurrent()
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                        currentIndex = event.key === Qt.Key_Home ? 0 : count - 1
                        positionViewAtIndex(currentIndex, ListView.Contain)
                        selectCurrent()
                        event.accepted = true
                    }
                }
                Controls.ScrollBar.horizontal: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
                WheelHandler {
                    target: null
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    orientation: Qt.Horizontal
                    onWheel: function(event) {
                        const delta = event.pixelDelta.x !== 0 ? event.pixelDelta.x : event.angleDelta.x
                        if (delta === 0) { event.accepted = false; return }
                        cards.contentX = Math.max(0, Math.min(cards.contentX - delta, Math.max(0, cards.contentWidth - cards.width)))
                        event.accepted = true
                    }
                }
                delegate: LV.Card {
                    id: card
                    required property int index
                    required property var modelData
                    objectName: "modelCard" + category.modelData.key + index
                    type: LV.Card.Model
                    width: 256
                    height: 280
                    title: modelData.name
                    description: modelData.relativePath
                    showDescription: false
                    showStatus: false
                    showAction: false
                    selectable: false
                    selected: root.selectedPath === modelData.path
                    Accessible.name: modelData.name
                    footnote: root.catalog.requestedPath === modelData.path && root.catalog.downloadStatus.length > 0
                        ? root.catalog.downloadStatus : modelData.sizeText
                    rows: [
                        { label: qsTr("Architecture"), value: modelData.architecture },
                        { label: qsTr("Format"), value: modelData.format },
                        { label: qsTr("Precision"), value: modelData.precision }
                    ]
                    onClicked: {
                        cards.currentIndex = index
                        root.selectedPath = modelData.path
                        if (modelData.available === false) root.catalog.activatePath(modelData.path)
                    }
                    onActiveFocusChanged: if (activeFocus) {
                        cards.currentIndex = index
                        cards.positionViewAtIndex(index, ListView.Contain)
                    }
                    onMenuRequested: root.openMenu(modelData, card)
                    TapHandler {
                        acceptedDevices: PointerDevice.TouchScreen
                        onLongPressed: root.openMenu(card.modelData, card)
                    }
                    TapHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        acceptedButtons: Qt.RightButton
                        onTapped: root.openMenu(card.modelData, card)
                    }
                }
            }
            LV.ListItem {
                anchors.left: parent.left
                anchors.right: parent.right
                visible: category.rows.length === 0
                type: LV.ListItem.Navigation
                showLeadingIcon: false
                showTrailingIcon: false
                showValue: false
                description: ""
                detail: ""
                label: root.catalog.loading ? qsTr("Reading models…") : qsTr("No models in this category")
            }
        }
    }

    function rememberPositions() {
        const positions = []
        for (let i = 0; i < categories.count; ++i)
            positions.push((categories.itemAt(i) as ModelCategory).list.contentX)
        savedPositions = positions
    }
    function restorePositions() {
        let found = false
        for (let i = 0; i < categories.count; ++i) {
            const section = categories.itemAt(i) as ModelCategory
            section.list.forceLayout()
            if (savedPositions && i < savedPositions.length)
                section.list.contentX = Math.max(0, Math.min(savedPositions[i], Math.max(0, section.list.contentWidth - section.list.width)))
            for (let j = 0; j < section.rows.length; ++j)
                if (section.rows[j].path === selectedPath) found = true
        }
        if (!found) selectedPath = ""
        savedPositions = null
    }
    function openMenu(model, card) {
        selectedPath = model.path
        modelMenu.modelEntry = model
        modelMenu.openFor(card, 0, 36)
    }
    onVisibleChanged: {
        modelMenu.close()
        if (visible) catalog.refresh()
    }
    Connections {
        target: root.catalog
        function onModelsAboutToChange() { root.rememberPositions(); modelMenu.close() }
        function onModelsChanged() { Qt.callLater(root.restorePositions) }
        function onDirectoryChanged() {
            root.selectedPath = ""
            root.savedPositions = [0, 0, 0, 0]
            modelsScroll.contentItem.contentY = 0
        }
    }
    Controls.ScrollView {
        id: modelsScroll
        objectName: "modelsScroll"
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: modelsContent.height
        clip: true
        Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
        Item {
            id: modelsContent
            width: modelsScroll.availableWidth
            height: page.implicitHeight + root.contentInset * 2
            LV.VStack {
                id: page
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: root.contentInset
                height: implicitHeight
                spacing: 24
                LV.VStack {
                    id: modelSections
                    Layout.fillWidth: true
                    spacing: 24
                Repeater {
                    id: categories
                    model: root.sections
                    delegate: ModelCategory {}
                }
                }
                Item {
                    implicitHeight: Math.max(1, root.height - modelSections.implicitHeight - modelFooter.implicitHeight
                        - (modelError.visible ? modelError.implicitHeight + 24 : 0) - 48 - 72 - 1)
                }
                LV.Label {
                    id: modelError
                    visible: root.catalog.errorString.length > 0 || root.importError.length > 0
                    Layout.fillWidth: true
                    style: description
                    text: root.importError || root.catalog.errorString
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    sizeToContentHeight: true
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: LV.Theme.panelBackground08 }
                LV.HStack {
                    id: modelFooter
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.touchNavigation ? 44 : 16
                    spacing: 12
                    LV.Label {
                        objectName: "modelsStatus"
                        Layout.fillWidth: true
                        style: caption
                        text: root.importing ? root.importStatus : root.catalog.downloadStatus || (root.catalog.uncategorizedCount > 0
                            ? qsTr("%1 models · %2 need a category").arg(root.catalog.count).arg(root.catalog.uncategorizedCount)
                            : qsTr("%1 models").arg(root.catalog.count))
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                    LV.LabelButton {
                        visible: root.importing
                        text: qsTr("Cancel")
                        onClicked: root.cancelImportRequested()
                    }
                    LV.Label {
                        objectName: "browseModelFolders"
                        text: qsTr("Society / Models")
                        style: caption
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Browse model folders, including uncategorized models")
                        Keys.onReturnPressed: root.browseFoldersRequested()
                        TapHandler { onTapped: root.browseFoldersRequested() }
                    }
                }
            }
        }
    }
    LV.ContextMenu {
        id: modelMenu
        objectName: "modelCardMenu"
        property var modelEntry: ({})
        showIconSlot: false
        items: [qsTr("Open"), qsTr("Show in folder")]
        onItemTriggered: function(index) {
            if (index === 0) root.fileRequested(modelEntry.path)
            else root.folderRequested(modelEntry.folderPath)
        }
    }
}
