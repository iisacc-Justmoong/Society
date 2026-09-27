pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import LVRS 1.0 as LV
import Society
import "../Environment"

Controls.ScrollView {
    id: root
    objectName: "browseView"
    required property NetworkDriveController network
    signal devicesRequested()
    property string downloadPath: ""
    clip: true
    contentWidth: availableWidth
    FileDialog { id: destination; title: qsTr("Save file from your device"); fileMode: FileDialog.SaveFile; onAccepted: root.network.download(root.downloadPath, selectedFile) }
    ColumnLayout {
        width: root.availableWidth
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 24
            spacing: 16
            LV.Label { text: qsTr("Browse"); style: title }
            LV.Label { Layout.fillWidth: true; text: qsTr("Browse files on your connected devices."); style: description; wrapMode: Text.Wrap; sizeToContentHeight: true }
            EnvironmentSection {
                title: qsTr("Connected devices")
                Repeater {
                    model: root.network.hosts
                    EnvironmentRow {
                        required property var modelData
                        label: modelData.name
                        value: qsTr("Browse")
                        onClicked: root.network.browse(modelData.peerId)
                    }
                }
                EnvironmentRow { visible: root.network.hosts.length === 0; label: qsTr("No connected devices"); description: qsTr("Manage devices in Environment to connect a host."); value: qsTr("Devices"); onClicked: root.devicesRequested() }
            }
            EnvironmentSection {
                visible: root.network.currentHost.length > 0
                title: root.network.currentPath || qsTr("Files")
                LV.LabelButton { text: qsTr("Up"); enabled: !root.network.busy && root.network.currentPath.length > 0; onClicked: root.network.browse(root.network.currentHost, root.network.currentPath.split("/").slice(0, -1).join("/")) }
                Repeater {
                    model: root.network.entries
                    EnvironmentRow {
                        required property var modelData
                        label: modelData.name
                        value: modelData.directory ? qsTr("Open") : qsTr("Download")
                        enabled: !root.network.busy
                        onClicked: {
                            const path = (root.network.currentPath ? root.network.currentPath + "/" : "") + modelData.name
                            if (modelData.directory) root.network.browse(root.network.currentHost, path)
                            else { root.downloadPath = path; destination.open() }
                        }
                    }
                }
                LV.LabelButton { visible: root.network.nextCursor.length > 0; text: qsTr("Load more"); enabled: !root.network.busy; onClicked: root.network.browse(root.network.currentHost, root.network.currentPath, root.network.nextCursor) }
            }
            LV.Label { Layout.fillWidth: true; text: root.network.authError || root.network.status; style: caption; wrapMode: Text.Wrap; sizeToContentHeight: true }
        }
    }
}
