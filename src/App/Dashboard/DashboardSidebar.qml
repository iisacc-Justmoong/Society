pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV

Rectangle {
    id: root
    property bool touchNavigation: false
    property string objectNamePrefix: ""
    property var guilds: []
    property var organizations: []
    signal homeRequested()
    signal calendarRequested()
    signal activityRequested()
    signal featureRequested(string feature)
    signal workspaceRequested(string kind, string id)
    implicitWidth: 204
    color: LV.Theme.panelBackground04

    readonly property var workspaceEntries: [
        { id: "home", name: qsTr("Home"), icon: "home", extent: 16 },
        { id: "projects", name: qsTr("Projects"), icon: "projects", extent: 18 },
        { id: "calendar", name: qsTr("Calendar"), icon: "calendar", extent: 18 },
        { id: "activity", name: qsTr("Activity"), icon: "activity", extent: 18 },
        { id: "people", name: qsTr("People"), icon: "people", extent: 16 }
    ]

    function activateWorkspace(key: string): void {
        if (key === "home") homeRequested()
        else if (key === "calendar") calendarRequested()
        else if (key === "activity") activityRequested()
        else if (key === "projects") featureRequested(qsTr("Projects"))
        else if (key === "people") featureRequested(qsTr("People"))
    }

    component NavigationGroup: LV.VStack {
        id: group
        required property string key
        required property string title
        required property var entries
        property string emptyText: ""
        Layout.fillWidth: true
        spacing: 8
        LV.Label {
            objectName: root.objectNamePrefix + "dashboardHeading" + group.key
            Layout.fillWidth: true
            Layout.preferredHeight: 12
            style: description
            text: group.title
            textFormat: Text.PlainText
        }
        LV.VStack {
            Layout.fillWidth: true
            spacing: 0
            Repeater {
                model: group.entries
                LV.ListItem {
                    id: row
                    required property var modelData
                    readonly property int glyphExtent: group.key === "workspace" ? modelData.extent : 18
                    objectName: root.objectNamePrefix + "dashboardItem" + group.key + "_" + modelData.id
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: 32
                    Layout.minimumHeight: 32
                    Layout.maximumHeight: 32
                    standardItemHeight: 32
                    detailVerticalPadding: 7
                    type: LV.ListItem.Navigation
                    label: modelData.name
                    detail: ""
                    description: ""
                    showDescription: false
                    showValue: false
                    showTrailingIcon: true
                    iconSize: 18
                    iconSource: Qt.resolvedUrl("icons/" + (group.key === "workspace" ? modelData.icon : group.key) + ".svg")
                    leadingComponent: Component {
                        Item {
                            property var listItem: null
                            implicitWidth: 18
                            implicitHeight: 18
                            Image {
                                width: row.glyphExtent
                                height: row.glyphExtent
                                source: row.iconSource
                                sourceSize.width: width * Screen.devicePixelRatio
                                sourceSize.height: height * Screen.devicePixelRatio
                                smooth: true
                            }
                        }
                    }
                    activeFocusOnTab: true
                    Accessible.name: label
                    onClicked: {
                        if (group.key === "workspace") root.activateWorkspace(modelData.id)
                        else root.workspaceRequested(group.key, modelData.id)
                    }
                    onActiveFocusChanged: {
                        if (!activeFocus) return
                        const top = mapToItem(viewport.contentItem, 0, 0).y
                        if (top < viewport.contentY) viewport.contentY = top
                        else if (top + height > viewport.contentY + viewport.height)
                            viewport.contentY = top + height - viewport.height
                    }
                }
            }
            LV.Label {
                objectName: root.objectNamePrefix + "dashboardEmpty" + group.key
                visible: group.entries.length === 0
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.preferredHeight: 32
                style: description
                text: group.emptyText
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    Flickable {
        id: viewport
        objectName: root.objectNamePrefix + "dashboardSidebarScroll"
        anchors.fill: parent
        anchors.margins: 12
        contentWidth: width
        contentHeight: Math.max(height, groups.implicitHeight)
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        function clampScroll() { contentY = Math.max(0, Math.min(contentY, contentHeight - height)) }
        onContentHeightChanged: clampScroll()
        onHeightChanged: clampScroll()
        Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
        LV.VStack {
            id: groups
            width: viewport.width
            spacing: 8
            NavigationGroup {
                key: "workspace"
                title: qsTr("Workspace")
                entries: root.workspaceEntries
            }
            NavigationGroup {
                key: "guild"
                title: qsTr("Guilds")
                entries: root.guilds
                emptyText: qsTr("No guilds")
            }
            NavigationGroup {
                key: "organization"
                title: qsTr("Organization")
                entries: root.organizations
                emptyText: qsTr("No organizations")
            }
        }
    }
}
