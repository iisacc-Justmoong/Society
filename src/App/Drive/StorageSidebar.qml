pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society

Rectangle {
    id: root
    objectName: "driveSidebar"
    required property StorageNavigation navigation
    property bool touchNavigation: false
    property string objectNamePrefix: ""
    signal activated()
    implicitWidth: 228
    color: LV.Theme.panelBackground04

    component NavigationGroup: LV.VStack {
        id: group
        required property string key
        required property string title
        required property var entries
        required property string emptyText
        spacing: 8
        Layout.fillWidth: true
        LV.Label {
            objectName: root.objectNamePrefix + "storageHeading" + group.key
            Layout.fillWidth: true
            Layout.preferredHeight: 11
            style: caption
            text: group.title
            textFormat: Text.PlainText
        }
        Repeater {
            model: group.entries
            LV.ListItem {
                required property var modelData
                objectName: root.objectNamePrefix + (group.key === "storage" ? "storageSection" : "storageTarget" + group.key) + modelData.id
                Layout.fillWidth: true
                Layout.preferredHeight: root.touchNavigation ? 44 : 32
                standardItemHeight: root.touchNavigation ? 44 : 32
                type: LV.ListItem.Navigation
                label: modelData.name
                iconName: modelData.icon
                detail: ""
                description: ""
                showDescription: false
                showValue: false
                showTrailingIcon: false
                selected: group.key === "storage" && root.navigation.selectedSection === modelData.id
                selectedBackgroundColor: LV.Theme.panelBackground12
                activeFocusOnTab: true
                Accessible.name: label
                Accessible.description: modelData.status || ""
                Accessible.selected: selected
                onClicked: { root.navigation.activate(group.key, modelData.id); root.activated() }
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
            objectName: root.objectNamePrefix + "storageEmpty" + group.key
            visible: group.entries.length === 0
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.preferredHeight: 32
            style: caption
            text: group.emptyText
            textFormat: Text.PlainText
            verticalAlignment: Text.AlignVCenter
        }
    }

    Flickable {
        id: viewport
        objectName: root.objectNamePrefix + "storageSidebarScroll"
        anchors.fill: parent
        anchors.margins: 12
        contentWidth: width
        contentHeight: Math.max(height, groups.implicitHeight + 9)
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
                key: "storage"
                title: qsTr("My storage")
                entries: root.navigation.sections
                emptyText: ""
            }
            NavigationGroup {
                key: "devices"
                title: qsTr("Other devices")
                entries: root.navigation.devices
                emptyText: qsTr("No other devices")
            }
            NavigationGroup {
                key: "guild"
                title: qsTr("Guild")
                entries: root.navigation.guilds
                emptyText: qsTr("No guilds")
            }
            NavigationGroup {
                key: "organization"
                title: qsTr("Organization")
                entries: root.navigation.organizations
                emptyText: qsTr("No organizations")
            }
        }
        Rectangle {
            width: viewport.width
            height: 1
            y: viewport.contentHeight - height
            color: LV.Theme.panelBackground08
        }
    }
}
