pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV

LV.HStack {
    id: root
    objectName: "dashboardView"
    property var recentFiles: []
    property var historyFiles: []
    property bool loading: false
    property string query: ""
    property string errorString: ""
    property string deviceStatus: ""
    signal sectionRequested(string section)
    signal fileRequested(string path)
    signal revealRequested(string path)
    signal devicesRequested()
    signal featureRequested(string feature)
    signal generateRequested(string prompt, string mediaType, string aspectRatio, int count)
    spacing: 0
    alignment: Qt.AlignTop

    LV.VStack {
        objectName: "dashboardSidebar"
        visible: root.width >= 760
        Layout.preferredWidth: 220
        Layout.fillHeight: true
        spacing: 8
        alignment: Qt.AlignLeft

        LV.VStack {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            spacing: 8
            alignment: Qt.AlignLeft
            LV.Label { text: qsTr("Workspace"); style: description }
            LV.ListItem {
                Layout.fillWidth: true
                detail: ""
                label: qsTr("Home"); iconName: "home"
                onClicked: dashboardScroll.contentItem.contentY = 0
            }
            LV.ListItem {
                Layout.fillWidth: true
                detail: ""
                label: qsTr("Guild"); iconName: "option"
                onClicked: root.featureRequested("Guild")
            }
            LV.ListItem {
                Layout.fillWidth: true
                detail: ""
                label: qsTr("Organization"); iconName: "warehouse"
                onClicked: root.featureRequested("Organization")
            }
            LV.Label { text: qsTr("Locations"); style: description }
            LV.ListItem {
                objectName: "dashboardLocal"
                Layout.fillWidth: true
                detail: ""
                label: qsTr("Local"); iconName: "nodesfolder"
                onClicked: root.sectionRequested("")
            }
            LV.ListItem {
                objectName: "dashboardCloud"
                Layout.fillWidth: true
                detail: ""
                label: qsTr("Cloud"); iconName: "RemoteChanges"
                onClicked: root.devicesRequested()
            }
            LV.ListItem {
                objectName: "dashboardDeleted"
                Layout.fillWidth: true
                detail: ""
                label: qsTr("Deleted"); iconName: "generaldelete"
                onClicked: root.sectionRequested("deleted")
            }
            LV.Spacer { Layout.fillHeight: true }
            LV.ListItem {
                objectName: "dashboardThisDevice"
                Layout.fillWidth: true
                type: LV.ListItem.Action
                label: Qt.platform.os === "osx" ? qsTr("This Mac") : qsTr("This device")
                description: root.deviceStatus
                iconName: "application"
                primaryAction: ({ text: qsTr("View"), tone: LV.AbstractButton.Default })
                onActionTriggered: function(action) {
                    if (action === "primary") root.devicesRequested()
                }
            }
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
        // Keep the list composition while exposing the window's shared material.
        LV.List {
            anchors.fill: parent
            items: []
            interactive: false
            scrollable: false
            footerVisible: false
            minimumListHeight: 0
            backgroundColor: "transparent"
        }
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
                    implicitHeight: page.implicitHeight + 48
                    LV.VStack {
                        id: page
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 24
                        height: implicitHeight
                        spacing: 24
                        QuickGenerate {
                            Layout.fillWidth: true
                            onGenerateRequested: function(prompt, mediaType, aspectRatio, count) {
                                root.generateRequested(prompt, mediaType, aspectRatio, count)
                            }
                        }
                        Repeater {
                            model: [
                                { title: qsTr("Recent files"), key: "files", name: "RecentFiles" },
                                { title: qsTr("Generate History"), key: "generation-history", name: "GenerationHistory" }
                            ]
                            LV.VStack {
                                id: section
                                required property var modelData
                                readonly property var rows: modelData.key === "files" ? root.recentFiles : root.historyFiles
                                Layout.fillWidth: true
                                spacing: 12
                                LV.HStack {
                                    Layout.fillWidth: true
                                    spacing: 12
                                    LV.ListItem {
                                        Layout.fillWidth: true
                                        label: section.modelData.title
                                        detail: ""
                                        showLeadingIcon: false
                                        onClicked: root.sectionRequested(section.modelData.key)
                                    }
                                    LV.LabelButton {
                                        objectName: "viewAll" + section.modelData.name
                                        Layout.preferredWidth: 116
                                        text: qsTr("View all files")
                                        tone: LV.AbstractButton.Borderless
                                        onClicked: root.sectionRequested(section.modelData.key)
                                    }
                                }
                                GridLayout {
                                    id: cards
                                    Layout.fillWidth: true
                                    columns: Math.max(1, Math.min(3, Math.floor(width / 340)))
                                    columnSpacing: 0
                                    rowSpacing: 12
                                    Repeater {
                                        model: section.rows
                                        LV.ListItem {
                                            id: card
                                            required property var modelData
                                            objectName: "dashboardResource"
                                            Layout.fillWidth: true
                                            Layout.preferredWidth: 1
                                            Layout.minimumWidth: 0
                                            type: LV.ListItem.Resource
                                            label: modelData.name
                                            description: modelData.description
                                            previewIconName: modelData.iconName
                                            dateText: modelData.dateText
                                            metadata1: modelData.metadata1
                                            metadata2: modelData.metadata2
                                            statusText: qsTr("Available locally")
                                            secondaryAction: ({ text: qsTr("Reveal"), tone: LV.AbstractButton.Default })
                                            primaryAction: ({ text: qsTr("Open"), tone: LV.AbstractButton.Primary })
                                            moreMenu: ({ text: qsTr("More"), items: [qsTr("Open"), qsTr("Reveal in Storage")] })
                                            onActionTriggered: function(action, payload) {
                                                if (action === "primary" || (action === "menuItem" && payload.index === 0))
                                                    root.fileRequested(card.modelData.path)
                                                else if (action === "secondary" || (action === "menuItem" && payload.index === 1))
                                                    root.revealRequested(card.modelData.folderPath)
                                            }
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
}
