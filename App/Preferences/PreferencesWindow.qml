pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society

LV.ApplicationWindow {
    id: preferences
    objectName: "preferencesWindow"
    required property NetworkDriveController network
    signal devicesRequested()

    title: qsTr("Preferences — Society")
    primaryColor: LV.Theme.accentGreen
    useInternalPageStack: false
    navigationEnabled: false
    width: 560
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
    Controls.ButtonGroup { id: modeGroup }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: LV.Theme.gap20
        spacing: LV.Theme.gap16

        LV.Label { text: qsTr("Device mode"); style: header }

        Controls.ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: scroll.availableWidth
                spacing: LV.Theme.gap16

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: clientOption.implicitHeight + LV.Theme.gap16 * 2
                    radius: LV.Theme.radiusMd
                    color: LV.Theme.panelBackground06
                    border.color: preferences.network.mode === NetworkDriveController.ClientMode
                        ? LV.Theme.primary : LV.Theme.panelBackground10
                    ColumnLayout {
                        id: clientOption
                        anchors.fill: parent
                        anchors.margins: LV.Theme.gap16
                        spacing: LV.Theme.gap8
                        LV.RadioButton {
                            objectName: "preferencesClientMode"
                            Layout.fillWidth: true
                            text: qsTr("Client mode")
                            Controls.ButtonGroup.group: modeGroup
                            checked: preferences.network.mode === NetworkDriveController.ClientMode
                            onClicked: preferences.network.mode = NetworkDriveController.ClientMode
                        }
                        LV.Label {
                            Layout.fillWidth: true
                            text: qsTr("Browse and download Files from your other Society devices.")
                            wrapMode: Text.Wrap
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: hostOption.implicitHeight + LV.Theme.gap16 * 2
                    radius: LV.Theme.radiusMd
                    color: LV.Theme.panelBackground06
                    border.color: preferences.network.mode === NetworkDriveController.HostMode
                        ? LV.Theme.primary : LV.Theme.panelBackground10
                    ColumnLayout {
                        id: hostOption
                        anchors.fill: parent
                        anchors.margins: LV.Theme.gap16
                        spacing: LV.Theme.gap8
                        LV.RadioButton {
                            objectName: "preferencesHostMode"
                            Layout.fillWidth: true
                            text: qsTr("Host mode")
                            Controls.ButtonGroup.group: modeGroup
                            enabled: preferences.network.hostModeAvailable
                            checked: preferences.network.mode === NetworkDriveController.HostMode
                            onClicked: preferences.network.mode = NetworkDriveController.HostMode
                        }
                        LV.Label {
                            Layout.fillWidth: true
                            text: qsTr("Also share this container's Files with devices signed in to your account.")
                            wrapMode: Text.Wrap
                        }
                    }
                }

                LV.Label {
                    objectName: "preferencesConnectionStatus"
                    Layout.fillWidth: true
                    text: preferences.network.mode === NetworkDriveController.HostMode && preferences.network.containerPath.length === 0
                        ? qsTr("Open a Society container to host files.")
                        : preferences.network.hosting ? qsTr("Hosting Files for your account.")
                        : preferences.network.connected ? qsTr("Connected as a client.")
                        : preferences.network.status
                    wrapMode: Text.Wrap
                }
                LV.Label {
                    Layout.fillWidth: true
                    style: caption
                    text: qsTr("Changes apply immediately. Downloads in progress are cancelled when you switch modes.")
                    wrapMode: Text.Wrap
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            LV.PushButton {
                objectName: "preferencesDevices"
                text: qsTr("Devices…")
                tone: LV.AbstractButton.Default
                onClicked: preferences.devicesRequested()
            }
            Item { Layout.fillWidth: true }
            LV.PushButton {
                objectName: "closePreferences"
                text: qsTr("Done")
                onClicked: preferences.close()
            }
        }
    }
}
