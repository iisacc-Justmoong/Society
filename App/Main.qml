pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society

LV.ApplicationWindow {
    id: root
    objectName: "societyWindow"
    property string initialContainerPath: ""

    title: drive.hasDrive ? "Society Container — Society" : "Society"
    width: 1120
    height: 720
    desktopMinWidth: 360
    desktopMinHeight: 320
    visible: true
    solidChrome: false
    useInternalPageStack: false

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

    DriveController { id: drive; objectName: "driveController" }

    ModelImporter {
        id: modelImporter
        objectName: "modelImporter"
        containerPath: drive.rootPath
        onFinished: function(containerPath, paths) {
            if (containerPath === drive.rootPath && paths.length > 0)
                drive.openSection("models")
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Choose a folder for Society Container")
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

    ColumnLayout {
        objectName: "driveContent"
        anchors.fill: parent
        anchors.topMargin: root.mobileSystemSafeTopInset
        anchors.bottomMargin: root.mobileSystemSafeBottomInset
        anchors.leftMargin: root.mobileSystemSafeLeftInset
        anchors.rightMargin: root.mobileSystemSafeRightInset
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            spacing: 16
            LV.Label {
                Layout.fillWidth: true
                style: header
                text: "Society Container"
                elide: Text.ElideRight
            }
            LV.PushButton {
                objectName: "chooseContainer"
                visible: !drive.managedContainer || !drive.hasDrive
                text: drive.managedContainer ? qsTr("Retry")
                    : (drive.hasDrive ? qsTr("Open container…") : qsTr("Choose folder…"))
                enabled: !drive.busy && !modelImporter.busy && !modelImporter.choosingFiles
                onClicked: drive.managedContainer ? drive.openDefaultContainer() : folderDialog.open()
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: LV.Theme.panelBackground10 }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                objectName: "driveSidebar"
                visible: drive.hasDrive && root.width >= 760
                Layout.preferredWidth: 216
                Layout.fillHeight: true
                color: LV.Theme.panelBackground06

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 6
                    LV.AbstractButton {
                        Layout.fillWidth: true
                        text: qsTr("Society Container")
                        onClicked: drive.goHome()
                    }
                    LV.Label {
                        Layout.topMargin: 20
                        Layout.bottomMargin: 6
                        style: caption
                        text: qsTr("SECTIONS")
                    }
                    Repeater {
                        model: drive.sections
                        LV.AbstractButton {
                            id: sectionButton
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 36
                            text: modelData.name
                            Accessible.name: modelData.name
                            Accessible.selected: drive.currentSection === modelData.name
                            onClicked: drive.openSection(modelData.key)
                            background: Rectangle {
                                radius: LV.Theme.radiusMd
                                color: drive.currentSection === sectionButton.text ? LV.Theme.accentTint : "transparent"
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                    LV.Label {
                        Layout.fillWidth: true
                        style: caption
                        text: drive.rootPath
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
                    visible: drive.hasDrive
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 10
                    LV.PushButton {
                        objectName: "driveUp"
                        text: qsTr("Up")
                        enabled: !drive.atRoot
                        onClicked: drive.goUp()
                    }
                    Controls.ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        clip: true
                        Controls.ScrollBar.vertical.policy: Controls.ScrollBar.AlwaysOff
                        Row {
                            spacing: 6
                            Repeater {
                                model: drive.breadcrumbs
                                LV.AbstractButton {
                                    required property var modelData
                                    text: modelData.name
                                    onClicked: drive.navigate(modelData.path)
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
                        visible: !drive.hasDrive
                        spacing: 16
                        LV.Label {
                            Layout.fillWidth: true
                            style: title2
                            text: qsTr("Your Society Container")
                            horizontalAlignment: Text.AlignHCenter
                        }
                        LV.Label {
                            Layout.fillWidth: true
                            style: description
                            text: drive.managedContainer
                                ? qsTr("Opening your Society Container…")
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
                        visible: drive.hasDrive && drive.atRoot
                        model: drive.sections
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
                            onClicked: drive.openSection(modelData.key)
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
                        objectName: "fileGridView"
                        anchors.fill: parent
                        visible: drive.hasDrive && !drive.atRoot
                        path: drive.currentPath
                        heading: drive.currentSection
                        imagesOnly: drive.currentSection === "Generation History"
                        onActivated: function(path, isDirectory) {
                            if (isDirectory)
                                drive.navigate(path)
                            else
                                drive.openFile(path)
                        }
                    }
                }
            }
        }

        LV.Label {
            objectName: "driveError"
            visible: drive.errorString.length > 0
            Layout.fillWidth: true
            Layout.margins: 12
            style: description
            text: drive.errorString
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            sizeToContentHeight: true
        }

        ColumnLayout {
            visible: drive.hasDrive
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                LV.PushButton {
                    objectName: "importModels"
                    text: qsTr("Import models…")
                    enabled: !modelImporter.busy && !modelImporter.choosingFiles && !drive.busy
                    onClicked: {
                        if (Qt.platform.os === "ios")
                            modelImporter.chooseFiles()
                        else
                            modelDialog.open()
                    }
                }
                LV.Label {
                    objectName: "modelImportStatus"
                    Layout.fillWidth: true
                    style: caption
                    text: modelImporter.status.length > 0 ? modelImporter.status
                        : qsTr("Drop .safetensor or .safetensors files here to import into Models.")
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                }
                LV.PushButton {
                    objectName: "cancelModelImport"
                    visible: modelImporter.busy
                    text: qsTr("Cancel")
                    onClicked: modelImporter.cancel()
                }
            }
            Rectangle {
                visible: modelImporter.busy
                Layout.fillWidth: true
                implicitHeight: 3
                color: LV.Theme.panelBackground10
                Rectangle {
                    width: parent.width * modelImporter.progress
                    height: parent.height
                    color: LV.Theme.accent
                }
            }
            LV.Label {
                objectName: "modelImportError"
                visible: modelImporter.errorString.length > 0
                Layout.fillWidth: true
                style: description
                text: modelImporter.errorString
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
                sizeToContentHeight: true
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: LV.Theme.panelBackground10 }
        ColumnLayout {
            visible: drive.hasDrive
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8
            LV.Label {
                objectName: "systemStatus"
                Layout.fillWidth: true
                style: caption
                text: drive.systemStatus
                textFormat: Text.PlainText
                elide: Text.ElideRight
            }
            RowLayout {
                Layout.fillWidth: true
                LV.Label { Layout.fillWidth: true; style: caption; text: root.width >= 440 ? qsTr("8 sections") : "" }
                LV.PushButton {
                    objectName: "connectToSystem"
                    visible: drive.systemSupported
                    enabled: !drive.busy
                    text: drive.systemPath.length > 0 ? qsTr("Reconnect") : qsTr("Connect to %1").arg(drive.systemName)
                    onClicked: drive.connectToSystem()
                }
                LV.PushButton {
                    objectName: "revealInSystem"
                    visible: drive.systemPath.length > 0 || (drive.managedContainer && drive.hasDrive)
                    enabled: !drive.busy
                    text: qsTr("Open in %1").arg(drive.systemName)
                    onClicked: drive.revealInSystem()
                }
            }
        }
    }

    DropArea {
        id: modelDropArea
        objectName: "modelDropArea"
        anchors.fill: parent
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
