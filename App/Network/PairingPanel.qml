pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society

Controls.Popup {
    id: panel
    required property DevicePairing pairing
    required property QrScanner scanner
    required property var appWindow
    signal accountRequested()
    signal filesRequested()
    readonly property bool desktop: pairing.network && pairing.network.hostModeAvailable
    property bool useQr: true
    width: Math.max(0, Math.min(parent.width - 24, desktop && useQr ? 512 : 440))
    height: Math.max(0, Math.min(parent.height - 24, contentColumn.implicitHeight + topPadding + bottomPadding))
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    padding: 24
    modal: true
    focus: true
    background: Rectangle { color: LV.Theme.panelBackground06; radius: LV.Theme.radiusMd }
    onOpened: {
        useQr = !pairing.network || !pairing.network.discovering
        if (desktop && useQr && pairing.incomingName.length === 0) pairing.begin()
    }
    onClosed: { scanner.stop(); pairing.cancel() }
    Connections {
        target: panel.scanner
        function onCodeCaptured(text: string) { panel.pairing.scanCode(text) }
    }
    Connections {
        target: panel.pairing
        function onInvitationReceived() { panel.useQr = false }
    }
    Controls.ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            id: contentColumn
            width: scroll.availableWidth
            spacing: 16
            RowLayout {
                Layout.fillWidth: true
                LV.Label { Layout.fillWidth: true; style: header; text: panel.desktop ? qsTr("Pair a device") : qsTr("Pair desktop") }
                LV.PushButton { objectName: "pairingClose"; text: qsTr("Close"); onClicked: panel.close() }
            }
            RowLayout {
                visible: panel.desktop && !panel.pairing.busy && panel.pairing.phase !== "invited" && panel.pairing.phase !== "paired"
                LV.PushButton {
                    objectName: "pairingNearbyTab"
                    text: qsTr("Nearby devices")
                    enabled: panel.useQr
                    onClicked: { panel.useQr = false; panel.pairing.cancel() }
                }
                LV.PushButton {
                    objectName: "pairingQrTab"
                    text: qsTr("QR code")
                    enabled: !panel.useQr
                    onClicked: { panel.useQr = true; panel.pairing.showHostQr() }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: !panel.useQr && !panel.pairing.busy && panel.pairing.phase !== "invited" && panel.pairing.phase !== "paired"
                LV.Label {
                    objectName: "pairingDiscoveryStatus"
                    Layout.fillWidth: true
                    text: panel.pairing.network ? panel.pairing.network.discoveryStatus : ""
                    wrapMode: Text.Wrap
                    sizeToContentHeight: true
                }
                LV.PushButton {
                    objectName: "pairingDiscoveryAccount"
                    visible: panel.pairing.network && !panel.pairing.network.signedIn && !panel.pairing.network.discovering
                    text: qsTr("Sign in to iisacc")
                    onClicked: { panel.close(); panel.accountRequested() }
                }
                Repeater {
                    model: panel.desktop && panel.pairing.network ? panel.pairing.network.nearbyDevices : []
                    RowLayout {
                        id: nearbyRow
                        required property var modelData
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            LV.Label { Layout.fillWidth: true; text: nearbyRow.modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight }
                            LV.Label {
                                Layout.fillWidth: true
                                text: (nearbyRow.modelData.kind === "phone" ? qsTr("Phone") : nearbyRow.modelData.kind === "tablet" ? qsTr("Tablet") : qsTr("Desktop")) + " · " + nearbyRow.modelData.address
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                            }
                        }
                        LV.PushButton {
                            objectName: "pairingNearbyDevice"
                            text: nearbyRow.modelData.connected ? qsTr("Connected") : qsTr("Pair")
                            enabled: !nearbyRow.modelData.connected
                            Accessible.name: qsTr("Pair with %1").arg(nearbyRow.modelData.name)
                            onClicked: panel.pairing.inviteDevice(nearbyRow.modelData.id)
                        }
                    }
                }
            }
            LV.Label {
                Layout.fillWidth: true
                visible: panel.useQr && panel.pairing.phase !== "invited"
                text: panel.desktop ? qsTr("Scan this QR in mobile Society to access this desktop's Files over the same Wi-Fi or LAN.")
                    : qsTr("On your desktop, open Devices → Pair a device. Connect both devices to the same Wi-Fi or LAN.")
                wrapMode: Text.Wrap
                sizeToContentHeight: true
            }
            LV.Label {
                objectName: "pairingStatus"
                Layout.fillWidth: true
                text: panel.scanner.errorString || panel.pairing.message
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                sizeToContentHeight: true
                visible: text.length > 0 && panel.pairing.phase !== "showing"
            }
            LV.Label {
                objectName: "pairingVerificationCode"
                Layout.fillWidth: true
                visible: panel.pairing.verificationCode.length > 0
                text: panel.pairing.verificationCode
                style: header
                horizontalAlignment: Text.AlignHCenter
            }
            LV.PushButton {
                objectName: "pairingAcceptInvitation"
                Layout.fillWidth: true
                visible: panel.pairing.phase === "invited"
                text: qsTr("Accept pairing request")
                onClicked: panel.pairing.acceptInvitation()
            }
            LV.PushButton {
                objectName: "pairingConfirmDevice"
                Layout.fillWidth: true
                visible: panel.pairing.canConfirm
                text: qsTr("Codes match — allow connection")
                onClicked: panel.pairing.confirmDevice()
            }
            LV.PushButton {
                objectName: "pairingCancelDiscovery"
                Layout.fillWidth: true
                visible: panel.pairing.busy || panel.pairing.phase === "invited"
                text: panel.pairing.phase === "invited" ? qsTr("Decline") : qsTr("Cancel pairing")
                onClicked: panel.pairing.cancel()
            }
            PairingQr {
                id: qrImage
                objectName: "pairingQr"
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Math.min(scroll.availableWidth, 432)
                Layout.preferredHeight: qrImage.width
                text: panel.pairing.qrText
                visible: valid && panel.pairing.phase === "showing"
                Accessible.role: Accessible.Graphic
                Accessible.name: qsTr("One-time Society pairing QR code")
            }
            LV.Label {
                objectName: "pairingExpiry"
                visible: panel.pairing.phase === "showing"
                text: qsTr("Expires in %1 seconds").arg(panel.pairing.secondsRemaining)
            }
            LV.PushButton {
                objectName: "pairingRefresh"
                Layout.fillWidth: true
                visible: panel.desktop && panel.useQr && !panel.pairing.busy && panel.pairing.phase !== "invited" && panel.pairing.phase !== "paired"
                enabled: !panel.pairing.busy
                text: panel.pairing.qrText.length > 0 ? qsTr("Show new QR code") : qsTr("Show QR code")
                onClicked: panel.pairing.showHostQr()
            }
            LV.PushButton {
                objectName: "pairingScan"
                Layout.fillWidth: true
                visible: !panel.desktop && !panel.pairing.busy && panel.pairing.phase !== "invited" && panel.pairing.phase !== "paired"
                enabled: !panel.scanner.active && !panel.pairing.busy
                text: qsTr("Scan QR code")
                onClicked: panel.scanner.start(panel.appWindow)
            }
            LV.PushButton {
                Layout.fillWidth: true
                visible: panel.scanner.permissionDenied
                text: qsTr("Open camera settings")
                onClicked: panel.scanner.openSettings()
            }
            LV.PushButton {
                objectName: "pairingDone"
                Layout.fillWidth: true
                visible: panel.pairing.phase === "paired"
                text: panel.pairing.network && panel.pairing.network.hosting ? qsTr("Done") : qsTr("Open host Files")
                onClicked: { panel.filesRequested(); panel.close() }
            }
        }
    }
}
