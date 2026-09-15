pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import LVRS 1.0 as LV
import Society

LV.Sheet {
    id: panel
    required property NetworkDriveController network
    signal preferencesRequested()
    signal accountRequested()
    signal pairingRequested()
    presentation: network.hostModeAvailable ? LV.Sheet.Desktop : LV.Sheet.Mobile
    detent: LV.Sheet.Large
    preferredWidth: 680
    preferredHeight: 620
    contentPadding: 20
    showHeader: false
    scrollContent: false
    property string selectedPath: ""
    property string selectedDeviceId: ""
    property string selectedDeviceName: ""
    property bool selectionPending: false
    readonly property string selectedPeerId: {
        for (const host of network.hosts) {
            const deviceId = host.metadata && host.metadata.deviceId ? host.metadata.deviceId : host.peerId
            if (deviceId === selectedDeviceId) return host.peerId
        }
        return ""
    }
    readonly property bool selectedFilesVisible: selectedDeviceId.length === 0
        || (selectedPeerId.length > 0 && network.currentHost === selectedPeerId)
    onSelectedPeerIdChanged: {
        selectionPending = selectedPeerId.length > 0
        browseSelectedDevice()
    }
    onOpened: browseSelectedDevice()
    onClosed: { selectionPending = false; /* qmllint disable missing-property */ Qt.inputMethod.hide() /* qmllint enable missing-property */ }
    Connections {
        target: panel.network
        function onStateChanged() { panel.browseSelectedDevice() }
    }
    function browseSelectedDevice() {
        if (!visible || !selectionPending || selectedPeerId.length === 0 || network.busy) return
        selectionPending = false
        network.browse(selectedPeerId)
    }
    function clearDeviceSelection() {
        selectedDeviceId = ""
        selectedDeviceName = ""
        selectedPath = ""
        selectionPending = false
    }
    function selectDevice(id, name, peerId) {
        const sameDevice = selectedDeviceId === id
        selectedDeviceName = name
        selectedDeviceId = id
        selectedPath = ""
        if (sameDevice) selectionPending = peerId.length > 0 && selectedPeerId === peerId
        browseSelectedDevice()
    }
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
            LV.Label {
                objectName: "networkDeviceTitle"
                text: panel.selectedDeviceName || qsTr("Your devices")
                textFormat: Text.PlainText
                style: header
                Layout.fillWidth: true
            }
            LV.LabelButton {
                objectName: "networkAllDevices"
                visible: panel.selectedDeviceId.length > 0
                text: qsTr("All devices")
                onClicked: panel.clearDeviceSelection()
            }
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
            text: qsTr("Sign in to the same iisacc account on each device. Synchronize on your local network or connect through your own Society server, including a NAS.")
            wrapMode: Text.Wrap
            sizeToContentHeight: true
        }
        LV.PushButton {
            objectName: "networkAutomaticSync"
            Layout.fillWidth: true
            text: !panel.network.signedIn ? qsTr("Sign in to sync automatically")
                : !panel.network.automaticPairingEnabled ? qsTr("Resume automatic sync") : qsTr("Sync now")
            onClicked: {
                if (!panel.network.signedIn) panel.accountRequested()
                else { panel.network.resumeAutomaticPairing(); panel.network.synchronizeNow() }
            }
        }
        LV.Label {
            Layout.fillWidth: true
            visible: panel.network.signedIn
            text: panel.network.automaticPairingStatus
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            sizeToContentHeight: true
        }
        LV.PushButton {
            objectName: "networkPairing"
            Layout.fillWidth: true
            text: qsTr("Connect manually…")
            onClicked: panel.pairingRequested()
        }
        LV.Label {
            Layout.fillWidth: true
            text: qsTr("Your Society server")
            style: header
        }
        LV.InputField {
            id: serverAddress
            objectName: "networkServerAddress"
            Layout.fillWidth: true
            text: panel.network.relayUrl.toString()
            placeholderText: qsTr("wss://nas.example.com/society")
            Accessible.name: qsTr("Society server address")
        }
        LV.Label {
            Layout.fillWidth: true
            text: qsTr("Use a server you operate or trust. It verifies your account and carries synchronization traffic between your devices.")
            wrapMode: Text.Wrap
            sizeToContentHeight: true
        }
        ColumnLayout {
            Layout.fillWidth: true
            LV.PushButton {
                objectName: "networkConnectServer"
                Layout.fillWidth: true
                text: qsTr("Connect to server")
                enabled: panel.network.signedIn && !panel.network.authBusy && serverAddress.text.trim().length > 0
                onClicked: panel.network.configureServer(serverAddress.text.trim(), false)
            }
            LV.PushButton {
                objectName: "networkHostServer"
                Layout.fillWidth: true
                visible: panel.network.hostModeAvailable
                text: qsTr("Host this container")
                enabled: panel.network.signedIn && !panel.network.authBusy && serverAddress.text.trim().length > 0
                    && panel.network.containerPath.length > 0
                onClicked: panel.network.configureServer(serverAddress.text.trim(), true)
            }
            LV.LabelButton {
                objectName: "networkClearServer"
                Layout.fillWidth: true
                text: qsTr("Use nearby discovery")
                visible: panel.network.relayUrl.toString().length > 0
                enabled: panel.network.signedIn
                onClicked: panel.network.configureServer("", false)
            }
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
            objectName: "selectedDeviceUnavailable"
            Layout.fillWidth: true
            visible: panel.selectedDeviceId.length > 0 && panel.selectedPeerId.length === 0
            text: qsTr("Files are not available from this device yet. Keep Society open on both devices to connect automatically.")
            wrapMode: Text.Wrap
            sizeToContentHeight: true
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
            visible: panel.selectedFilesVisible
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
                    model: panel.selectedDeviceId.length === 0 ? panel.network.hosts : []
                    LV.PushButton {
                        required property var modelData
                        Layout.fillWidth: true
                        text: modelData.name
                        enabled: !panel.network.busy
                        onClicked: panel.selectDevice(modelData.metadata && modelData.metadata.deviceId
                            ? modelData.metadata.deviceId : modelData.peerId, modelData.name, modelData.peerId)
                    }
                }
                Repeater {
                    model: panel.selectedFilesVisible ? panel.network.entries : []
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
                    text: qsTr("Load more"); visible: panel.selectedFilesVisible && panel.network.nextCursor.length > 0; enabled: !panel.network.busy
                    onClicked: panel.network.browse(panel.network.currentHost, panel.network.currentPath, panel.network.nextCursor)
                }
            }
        }
    }
}
}
