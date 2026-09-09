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
    width: Math.max(0, Math.min(parent.width - 24, 440))
    height: Math.max(0, Math.min(parent.height - 24, contentColumn.implicitHeight + topPadding + bottomPadding))
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    padding: 24
    modal: true
    focus: true
    background: Rectangle { color: LV.Theme.panelBackground06; radius: LV.Theme.radiusMd }
    onOpened: { if (desktop) pairing.showHostQr() }
    onClosed: { scanner.stop(); pairing.cancel() }
    Connections {
        target: panel.scanner
        function onCodeCaptured(text: string) { panel.pairing.scanCode(text) }
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
                LV.Label { Layout.fillWidth: true; style: header; text: panel.desktop ? qsTr("Pair mobile device") : qsTr("Pair desktop") }
                LV.PushButton { objectName: "pairingClose"; text: qsTr("Close"); onClicked: panel.close() }
            }
            LV.Label {
                Layout.fillWidth: true
                text: panel.desktop ? qsTr("Scan this QR in mobile Society to access this desktop's Files over the same Wi-Fi or LAN.")
                    : qsTr("On your desktop, open Devices → Pair mobile device. Connect both devices to the same Wi-Fi or LAN.")
                wrapMode: Text.Wrap
                sizeToContentHeight: true
            }
            PairingQr {
                id: qrImage
                objectName: "pairingQr"
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Math.min(scroll.availableWidth, 320)
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
            LV.Label {
                objectName: "pairingStatus"
                Layout.fillWidth: true
                text: panel.scanner.errorString || panel.pairing.message
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                sizeToContentHeight: true
                visible: text.length > 0
            }
            LV.PushButton {
                objectName: "pairingRefresh"
                Layout.fillWidth: true
                visible: panel.desktop && panel.pairing.phase !== "paired"
                enabled: !panel.pairing.busy
                text: panel.pairing.qrText.length > 0 ? qsTr("Show new QR code") : qsTr("Show QR code")
                onClicked: panel.pairing.showHostQr()
            }
            LV.PushButton {
                objectName: "pairingScan"
                Layout.fillWidth: true
                visible: !panel.desktop && panel.pairing.phase !== "paired"
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
                text: panel.desktop ? qsTr("Done") : qsTr("Open host Files")
                onClicked: { panel.filesRequested(); panel.close() }
            }
        }
    }
}
