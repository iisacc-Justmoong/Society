pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV

Item {
    id: root
    property bool touchNavigation: false
    property string objectNamePrefix: ""
    property string deviceStatus: ""
    signal homeRequested()
    signal sectionRequested(string section)
    signal devicesRequested()
    signal featureRequested(string feature)
    implicitWidth: 220
    Controls.ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
        LV.VStack {
            width: scroll.availableWidth - 24
            x: 12; y: 12
            height: Math.max(implicitHeight, scroll.availableHeight - 24)
            spacing: 8
            alignment: Qt.AlignLeft
            LV.Label { text: qsTr("Workspace"); style: description }
            LV.ListItem {
                Layout.fillWidth: true
                Layout.minimumHeight: root.touchNavigation ? 44 : 0
                detail: ""
                label: qsTr("Home"); iconName: "home"
                onClicked: root.homeRequested()
            }
            LV.ListItem {
                Layout.fillWidth: true
                Layout.minimumHeight: root.touchNavigation ? 44 : 0
                detail: ""
                label: qsTr("Guild"); iconName: "option"
                onClicked: root.featureRequested("Guild")
            }
            LV.ListItem {
                Layout.fillWidth: true
                Layout.minimumHeight: root.touchNavigation ? 44 : 0
                detail: ""
                label: qsTr("Organization"); iconName: "warehouse"
                onClicked: root.featureRequested("Organization")
            }
            LV.Label { text: qsTr("Locations"); style: description }
            LV.ListItem {
                objectName: root.objectNamePrefix + "dashboardLocal"
                Layout.fillWidth: true
                Layout.minimumHeight: root.touchNavigation ? 44 : 0
                detail: ""
                label: qsTr("Local"); iconName: "nodesfolder"
                onClicked: root.sectionRequested("")
            }
            LV.ListItem {
                objectName: root.objectNamePrefix + "dashboardCloud"
                Layout.fillWidth: true
                Layout.minimumHeight: root.touchNavigation ? 44 : 0
                detail: ""
                label: qsTr("Cloud"); iconName: "RemoteChanges"
                onClicked: root.devicesRequested()
            }
            LV.ListItem {
                objectName: root.objectNamePrefix + "dashboardDeleted"
                Layout.fillWidth: true
                Layout.minimumHeight: root.touchNavigation ? 44 : 0
                detail: ""
                label: qsTr("Deleted"); iconName: "generaldelete"
                onClicked: root.sectionRequested("deleted")
            }
            LV.Spacer { Layout.fillHeight: true }
            LV.ListItem {
                objectName: root.objectNamePrefix + "dashboardThisDevice"
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
}
