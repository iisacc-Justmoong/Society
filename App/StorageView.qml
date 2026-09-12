pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society

Item {
    id: root
    objectName: "storageView"
    required property DriveController drive
    required property ModelImporter modelImporter
    required property bool hostModeAvailable
    property bool signedIn: false
    property string synchronizationStatus: ""
    signal accountRequested()
    signal devicesRequested()
    signal preferencesRequested()
    signal chooseContainerRequested()
    signal importModelsRequested()

    ColumnLayout {
        objectName: "driveContent"
        anchors.fill: parent
        spacing: 0

        RowLayout {
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

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: LV.Theme.panelBackground10 }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                objectName: "driveSidebar"
                visible: root.drive.contentsAvailable && root.width >= 760
                Layout.preferredWidth: 216
                Layout.fillHeight: true
                color: LV.Theme.panelBackground06

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 6
                    LV.AbstractButton {
                        objectName: "societyDriveHome"
                        Layout.fillWidth: true
                        text: qsTr("Society")
                        onClicked: root.drive.goHome()
                    }
                    LV.Label {
                        Layout.topMargin: 20
                        Layout.bottomMargin: 6
                        style: caption
                        text: qsTr("SECTIONS")
                    }
                    Repeater {
                        model: root.drive.sections
                        LV.AbstractButton {
                            id: sectionButton
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 36
                            text: modelData.name
                            Accessible.name: modelData.name
                            Accessible.selected: root.drive.currentSection === modelData.name
                            onClicked: root.drive.openSection(modelData.key)
                            background: Rectangle {
                                radius: LV.Theme.radiusMd
                                color: root.drive.currentSection === sectionButton.text ? LV.Theme.accentTint : "transparent"
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                    LV.Label {
                        Layout.fillWidth: true
                        style: caption
                        text: root.drive.rootPath
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                RowLayout {
                    visible: root.drive.contentsAvailable
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 10
                    LV.PushButton {
                        objectName: "driveUp"
                        text: qsTr("Up")
                        enabled: !root.drive.atRoot
                        onClicked: root.drive.goUp()
                    }
                    Controls.ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        clip: true
                        Controls.ScrollBar.vertical.policy: Controls.ScrollBar.AlwaysOff
                        Row {
                            spacing: 6
                            Repeater {
                                model: root.drive.breadcrumbs
                                LV.AbstractButton {
                                    required property var modelData
                                    text: modelData.name
                                    onClicked: root.drive.navigate(modelData.path)
                                }
                            }
                        }
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
                                : qsTr("Choose a folder to open your drive and its eight sections.")
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            sizeToContentHeight: true
                        }
                    }

                    GridView {
                        id: sectionsGrid
                        objectName: "sectionsGrid"
                        anchors.fill: parent
                        anchors.margins: 16
                        visible: root.drive.contentsAvailable && root.drive.atRoot
                        model: root.drive.sections
                        cellWidth: width / Math.max(1, Math.floor(width / 174))
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
                        visible: root.drive.contentsAvailable && !root.drive.atRoot
                        path: root.drive.contentsAvailable ? root.drive.currentPath : ""
                        heading: root.drive.currentSection
                        imagesOnly: root.drive.currentSection === "Generation History"
                        onActivated: function(path, isDirectory) {
                            if (isDirectory)
                                root.drive.navigate(path)
                            else
                                root.drive.openFile(path)
                        }
                    }
                    // The persistent folder model watches real filesystem changes
                    // asynchronously; sync status must not recreate it.
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
            visible: root.drive.contentsAvailable
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

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: LV.Theme.panelBackground10 }
        ColumnLayout {
            visible: root.drive.hasDrive
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
                LV.Label { Layout.fillWidth: true; style: caption; text: root.width >= 440 ? qsTr("8 sections") : "" }
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

}
