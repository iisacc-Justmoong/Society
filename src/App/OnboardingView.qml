pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import LVRS 1.0 as LV
import Society

LV.VStack {
    id: root
    objectName: "containerOnboarding"

    required property DriveController drive
    required property NetworkDriveController network
    readonly property bool client: !network.hostModeAvailable
    readonly property bool working: drive.busy
    signal accountRequested()
    signal devicesRequested()
    signal pairingRequested()

    function createDisk(folder): void {
        if (!working && !client) drive.openContainerUrl(folder)
    }
    function selectDisk(image): void {
        if (!working && !client) drive.openContainerImage(image)
    }
    function retryConnection(): void {
        if (!drive.hasDrive && !working) drive.openDefaultContainer()
        if (!network.signedIn) accountRequested()
        else {
            network.resumeAutomaticPairing()
            network.synchronizeNow()
        }
    }

    Flickable {
        id: viewport
        objectName: "onboardingViewport"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        contentWidth: width
        contentHeight: Math.max(height, form.implicitHeight + 64)
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        LV.VStack {
            id: form
            objectName: "onboardingForm"
            width: Math.min(480, Math.max(0, viewport.width - 48))
            x: (viewport.width - width) / 2
            y: Math.max(32, (viewport.height - implicitHeight) / 2)
            spacing: 24
            alignment: Qt.AlignLeft

            LV.VStack {
                Layout.fillWidth: true
                spacing: 12
                alignment: Qt.AlignLeft
                LV.Label {
                    text: qsTr("Society")
                    style: header
                    color: LV.Theme.accent
                }
                LV.Label {
                    objectName: "onboardingTitle"
                    Layout.fillWidth: true
                    text: root.client ? qsTr("Connect to your Society host") : qsTr("Connect your Society disk")
                    style: title
                    wrapMode: Text.WordWrap
                    sizeToContentHeight: true
                }
                LV.Label {
                    Layout.fillWidth: true
                    text: root.client
                        ? qsTr("Keep Society open on your desktop and sign in to the same iisacc account. Connect over the same Wi-Fi or LAN, or through your Society server. Your workspace opens after the host connection and container are verified.")
                        : qsTr("Create a Society disk in a folder you choose, or select an existing disk to reconnect it. Keep its storage connected while using Society.")
                    style: body
                    color: LV.Theme.descriptionColor
                    wrapMode: Text.WordWrap
                    sizeToContentHeight: true
                }
            }

            LV.Label {
                objectName: "onboardingError"
                Layout.fillWidth: true
                visible: root.drive.errorString.length > 0
                text: root.drive.errorString
                textFormat: Text.PlainText
                color: LV.Theme.accentRed
                wrapMode: Text.WrapAnywhere
                sizeToContentHeight: true
                Accessible.role: Accessible.AlertMessage
            }
            LV.Label {
                objectName: "onboardingConnectionStatus"
                Layout.fillWidth: true
                visible: root.client || root.working
                text: root.working ? root.drive.systemStatus
                    : !root.network.signedIn ? qsTr("Sign in to verify and connect to your host.")
                    : root.network.connected ? root.network.synchronizationStatus
                    : root.network.automaticPairingStatus
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                sizeToContentHeight: true
            }

            LV.VStack {
                Layout.fillWidth: true
                visible: root.client
                spacing: 10
                LV.LabelButton {
                    objectName: "onboardingConnectHost"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 44
                    text: !root.network.signedIn ? qsTr("Sign in to connect")
                        : root.network.synchronizing ? qsTr("Verifying host container…") : qsTr("Retry host connection")
                    tone: LV.AbstractButton.Primary
                    enabled: !root.working && !root.network.authBusy && !root.network.synchronizing
                    onClicked: root.retryConnection()
                }
                LV.LabelButton {
                    objectName: "onboardingQrPairing"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 44
                    text: qsTr("Pair with QR code…")
                    tone: LV.AbstractButton.Default
                    onClicked: root.pairingRequested()
                }
                LV.LabelButton {
                    objectName: "onboardingHostDevices"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 44
                    text: qsTr("Find a host or configure a server…")
                    tone: LV.AbstractButton.Default
                    onClicked: root.devicesRequested()
                }
                LV.LabelButton {
                    objectName: "onboardingHostAccount"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 36
                    visible: root.network.signedIn
                    text: qsTr("Manage account…")
                    tone: LV.AbstractButton.Borderless
                    onClicked: root.accountRequested()
                }
                LV.Label {
                    Layout.fillWidth: true
                    text: qsTr("If your host is unavailable, check its disk and network connection. Existing files on this device are kept while Society reconnects.")
                    style: caption
                    color: LV.Theme.descriptionColor
                    wrapMode: Text.WordWrap
                    sizeToContentHeight: true
                }
            }

            LV.VStack {
                Layout.fillWidth: true
                visible: !root.client
                spacing: 10
                LV.LabelButton {
                    objectName: "onboardingCreateDisk"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 44
                    text: qsTr("Create a new disk…")
                    tone: LV.AbstractButton.Primary
                    enabled: !root.working
                    onClicked: folderDialog.open()
                }
                LV.LabelButton {
                    objectName: "onboardingChooseDisk"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 44
                    text: qsTr("Choose an existing disk…")
                    tone: LV.AbstractButton.Default
                    enabled: !root.working
                    onClicked: diskDialog.open()
                }
                LV.LabelButton {
                    objectName: "onboardingRetry"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 36
                    visible: root.drive.errorString.length > 0
                    text: qsTr("Retry saved location")
                    enabled: !root.working
                    tone: LV.AbstractButton.Borderless
                    onClicked: root.drive.openDefaultContainer()
                }
                LV.Label {
                    Layout.fillWidth: true
                    text: qsTr("A disk image is saved inside the folder you choose. Your file manager opens directly to Files. Models, photos, and app data stay inside Society.")
                    style: caption
                    color: LV.Theme.descriptionColor
                    wrapMode: Text.WordWrap
                    sizeToContentHeight: true
                }
                LV.Label {
                    Layout.fillWidth: true
                    text: qsTr("Sign in to save the drive location to your account and keep your other devices up to date. A moved account drive must be the same Society disk.")
                    style: caption
                    color: LV.Theme.descriptionColor
                    wrapMode: Text.WordWrap
                    sizeToContentHeight: true
                }
            }
        }
    }

    FileDialog {
        id: diskDialog
        objectName: "onboardingDiskDialog"
        title: qsTr("Choose your Society disk image")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Society disk images (*.sparsebundle)")]
        onAccepted: root.selectDisk(selectedFile)
    }
    FolderDialog {
        id: folderDialog
        objectName: "onboardingFolderDialog"
        title: qsTr("Choose where to create the Society disk")
        onAccepted: root.createDisk(selectedFolder)
    }
}
