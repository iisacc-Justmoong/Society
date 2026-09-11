pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import LVRS 1.0 as LV
import Society

Controls.Popup {
    id: panel
    required property NetworkDriveController network
    signal preferencesRequested()
    signal accountRequested()
    signal pairingRequested()
    width: Math.min(parent.width - 32, 680)
    height: Math.min(parent.height - 32, 620)
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    modal: true
    padding: 20
    background: Rectangle { color: LV.Theme.panelBackground06; radius: 12 }
    property string selectedPath: ""
    FileDialog {
        id: destination
        title: qsTr("Save file from your device")
        fileMode: FileDialog.SaveFile
        onAccepted: panel.network.download(panel.selectedPath, selectedFile)
    }
    Controls.ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
        width: scroll.availableWidth
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            LV.Label { text: qsTr("Your devices"); style: header; Layout.fillWidth: true }
            LV.PushButton { text: qsTr("Close"); onClicked: panel.close() }
        }
        RowLayout {
            Layout.fillWidth: true
            LV.Label {
                objectName: "networkModeLabel"
                Layout.fillWidth: true
                text: panel.network.mode === NetworkDriveController.HostMode ? qsTr("Host mode") : qsTr("Client mode")
            }
            LV.PushButton {
                objectName: "networkPreferences"
                visible: panel.network.hostModeAvailable
                text: qsTr("Preferences…")
                tone: LV.AbstractButton.Default
                onClicked: panel.preferencesRequested()
            }
        }
        LV.Label {
            Layout.fillWidth: true
            text: panel.network.mode === NetworkDriveController.HostMode
                ? (panel.network.containerPath.length > 0
                    ? qsTr("Share this container's Files with your other devices when connected.")
                    : qsTr("Open a Society container to host files."))
                : qsTr("Access Files on your other devices.")
            wrapMode: Text.Wrap
        }
        LV.Label {
            Layout.fillWidth: true
            text: qsTr("Devices signed in to the same iisacc account pair automatically on your Wi-Fi or LAN and sync their containers.")
            wrapMode: Text.Wrap
            sizeToContentHeight: true
        }
        LV.PushButton {
            objectName: "networkPairing"
            Layout.fillWidth: true
            text: panel.network.hostModeAvailable ? qsTr("Pair a device") : qsTr("Pair desktop")
            onClicked: panel.pairingRequested()
        }
        LV.PushButton {
            objectName: "networkAccount"
            Layout.fillWidth: true
            text: panel.network.signedIn ? qsTr("Manage iisacc account") : qsTr("Sign in to iisacc")
            onClicked: panel.accountRequested()
        }
        RowLayout {
            visible: panel.network.connected
            LV.PushButton {
                text: qsTr("Refresh")
                enabled: !panel.network.busy
                onClicked: {
                    panel.network.refresh()
                }
            }
            LV.PushButton { text: qsTr("Disconnect"); enabled: panel.network.connected; onClicked: panel.network.disconnectSession() }
        }
        LV.Label {
            Layout.fillWidth: true
            text: panel.network.authError || panel.network.status
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }
        LV.Label {
            objectName: "containerSyncStatus"
            Layout.fillWidth: true
            visible: panel.network.signedIn && panel.network.connected
            text: panel.network.synchronizationStatus
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            sizeToContentHeight: true
        }
        RowLayout {
            Layout.fillWidth: true
            LV.Label { Layout.fillWidth: true; text: panel.network.currentPath.length > 0 ? panel.network.currentPath : qsTr("Files"); elide: Text.ElideMiddle }
            LV.Label { text: panel.network.transport === "local" ? qsTr("Local network") : panel.network.transport === "remote" ? qsTr("Remote") : "" }
            LV.PushButton {
                text: qsTr("Up"); enabled: !panel.network.busy && panel.network.currentPath.length > 0
                onClicked: panel.network.browse(panel.network.currentHost, panel.network.currentPath.split("/").slice(0, -1).join("/"))
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                Repeater {
                    model: panel.network.hosts
                    LV.PushButton {
                        required property var modelData
                        Layout.fillWidth: true
                        text: modelData.name
                        enabled: !panel.network.busy
                        onClicked: panel.network.browse(modelData.peerId)
                    }
                }
                Repeater {
                    model: panel.network.entries
                    LV.AbstractButton {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 36
                        text: (modelData.directory ? "▸ " : "↓ ") + modelData.name
                        enabled: !panel.network.busy
                        onClicked: {
                            const path = (panel.network.currentPath.length ? panel.network.currentPath + "/" : "") + modelData.name
                            if (modelData.directory) panel.network.browse(panel.network.currentHost, path)
                            else { panel.selectedPath = path; destination.open() }
                        }
                    }
                }
                LV.PushButton {
                    text: qsTr("Load more"); visible: panel.network.nextCursor.length > 0; enabled: !panel.network.busy
                    onClicked: panel.network.browse(panel.network.currentHost, panel.network.currentPath, panel.network.nextCursor)
                }
            }
        }
    }
}
}
