pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society

LV.HStack {
    id: root
    objectName: "dashboardView"
    property bool touchNavigation: false
    readonly property int contentInset: touchNavigation && width < 760 ? 16 : 24
    function goHome() { dashboardScroll.contentItem.contentY = 0 }
    required property DashboardFiles viewModel
    readonly property var recentFiles: viewModel.recentFiles
    readonly property var historyFiles: viewModel.generationHistory
    readonly property bool loading: viewModel.loading
    readonly property string query: viewModel.query
    property string driveError: ""
    readonly property string errorString: viewModel.errorString || driveError
    property bool initialized: false
    property string deviceStatus: ""
    signal sectionRequested(string section)
    signal fileRequested(string path)
    signal revealRequested(string path)
    signal devicesRequested()
    signal featureRequested(string feature)
    signal generateRequested(string prompt, string mediaType, string aspectRatio, int count)
    spacing: 0
    alignment: Qt.AlignTop
    Component.onCompleted: {
        initialized = true
        viewModel.refresh()
    }
    onVisibleChanged: {
        if (visible && initialized) viewModel.refresh()
        if (!visible) fileMenu.close()
    }

    function openFileMenu(file, card) {
        fileMenu.filePath = file.path
        fileMenu.folderPath = file.folderPath
        fileMenu.openFor(card, 0, card.height + LV.Theme.gap2)
    }

    DashboardSidebar {
        objectName: "dashboardSidebar"
        visible: root.width >= 760
        Layout.preferredWidth: 220
        Layout.fillHeight: true
        touchNavigation: root.touchNavigation
        deviceStatus: root.deviceStatus
        onHomeRequested: root.goHome()
        onSectionRequested: function(section) { root.sectionRequested(section) }
        onDevicesRequested: root.devicesRequested()
        onFeatureRequested: function(feature) { root.featureRequested(feature) }
    }

    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Controls.ScrollView {
            id: dashboardScroll
            objectName: "dashboardScroll"
            anchors.fill: parent
            contentWidth: availableWidth
            clip: true
            Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff

            LV.VStack {
                width: dashboardScroll.availableWidth
                spacing: 24
                // The enclosing item supplies Figma's 24 px content inset.
                Item {
                    Layout.fillWidth: true
                    implicitHeight: page.implicitHeight + root.contentInset * 2
                    LV.VStack {
                        id: page
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: root.contentInset
                        height: implicitHeight
                        spacing: 24
                        QuickGenerate {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            onGenerateRequested: function(prompt, mediaType, aspectRatio, count) {
                                root.generateRequested(prompt, mediaType, aspectRatio, count)
                            }
                        }
                        Repeater {
                            model: [
                                { title: qsTr("Recent files"), key: "files", name: "RecentFiles" },
                                { title: qsTr("Generate history"), key: "generation-history", name: "GenerationHistory" }
                            ]
                            LV.VStack {
                                id: section
                                required property var modelData
                                readonly property var rows: modelData.key === "files" ? root.recentFiles : root.historyFiles
                                Layout.fillWidth: true
                                spacing: 12
                                LV.HStack {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: LV.Theme.controlHeightSm
                                    spacing: 12
                                    LV.Label {
                                        objectName: "dashboard" + section.modelData.name + "Title"
                                        Layout.fillWidth: true
                                        Layout.leftMargin: LV.Theme.gap4
                                        Layout.preferredHeight: LV.Theme.scaleMetric(17)
                                        text: section.modelData.title
                                        style: body
                                        lineHeight: LV.Theme.scaleMetric(13)
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }
                                    LV.LabelButton {
                                        objectName: "viewAll" + section.modelData.name
                                        Layout.preferredWidth: 116
                                        text: qsTr("View all files")
                                        tone: LV.AbstractButton.Borderless
                                        onClicked: root.sectionRequested(section.modelData.key)
                                    }
                                }
                                ListView {
                                    id: cards
                                    objectName: "dashboard" + section.modelData.name + "Cards"
                                    Layout.fillWidth: true
                                    implicitHeight: LV.Theme.scaleMetric(160)
                                    visible: count > 0
                                    model: section.rows
                                    orientation: ListView.Horizontal
                                    spacing: LV.Theme.gap8
                                    clip: true
                                    boundsBehavior: Flickable.StopAtBounds
                                    activeFocusOnTab: true
                                    keyNavigationEnabled: true
                                    Keys.onReturnPressed: openCurrentFile()
                                    Keys.onEnterPressed: openCurrentFile()
                                    Keys.onPressed: function(event) {
                                        if (event.key === Qt.Key_Home) {
                                            currentIndex = 0
                                            positionViewAtBeginning()
                                            event.accepted = true
                                        } else if (event.key === Qt.Key_End) {
                                            currentIndex = count - 1
                                            positionViewAtEnd()
                                            event.accepted = true
                                        }
                                    }
                                    function openCurrentFile() {
                                        if (currentIndex >= 0 && currentIndex < count)
                                            root.fileRequested(section.rows[currentIndex].path)
                                    }
                                    Controls.ScrollBar.horizontal: Controls.ScrollBar {
                                        policy: Controls.ScrollBar.AsNeeded
                                    }
                                    delegate: LV.Card {
                                        id: card
                                        required property int index
                                        required property var modelData
                                        objectName: "dashboard" + section.modelData.name + "Card" + index
                                        type: LV.Card.File
                                        size: LV.Card.Small
                                        detail: LV.Card.Brief
                                        width: LV.Theme.scaleMetric(140)
                                        height: LV.Theme.scaleMetric(160)
                                        filename: modelData.name
                                        description: modelData.description || ""
                                        metadata: modelData.dateText
                                        previewSource: modelData.previewSource || ""
                                        selectable: false
                                        showMenu: hovered || visualFocus
                                        onClicked: {
                                            cards.currentIndex = index
                                            root.fileRequested(modelData.path)
                                        }
                                        onActiveFocusChanged: if (activeFocus) {
                                            cards.currentIndex = index
                                            cards.positionViewAtIndex(index, ListView.Contain)
                                        }
                                        onMenuRequested: root.openFileMenu(modelData, card)
                                        TapHandler {
                                            acceptedDevices: PointerDevice.TouchScreen
                                            onLongPressed: root.openFileMenu(card.modelData, card)
                                        }
                                        TapHandler {
                                            acceptedButtons: Qt.RightButton
                                            onTapped: root.openFileMenu(card.modelData, card)
                                        }
                                    }
                                }
                                LV.ListItem {
                                    objectName: "empty" + section.modelData.name
                                    visible: section.rows.length === 0
                                    Layout.fillWidth: true
                                    type: LV.ListItem.Detail
                                    showLeadingIcon: false
                                    label: root.loading ? qsTr("Reading Society…") : root.query.trim().length > 0
                                        ? qsTr("No matching files") : section.modelData.key === "files"
                                            ? qsTr("No recent files") : qsTr("No generated images yet")
                                    detail: root.query.trim().length > 0 ? qsTr("Try another file name.")
                                        : qsTr("Files saved in Society will appear here.")
                                    showBookmark: false; showDate: false; showFolders: false; showTags: false
                                }
                            }
                        }
                        LV.Label {
                            visible: root.errorString.length > 0
                            Layout.fillWidth: true
                            style: description
                            text: root.errorString
                            textFormat: Text.PlainText
                            wrapMode: Text.WordWrap
                            sizeToContentHeight: true
                        }
                    }
                }
            }
        }
    }
    LV.ContextMenu {
        id: fileMenu
        objectName: "dashboardFileMenu"
        property string filePath: ""
        property string folderPath: ""
        showIconSlot: false
        items: [qsTr("Open"), qsTr("Reveal in Storage")]
        onItemTriggered: function(index) {
            if (index === 0) root.fileRequested(filePath)
            else if (index === 1) root.revealRequested(folderPath)
        }
    }
}
