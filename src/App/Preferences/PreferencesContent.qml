pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society

Item {
    id: root
    required property NetworkDriveController network
    property DriveController drive: null
    property bool touchNavigation: false
    signal devicesRequested()
    signal doneRequested()
    FileDialog {
        id: movedDisk
        title: qsTr("Locate your Society disk image")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Society disk images (*.sparsebundle)")]
        onAccepted: root.drive.useMovedContainer(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: LV.Theme.gap16

        LV.Label { text: qsTr("Preferences"); style: header }

        Controls.ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff

            ColumnLayout {
                width: scroll.availableWidth
                spacing: LV.Theme.gap16

                LV.VStack {
                    Layout.fillWidth: true
                    visible: root.drive !== null
                    spacing: LV.Theme.gap8
                    LV.Label { text: qsTr("Account drive location"); style: header2 }
                    LV.Label {
                        Layout.fillWidth: true
                        text: root.drive && root.drive.accountManager
                            ? root.drive.accountManager.account.societyContainerDrive.imagePath || qsTr("No drive saved to this account.") : ""
                        textFormat: Text.PlainText
                        wrapMode: Text.WrapAnywhere
                        sizeToContentHeight: true
                    }
                    LV.Label {
                        objectName: "accountDriveStatus"
                        Layout.fillWidth: true
                        text: root.drive ? root.drive.accountDriveStatus : ""
                        wrapMode: Text.Wrap
                        sizeToContentHeight: true
                    }
                    LV.Label {
                        objectName: "accountDriveError"
                        Layout.fillWidth: true
                        visible: root.drive && root.drive.errorString.length > 0
                        text: root.drive ? root.drive.errorString : ""
                        textFormat: Text.PlainText
                        color: LV.Theme.accentRed
                        wrapMode: Text.WrapAnywhere
                        sizeToContentHeight: true
                    }
                    LV.LabelButton {
                        objectName: "saveAccountDrive"
                        text: qsTr("Save current drive to account")
                        enabled: root.drive && root.drive.hasDrive && !root.drive.busy
                            && root.drive.accountManager && root.drive.accountManager.authenticated
                        onClicked: root.drive.saveContainerToAccount()
                    }
                    LV.LabelButton {
                        objectName: "locateMovedAccountDrive"
                        text: qsTr("Locate moved disk…")
                        enabled: root.drive && !root.drive.busy && root.drive.accountManager
                            && root.drive.accountManager.authenticated
                        onClicked: movedDisk.open()
                    }
                    LV.Label {
                        Layout.fillWidth: true
                        style: caption
                        text: qsTr("After moving your disk image, select it on this host. Society verifies the drive and updates the location for every device signed in to your account.")
                        wrapMode: Text.Wrap
                        sizeToContentHeight: true
                    }
                }

                LV.Label {
                    objectName: "preferencesConnectionStatus"
                    Layout.fillWidth: true
                    text: root.network.mode === NetworkDriveController.HostMode && root.network.containerPath.length === 0
                        ? qsTr("Open a Society container to host files.")
                        : root.network.hosting ? qsTr("Hosting Files for your account.")
                        : root.network.connected ? qsTr("Connected to your devices.")
                        : root.network.status
                    wrapMode: Text.Wrap
                    sizeToContentHeight: true
                }

            }
        }

        RowLayout {
            Layout.fillWidth: true
            LV.PushButton {
                objectName: "preferencesDevices"
                Layout.minimumHeight: root.touchNavigation ? 44 : 0
                text: qsTr("Devices…")
                tone: LV.AbstractButton.Default
                onClicked: root.devicesRequested()
            }
            Item { Layout.fillWidth: true }
            LV.PushButton {
                objectName: "closePreferences"
                Layout.minimumHeight: root.touchNavigation ? 44 : 0
                text: qsTr("Done")
                onClicked: root.doneRequested()
            }
        }
    }
}
