pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society
import "Models"
import "Drive"
import "Photos"

Item {
    id: root
    objectName: "storageView"
    required property DriveController drive
    required property StorageNavigation navigation
    required property ModelImporter modelImporter
    required property bool hostModeAvailable
    property bool desktop: true
    readonly property bool compactLayout: !desktop && width < 760
    readonly property bool actionsOpen: storageActions.visible
    onVisibleChanged: if (!visible) storageActions.close()
    property bool signedIn: false
    property string synchronizationStatus: ""
    property var photos: null
    readonly property bool photosOverview: drive.contentsAvailable && drive.currentPath === drive.rootPath + "/Photos"
    signal accountRequested()
    signal devicesRequested()
    signal preferencesRequested()
    signal chooseContainerRequested()
    signal importModelsRequested()
    property bool browsingModelFolders: false
    readonly property bool modelsOverview: drive.contentsAvailable && drive.currentPath === drive.rootPath + "/Models" && !browsingModelFolders
    function browseModelFolder(path) {
        if (drive.navigate(path)) browsingModelFolders = true
    }
    StorageModels {
        id: models
        objectName: "modelCatalog"
        directory: root.drive.contentsAvailable ? root.drive.rootPath + "/Models" : ""
        onObjectReady: function(path, directory) {
            if (directory) root.browseModelFolder(path)
            else root.drive.openFile(path)
        }
    }
    Connections {
        target: root.drive
        function onLocationChanged() { root.browsingModelFolders = false }
        function onContentsChanged() { models.refresh() }
    }
    Connections {
        target: root.modelImporter
        function onFinished() { models.refresh() }
        function onOrganized() { models.refresh() }
    }

    ColumnLayout {
        objectName: "driveContent"
        anchors.fill: parent
        spacing: 0

        RowLayout {
            visible: root.desktop && (!root.drive.contentsAvailable || root.drive.atRoot)
            Layout.fillWidth: true
            Layout.margins: 16
            spacing: 16
            LV.Label {
                objectName: "societyDriveTitle"
                Layout.fillWidth: true
                style: header
                text: "Society"
                elide: Text.ElideRight
            }
            LV.PushButton {
                objectName: "openNetworkDevices"
                text: qsTr("Devices")
                onClicked: root.devicesRequested()
            }
            LV.PushButton {
                objectName: "openAccount"
                visible: !root.hostModeAvailable
                text: root.signedIn ? qsTr("Account") : qsTr("Sign in")
                onClicked: root.accountRequested()
            }
            LV.PushButton {
                objectName: "openPreferences"
                visible: root.hostModeAvailable
                iconMode: root.width < 600
                iconGlyph: "⚙"
                text: iconMode ? "" : qsTr("Preferences…")
                Accessible.name: qsTr("Preferences")
                tone: LV.AbstractButton.Default
                onClicked: root.preferencesRequested()
            }
            LV.PushButton {
                objectName: "chooseContainer"
                visible: !root.drive.managedContainer || !root.drive.hasDrive
                text: root.drive.managedContainer ? qsTr("Retry")
                    : (root.drive.hasDrive ? qsTr("Open container…") : qsTr("Choose folder…"))
                enabled: !root.drive.busy && !root.modelImporter.busy && !root.modelImporter.choosingFiles
                onClicked: root.drive.managedContainer ? root.drive.openDefaultContainer() : root.chooseContainerRequested()
            }
        }

        Rectangle { visible: root.desktop && (!root.drive.contentsAvailable || root.drive.atRoot); Layout.fillWidth: true; implicitHeight: 1; color: LV.Theme.panelBackground10 }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            StorageSidebar {
                visible: root.drive.contentsAvailable && root.width >= 760
                navigation: root.navigation
                touchNavigation: !root.desktop
                Layout.preferredWidth: 228
                Layout.fillHeight: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                RowLayout {
                    visible: !root.desktop || (root.drive.contentsAvailable && !root.modelsOverview)
                    Layout.fillWidth: true
                    Layout.margins: root.compactLayout ? 8 : 16
                    spacing: 10
                    LV.PushButton {
                        objectName: "driveUp"
                        Layout.minimumWidth: root.desktop ? 0 : 44
                        Layout.minimumHeight: root.desktop ? 0 : 44
                        text: qsTr("Up")
                        enabled: !root.drive.atRoot
                        onClicked: root.drive.goUp()
                    }
                    Controls.ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.desktop ? 32 : 44
                        clip: true
                        Controls.ScrollBar.vertical.policy: Controls.ScrollBar.AlwaysOff
                        Row {
                            spacing: 6
                            Repeater {
                                model: root.drive.breadcrumbs
                                LV.AbstractButton {
                                    required property var modelData
                                    text: modelData.name
                                    height: root.desktop ? implicitHeight : 44
                                    onClicked: root.drive.navigate(modelData.path)
                                }
                            }
                        }
                    }
                    LV.IconButton {
                        objectName: "storageActionsToggle"
                        visible: !root.desktop
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 44
                        iconName: "generalsettings"
                        tone: LV.AbstractButton.Borderless
                        Accessible.name: qsTr("Storage actions")
                        onClicked: storageActions.open()
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(440, parent.width - 48)
                        visible: !root.drive.contentsAvailable
                        spacing: 16
                        LV.Label {
                            Layout.fillWidth: true
                            style: title2
                            text: qsTr("Your Society drive")
                            horizontalAlignment: Text.AlignHCenter
                        }
                        LV.Label {
                            objectName: "mirrorProgress"
                            Layout.fillWidth: true
                            style: description
                            textFormat: Text.PlainText
                            text: root.drive.hasDrive && root.drive.mirrorPending
                                ? (root.synchronizationStatus.length > 0 ? root.synchronizationStatus
                                    : qsTr("Connect to your desktop to mirror its Society drive. Your files appear here when the initial sync finishes."))
                                : root.drive.managedContainer
                                ? qsTr("Opening Society…")
                                : qsTr("Choose a folder to open your drive and its storage sections.")
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            sizeToContentHeight: true
                        }
                    }

                    GridView {
                        id: sectionsGrid
                        objectName: "sectionsGrid"
                        anchors.fill: parent
                        anchors.margins: root.compactLayout ? 12 : 16
                        visible: root.drive.contentsAvailable && root.drive.atRoot
                        model: root.drive.sections
                        cellWidth: width / Math.max(1, Math.floor(width / (root.compactLayout ? 148 : 174)))
                        cellHeight: 150
                        clip: true
                        currentIndex: -1
                        Controls.ScrollBar.vertical: Controls.ScrollBar {}
                        delegate: LV.AbstractButton {
                            id: sectionTile
                            required property var modelData
                            objectName: "sectionTile"
                            width: sectionsGrid.cellWidth - 12
                            height: sectionsGrid.cellHeight - 12
                            text: modelData.name
                            Accessible.name: modelData.name
                            onClicked: root.drive.openSection(modelData.key)
                            contentItem: ColumnLayout {
                                spacing: 12
                                Image {
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.preferredWidth: 68
                                    Layout.preferredHeight: 68
                                    source: LV.Theme.iconPath("nodesfolder")
                                    sourceSize: Qt.size(136, 136)
                                }
                                LV.Label {
                                    Layout.fillWidth: true
                                    style: body
                                    text: sectionTile.modelData.name
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }

                    FileGridView {
                        id: filesGrid
                        objectName: "fileGridView"
                        anchors.fill: parent
                        visible: root.drive.contentsAvailable && !root.drive.atRoot && !root.modelsOverview && !root.photosOverview
                        path: visible ? root.drive.currentPath : ""
                        heading: root.drive.currentSection
                        imagesOnly: root.drive.currentSection === "Generation History"
                        chronological: root.drive.currentSection === "Files" || filesGrid.imagesOnly
                        touchNavigation: !root.desktop
                        onActivated: function(path, isDirectory) {
                            if (isDirectory)
                                root.drive.currentSection === "Models" ? root.browseModelFolder(path) : root.drive.navigate(path)
                            else
                                root.drive.openFile(path)
                        }
                    }
                    // The persistent folder model watches real filesystem changes
                    // asynchronously; sync status must not recreate it.
                    PhotosView {
                        anchors.fill: parent
                        touchNavigation: !root.desktop
                        visible: root.photosOverview
                        controller: root.photos
                    }
                    ModelsView {
                        anchors.fill: parent
                        visible: root.modelsOverview
                        catalog: models
                        touchNavigation: !root.desktop
                        importing: root.modelImporter.busy
                        importEnabled: !root.modelImporter.busy && !root.modelImporter.choosingFiles && !root.drive.busy
                        importStatus: root.modelImporter.status
                        importError: root.modelImporter.errorString
                        onImportRequested: {
                            if (Qt.platform.os === "ios") root.modelImporter.chooseFiles()
                            else root.importModelsRequested()
                        }
                        onCancelImportRequested: root.modelImporter.cancel()
                        onFileRequested: function(path) { models.activatePath(path, true) }
                        onFolderRequested: function(path) { root.browseModelFolder(path) }
                        onBrowseFoldersRequested: root.browseModelFolder(root.drive.rootPath + "/Models")
                    }
                }
            }
        }

        LV.Label {
            objectName: "driveError"
            visible: root.drive.errorString.length > 0
            Layout.fillWidth: true
            Layout.margins: 12
            style: description
            text: root.drive.errorString
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            sizeToContentHeight: true
        }

        ColumnLayout {
            visible: root.desktop && root.drive.contentsAvailable && !root.modelsOverview && !root.photosOverview
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                LV.PushButton {
                    objectName: "importModels"
                    text: qsTr("Import models…")
                    enabled: !root.modelImporter.busy && !root.modelImporter.choosingFiles && !root.drive.busy
                    onClicked: {
                        if (Qt.platform.os === "ios")
                            root.modelImporter.chooseFiles()
                        else
                            root.importModelsRequested()
                    }
                }
                LV.Label {
                    objectName: "modelImportStatus"
                    Layout.fillWidth: true
                    style: caption
                    text: root.modelImporter.status.length > 0 ? root.modelImporter.status
                        : qsTr("Drop .safetensor or .safetensors files here to import into Models.")
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                }
                LV.PushButton {
                    objectName: "cancelModelImport"
                    visible: root.modelImporter.busy
                    text: qsTr("Cancel")
                    onClicked: root.modelImporter.cancel()
                }
            }
            Rectangle {
                visible: root.modelImporter.busy
                Layout.fillWidth: true
                implicitHeight: 3
                color: LV.Theme.panelBackground10
                Rectangle {
                    width: parent.width * root.modelImporter.progress
                    height: parent.height
                    color: LV.Theme.accent
                }
            }
            LV.Label {
                objectName: "modelImportError"
                visible: root.modelImporter.errorString.length > 0
                Layout.fillWidth: true
                style: description
                text: root.modelImporter.errorString
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
                sizeToContentHeight: true
            }
        }

        Rectangle { visible: root.desktop && !root.modelsOverview; Layout.fillWidth: true; implicitHeight: 1; color: LV.Theme.panelBackground10 }
        ColumnLayout {
            visible: root.desktop && root.drive.hasDrive && !root.modelsOverview
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8
            LV.Label {
                objectName: "systemStatus"
                Layout.fillWidth: true
                style: caption
                text: root.drive.systemStatus
                textFormat: Text.PlainText
                elide: Text.ElideRight
            }
            RowLayout {
                Layout.fillWidth: true
                LV.Label { Layout.fillWidth: true; style: caption; text: root.width >= 440 ? qsTr("%1 sections").arg(root.drive.sections.length) : "" }
                LV.PushButton {
                    objectName: "connectToSystem"
                    visible: root.drive.systemSupported
                    enabled: !root.drive.busy
                    text: root.drive.systemPath.length > 0 ? qsTr("Reconnect") : qsTr("Connect to %1").arg(root.drive.systemName)
                    onClicked: root.drive.connectToSystem()
                }
                LV.PushButton {
                    objectName: "revealInSystem"
                    visible: root.drive.systemPath.length > 0 || (root.drive.managedContainer && root.drive.hasDrive)
                    enabled: !root.drive.busy
                    text: qsTr("Open in %1").arg(root.drive.systemName)
                    onClicked: root.drive.revealInSystem()
                }
            }
        }
    }

    LV.Sheet {
        id: storageActions
        objectName: "storageActions"
        parent: Controls.Overlay.overlay
        presentation: LV.Sheet.Mobile
        detent: LV.Sheet.Large
        title: qsTr("Storage actions")
        contentPadding: 16
        LV.VStack {
            width: parent.width
            spacing: 12
            LV.Label {
                Layout.fillWidth: true
                text: root.drive.hasDrive ? root.drive.rootPath : qsTr("Your Society drive")
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                sizeToContentHeight: true
                style: description
            }
            LV.LabelButton {
                objectName: "mobileChooseContainer"
                Layout.fillWidth: true
                Layout.minimumHeight: 44
                visible: !root.drive.managedContainer || !root.drive.hasDrive
                text: root.drive.managedContainer ? qsTr("Retry") : qsTr("Open container…")
                enabled: !root.drive.busy && !root.modelImporter.busy && !root.modelImporter.choosingFiles
                onClicked: {
                    storageActions.close()
                    if (root.drive.managedContainer) root.drive.openDefaultContainer()
                    else root.chooseContainerRequested()
                }
            }
            LV.LabelButton {
                objectName: "mobileImportModels"
                Layout.fillWidth: true
                Layout.minimumHeight: 44
                text: qsTr("Import models…")
                enabled: root.drive.contentsAvailable && !root.drive.busy && !root.modelImporter.busy && !root.modelImporter.choosingFiles
                onClicked: {
                    storageActions.close()
                    if (Qt.platform.os === "ios") root.modelImporter.chooseFiles()
                    else root.importModelsRequested()
                }
            }
            LV.Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.modelImporter.errorString || root.modelImporter.status
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                sizeToContentHeight: true
            }
            LV.LabelButton {
                Layout.fillWidth: true
                Layout.minimumHeight: 44
                visible: root.modelImporter.busy
                text: qsTr("Cancel import")
                onClicked: root.modelImporter.cancel()
            }
            LV.Label {
                Layout.fillWidth: true
                visible: root.drive.hasDrive
                text: root.drive.systemStatus
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                sizeToContentHeight: true
                style: caption
            }
            LV.LabelButton {
                objectName: "mobileConnectToSystem"
                Layout.fillWidth: true
                Layout.minimumHeight: 44
                visible: root.drive.hasDrive && root.drive.systemSupported
                enabled: !root.drive.busy
                text: root.drive.systemPath.length > 0 ? qsTr("Reconnect") : qsTr("Connect to %1").arg(root.drive.systemName)
                onClicked: root.drive.connectToSystem()
            }
            LV.LabelButton {
                objectName: "mobileRevealInSystem"
                Layout.fillWidth: true
                Layout.minimumHeight: 44
                visible: root.drive.systemPath.length > 0 || (root.drive.managedContainer && root.drive.hasDrive)
                enabled: !root.drive.busy
                text: qsTr("Open in %1").arg(root.drive.systemName)
                onClicked: root.drive.revealInSystem()
            }
        }
    }
}
