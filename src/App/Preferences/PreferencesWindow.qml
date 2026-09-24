pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV
import Society

LV.ApplicationWindow {
    id: preferences
    objectName: "preferencesWindow"
    required property NetworkDriveController network
    property DriveController drive: null
    signal devicesRequested()

    title: qsTr("Preferences — Society")
    primaryColor: LV.Theme.accentGreen
    useInternalPageStack: false
    navigationEnabled: false
    width: 720
    height: 440
    desktopMinWidth: 360
    desktopMinHeight: 320
    visible: false
    modality: Qt.NonModal
    flags: Qt.Dialog
    solidChrome: false
    property bool initialPositionSet: false

    function open() {
        if (!network.hostModeAvailable)
            return
        if (!initialPositionSet && transientParent) {
            x = Math.round(transientParent.x + (transientParent.width - width) / 2)
            y = Math.round(transientParent.y + (transientParent.height - height) / 2)
            initialPositionSet = true
        }
        showNormal()
        raise()
        requestActivate()
    }

    Shortcut { sequence: "Escape"; enabled: preferences.visible; onActivated: preferences.close() }
    Shortcut { sequences: [StandardKey.Close]; enabled: preferences.visible; onActivated: preferences.close() }
    property string locationMessage: ""
    property bool locationFailed: false
    function applyDriveLocation() {
        locationFailed = !(drive && drive.openContainer(locationField.text.trim()))
        locationMessage = locationFailed ? (drive ? drive.errorString : qsTr("Drive service is unavailable."))
            : qsTr("Society drive location saved.")
    }
    FolderDialog {
        id: locationDialog
        title: qsTr("Choose an existing Society drive folder")
        onAccepted: locationField.text = preferences.drive.localContainerPath(selectedFolder)
    }
    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.preferredWidth: Math.min(180, preferences.width * 0.3)
            Layout.fillHeight: true
            color: LV.Theme.panelBackground03
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: LV.Theme.gap12
                spacing: LV.Theme.gap16
                LV.Label { text: qsTr("Preferences"); style: header2; Layout.fillWidth: true }
                LV.PushButton {
                    objectName: "preferencesDriveCategory"
                    text: qsTr("Society drive")
                    Layout.fillWidth: true
                    tone: LV.AbstractButton.Primary
                    Accessible.name: qsTr("Society drive category")
                }
                Item { Layout.fillHeight: true }
            }
        }
        Controls.ScrollView {
            id: driveDetails
            objectName: "preferencesDriveDetails"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
            ColumnLayout {
                width: driveDetails.availableWidth
                spacing: LV.Theme.gap16
                Item { Layout.preferredHeight: LV.Theme.gap8 }
                LV.Label {
                    Layout.leftMargin: LV.Theme.gap20
                    Layout.rightMargin: LV.Theme.gap20
                    Layout.fillWidth: true
                    text: qsTr("Society drive")
                    style: header
                }
                LV.Label {
                    Layout.leftMargin: LV.Theme.gap20
                    Layout.rightMargin: LV.Theme.gap20
                    Layout.fillWidth: true
                    text: qsTr("Choose the existing Society drive used for shared models and files. This does not move or delete data.")
                    style: description
                    wrapMode: Text.Wrap
                    sizeToContentHeight: true
                }
                LV.Label {
                    Layout.leftMargin: LV.Theme.gap20
                    Layout.rightMargin: LV.Theme.gap20
                    text: qsTr("Current location")
                    style: header2
                }
                LV.Label {
                    objectName: "preferencesCurrentDrive"
                    style: body
                    Layout.leftMargin: LV.Theme.gap20
                    Layout.rightMargin: LV.Theme.gap20
                    Layout.fillWidth: true
                    text: preferences.drive && preferences.drive.hasDrive ? preferences.drive.rootPath : qsTr("No Society drive connected")
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    sizeToContentHeight: true
                }
                LV.InputField {
                    id: locationField
                    objectName: "preferencesDriveLocation"
                    Layout.leftMargin: LV.Theme.gap20
                    Layout.rightMargin: LV.Theme.gap20
                    Layout.fillWidth: true
                    placeholderText: qsTr("Existing Society drive folder")
                    Accessible.name: qsTr("Society drive location")
                    text: preferences.drive ? preferences.drive.rootPath : ""
                    onAccepted: if (applyLocation.enabled) preferences.applyDriveLocation()
                }
                Flow {
                    Layout.leftMargin: LV.Theme.gap20
                    Layout.rightMargin: LV.Theme.gap20
                    Layout.fillWidth: true
                    spacing: LV.Theme.gap8
                    LV.PushButton {
                        objectName: "browseSocietyDrive"
                        tone: LV.AbstractButton.Default
                        text: qsTr("Choose folder…")
                        enabled: preferences.drive && !preferences.drive.busy
                        onClicked: locationDialog.open()
                    }
                    LV.PushButton {
                        id: applyLocation
                        objectName: "applySocietyDrive"
                        text: qsTr("Apply")
                        enabled: locationField.text.trim().length > 0 && preferences.drive && !preferences.drive.busy
                        onClicked: preferences.applyDriveLocation()
                    }
                }
                LV.Label {
                    objectName: "preferencesDriveFeedback"
                    style: body
                    Layout.leftMargin: LV.Theme.gap20
                    Layout.rightMargin: LV.Theme.gap20
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: preferences.locationMessage
                    color: preferences.locationFailed ? LV.Theme.accentRed : LV.Theme.textTokenBody
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    sizeToContentHeight: true
                }
                Item { Layout.preferredHeight: LV.Theme.gap20 }
            }
        }
    }
}
