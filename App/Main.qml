pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society
import "Network"
import "Preferences"
import iiAccountManager as Accounts

LV.ApplicationWindow {
    id: root
    objectName: "societyWindow"
    property string initialContainerPath: ""
    property PreferencesWindow preferencesWindow: null
    readonly property AccountController accountSession: session
    readonly property DriveController storageDrive: drive
    readonly property ModelImporter storageImporter: modelImporter
    property string selectedTab: isMobilePlatform ? "Storage" : "Dashboard"
    readonly property real contentTopInset: Math.max(mobileSystemSafeTopInset,
        windowChromeInteractionsEnabled && windowDragHandleEnabled && visibility !== Window.FullScreen
            ? Math.max(0, windowDragHandleTopMargin + windowDragHandleHeight) : 0)
    signal generateRequested(string prompt, string mediaType, string aspectRatio, int count)

    function openAccount() {
        pairingPanel.close()
        networkDevices.close()
        if (preferencesWindow) preferencesWindow.close()
        root.accountSession.manager.showAccount()
    }

    function showStorage(section) {
        if (section.length > 0) drive.openSection(section)
        else drive.goHome()
        selectedTab = "Storage"
    }

    function showNotice(title, message) {
        notice.title = title
        notice.message = message
        notice.open = true
    }

    function openPreferences() {
        if (!networkDrive.hostModeAvailable)
            return
        pairingPanel.close()
        root.accountSession.manager.closeView()
        networkDevices.close()
        if (!preferencesWindow)
            preferencesWindow = preferencesComponent.createObject(root)
        if (preferencesWindow)
            preferencesWindow.open()
    }

    function openDevices() {
        pairingPanel.close()
        root.accountSession.manager.closeView()
        if (preferencesWindow)
            preferencesWindow.close()
        root.raise()
        root.requestActivate()
        networkDevices.open()
    }

    onClosing: {
        if (root.preferencesWindow)
            root.preferencesWindow.close()
    }

    title: "Society"
    primaryColor: LV.Theme.accentGreen
    width: 1440
    height: 900
    desktopMinWidth: 360
    desktopMinHeight: 320
    visible: true
    useInternalPageStack: false
    onActiveChanged: if (active && selectedTab === "Dashboard" && dashboardFiles) dashboardFiles.refresh()
    onSelectedTabChanged: if (selectedTab === "Dashboard" && dashboardFiles) dashboardFiles.refresh()

    Component.onCompleted: {
        modelImporter.attachWindow(root)
        if (initialContainerPath.length > 0) {
            drive.openContainer(initialContainerPath)
            if (drive.managedContainer && drive.hasDrive)
                drive.connectToSystem()
        } else {
            drive.openDefaultContainer()
        }
    }

    DriveController {
        id: drive
        objectName: "driveController"
        mirrorPending: !networkDrive.containerReady
    }
    DashboardFiles {
        id: dashboardFiles
        objectName: "dashboardFiles"
        containerPath: root.isDesktopPlatform && drive.contentsAvailable ? drive.rootPath : ""
        query: societyView.query
    }
    AccountController { id: session; objectName: "societyAccount" }
    Accounts.AccountViews {
        objectName: "accountViews"
        manager: root.accountSession.manager
        parent: Controls.Overlay.overlay
        anchors.fill: parent
    }
    NetworkDriveController {
        id: networkDrive
        objectName: "networkDriveController"
        containerPath: drive.rootPath
        accountSession: root.accountSession
        onMirrorChanged: drive.refreshFromDisk()
        onContainerSynchronized: {
            drive.refreshFromDisk()
            if (dashboardFiles.containerPath.length > 0)
                dashboardFiles.refresh()
        }
    }
    NetworkDevices {
        id: networkDevices
        objectName: "networkDevices"
        network: networkDrive
        parent: Controls.Overlay.overlay
        onPreferencesRequested: root.openPreferences()
        onAccountRequested: root.openAccount()
        onPairingRequested: {
            networkDevices.close()
            pairingPanel.open()
        }
    }
    DevicePairing {
        id: devicePairing
        objectName: "devicePairing"
        network: networkDrive
        onInvitationReceived: { qrScanner.stop(); networkDevices.close(); pairingPanel.open() }
    }
    QrScanner { id: qrScanner; objectName: "qrScanner" }
    PairingPanel {
        id: pairingPanel
        objectName: "pairingPanel"
        pairing: devicePairing
        scanner: qrScanner
        appWindow: root
        parent: Controls.Overlay.overlay
        onAccountRequested: root.openAccount()
        onFilesRequested: { if (!networkDrive.hosting) networkDevices.open() }
    }
    Component {
        id: preferencesComponent
        PreferencesWindow {
            network: networkDrive
            transientParent: root
            onDevicesRequested: root.openDevices()
        }
    }
    Shortcut {
        objectName: "preferencesShortcut"
        sequence: "Ctrl+,"
        context: Qt.ApplicationShortcut
        enabled: networkDrive.hostModeAvailable
        onActivated: root.openPreferences()
    }
    Shortcut {
        sequences: [StandardKey.Close]
        enabled: !root.preferencesWindow || !root.preferencesWindow.visible
        onActivated: root.close()
    }

    ModelImporter {
        id: modelImporter
        objectName: "modelImporter"
        containerPath: drive.contentsAvailable ? drive.rootPath : ""
        onFinished: function(containerPath, paths) {
            if (containerPath === drive.rootPath && paths.length > 0) {
                drive.openSection("models")
                root.selectedTab = "Storage"
                dashboardFiles.refresh()
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Choose a folder for Society")
        onAccepted: drive.openContainerUrl(selectedFolder)
    }

    FileDialog {
        id: modelDialog
        objectName: "modelFileDialog"
        title: qsTr("Import models into Society")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Safetensors models (*.safetensor *.safetensors)")]
        onAccepted: modelImporter.importFiles(selectedFiles)
    }

    content: SocietyView {
        id: societyView
        anchors.fill: parent
        anchors.topMargin: root.contentTopInset
        anchors.bottomMargin: root.mobileSystemSafeBottomInset
        anchors.leftMargin: root.mobileSystemSafeLeftInset
        anchors.rightMargin: root.mobileSystemSafeRightInset
        drive: root.storageDrive
        modelImporter: root.storageImporter
        files: dashboardFiles
        desktop: root.isDesktopPlatform
        hostModeAvailable: networkDrive.hostModeAvailable
        signedIn: session.signedIn
        selectedTab: root.selectedTab
        deviceStatus: networkDrive.connected ? qsTr("Online") : drive.hasDrive ? qsTr("Local") : qsTr("Unavailable")
        synchronizationStatus: networkDrive.synchronizationStatus
        onTabRequested: function(tab) { root.selectedTab = tab }
        onDevicesRequested: root.openDevices()
        onPreferencesRequested: root.openPreferences()
        onAccountRequested: root.openAccount()
        onChooseContainerRequested: folderDialog.open()
        onImportModelsRequested: modelDialog.open()
        onSectionRequested: function(section) { root.showStorage(section) }
        onFileRequested: function(path) { drive.openFile(path) }
        onRevealRequested: function(path) {
            if (drive.navigate(path)) root.selectedTab = "Storage"
        }
        onFeatureRequested: function(feature) {
            root.showNotice(feature, qsTr("%1 is not connected to a workspace service yet.").arg(feature))
        }
        onGenerateRequested: function(prompt, mediaType, aspectRatio, count) {
            root.generateRequested(prompt, mediaType, aspectRatio, count)
            root.showNotice(qsTr("Generate"), qsTr("No generation provider is connected to Society yet. Your prompt, aspect ratio, and image count remain in this window."))
        }
    }

    LV.Alert {
        id: notice
        objectName: "dashboardNotice"
        parent: Controls.Overlay.overlay
        title: ""
        message: ""
        primaryText: qsTr("OK")
        secondaryText: ""
        showIcon: false
        onPrimaryClicked: open = false
        onDismissed: open = false
    }

    DropArea {
        id: modelDropArea
        objectName: "modelDropArea"
        anchors.fill: societyView
        enabled: drive.hasDrive && !modelImporter.busy
        onEntered: function(drag) {
            drag.accepted = (drag.supportedActions & Qt.CopyAction) !== 0
                && modelImporter.accepts(drag.urls)
        }
        onDropped: function(drop) {
            if ((drop.supportedActions & Qt.CopyAction) !== 0 && modelImporter.importFiles(drop.urls))
                drop.accept(Qt.CopyAction)
            else
                drop.accepted = false
        }
        Rectangle {
            objectName: "modelDropOverlay"
            anchors.fill: parent
            anchors.margins: 8
            visible: modelDropArea.containsDrag || modelImporter.nativeDragActive
            radius: LV.Theme.radiusMd
            color: LV.Theme.panelBackground12
            border.color: LV.Theme.accent
            border.width: 2
            LV.Label {
                anchors.centerIn: parent
                width: parent.width - 48
                style: title2
                text: qsTr("Import models to Models")
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                sizeToContentHeight: true
            }
        }
    }
}
