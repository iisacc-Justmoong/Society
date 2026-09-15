pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society

Item {
    id: root
    required property NetworkDriveController network
    property bool touchNavigation: false
    signal devicesRequested()
    signal doneRequested()
    Controls.ButtonGroup { id: modeGroup }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: LV.Theme.gap16

        LV.Label { text: qsTr("Device mode"); style: header }

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

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: clientOption.implicitHeight + LV.Theme.gap16 * 2
                    radius: LV.Theme.radiusMd
                    color: LV.Theme.panelBackground06
                    border.color: root.network.mode === NetworkDriveController.ClientMode
                        ? LV.Theme.primary : LV.Theme.panelBackground10
                    ColumnLayout {
                        id: clientOption
                        anchors.fill: parent
                        anchors.margins: LV.Theme.gap16
                        spacing: LV.Theme.gap8
                        LV.RadioButton {
                            objectName: "preferencesClientMode"
                            Layout.fillWidth: true
                            Layout.minimumHeight: root.touchNavigation ? 44 : 0
                            text: qsTr("Client mode")
                            Controls.ButtonGroup.group: modeGroup
                            checked: root.network.mode === NetworkDriveController.ClientMode
                            onClicked: root.network.mode = NetworkDriveController.ClientMode
                        }
                        LV.Label {
                            Layout.fillWidth: true
                            text: qsTr("Browse and download Files from your other Society devices.")
                            wrapMode: Text.Wrap
                            sizeToContentHeight: true
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: hostOption.implicitHeight + LV.Theme.gap16 * 2
                    radius: LV.Theme.radiusMd
                    color: LV.Theme.panelBackground06
                    border.color: root.network.mode === NetworkDriveController.HostMode
                        ? LV.Theme.primary : LV.Theme.panelBackground10
                    ColumnLayout {
                        id: hostOption
                        anchors.fill: parent
                        anchors.margins: LV.Theme.gap16
                        spacing: LV.Theme.gap8
                        LV.RadioButton {
                            objectName: "preferencesHostMode"
                            Layout.fillWidth: true
                            Layout.minimumHeight: root.touchNavigation ? 44 : 0
                            text: qsTr("Host mode")
                            Controls.ButtonGroup.group: modeGroup
                            enabled: root.network.hostModeAvailable
                            checked: root.network.mode === NetworkDriveController.HostMode
                            onClicked: root.network.mode = NetworkDriveController.HostMode
                        }
                        LV.Label {
                            Layout.fillWidth: true
                            text: root.network.hostModeAvailable
                                ? qsTr("Also share this container's Files with devices signed in to your account.")
                                : qsTr("Hosting is available on desktop. This device uses client mode.")
                            wrapMode: Text.Wrap
                            sizeToContentHeight: true
                        }
                    }
                }

                LV.Label {
                    objectName: "preferencesConnectionStatus"
                    Layout.fillWidth: true
                    text: root.network.mode === NetworkDriveController.HostMode && root.network.containerPath.length === 0
                        ? qsTr("Open a Society container to host files.")
                        : root.network.hosting ? qsTr("Hosting Files for your account.")
                        : root.network.connected ? qsTr("Connected as a client.")
                        : root.network.status
                    wrapMode: Text.Wrap
                    sizeToContentHeight: true
                }
                LV.Label {
                    Layout.fillWidth: true
                    style: caption
                    text: qsTr("Changes apply immediately. Downloads in progress are cancelled when you switch modes.")
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
