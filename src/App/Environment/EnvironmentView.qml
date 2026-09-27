pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtCore
import LVRS 1.0 as LV
import Society

Rectangle {
    id: root
    objectName: "environmentView"
    required property StorageNavigation navigation
    required property DriveController drive
    required property NetworkDriveController network
    required property var account
    property string page: "Overview"
    property string selectedDeviceId: "local"
    property var appEntries: catalogue.apps
    // Remote inventories are account-scoped provider snapshots, never inferred
    // from a device being online. Tests may inject a complete design fixture.
    property var remoteInventories: []
    property var webEntries: [{id: "account", name: "Account", symbol: "Ac", palette: 5, description: qsTr("Manage your profile and subscriptions."), status: qsTr("Web app · Browser"), license: qsTr("Account service"), action: qsTr("Open web"), url: account.manager.serviceUrl.toString().replace(/\/$/, "") + "/Account"}]
    readonly property bool compact: width < 760
    readonly property var currentDevice: ({id: "local", name: account.manager.deviceInfo.name || qsTr("This device"),
        kind: network.hostModeAvailable ? "desktop" : Qt.platform.os === "ios" ? "phone" : "tablet", online: true, connected: true,
        status: qsTr("Online"), publisher: Qt.platform.os, description: network.hostModeAvailable ? qsTr("This device · Host") : qsTr("This device · Client"),
        symbol: network.hostModeAvailable ? "PC" : "PH", action: qsTr("Sync now"), actionEnabled: network.synchronizationAvailable,
        license: drive.contentsAvailable ? drive.rootPath : qsTr("No shared container")})
    readonly property var devices: [currentDevice].concat(navigation.devices.map(device => Object.assign({}, device, {
        symbol: device.kind === "phone" ? "PH" : device.kind === "tablet" ? "TB" : "PC",
        publisher: device.kind === "phone" ? qsTr("Smartphone") : device.kind === "tablet" ? qsTr("Tablet") : qsTr("Desktop"),
        description: device.connected ? qsTr("Connected on your local network") : qsTr("Registered device"),
        license: device.online ? qsTr("Available on your network") : qsTr("Keep Society open to reconnect"),
        action: device.peerId ? qsTr("Browse") : qsTr("Reconnect"), actionEnabled: true
    })))
    readonly property var selectedDevice: devices.find(device => device.id === selectedDeviceId) || currentDevice
    readonly property var installedApps: appEntries.filter(app => app.installed)
    signal addDeviceRequested()
    signal pairingRequested()
    signal preferencesRequested()
    signal accountRequested()
    signal browseDeviceRequested(string id, string name, string peerId)
    signal societyRequested()
    signal noticeRequested(string title, string message)
    color: LV.Theme.panelBackground03
    onPageChanged: mainScroll.contentItem.contentY = 0
    onVisibleChanged: if (visible) catalogue.refresh()
    function showDevices() { page = "Devices" }
    function selectDevice(id) {
        selectedDeviceId = id
        page = "Devices"
        Qt.callLater(function() {
            const flick = mainScroll.contentItem
            const position = selectedDeviceSection.mapToItem(flick, 0, 0).y + flick.contentY - 24
            flick.contentY = Math.max(0, Math.min(position, flick.contentHeight - flick.height))
        })
    }
    function showApps(collection) { page = "Apps"; appsView.collection = collection }
    function openApp(entry) {
        if (entry.deviceId) { selectDevice(entry.deviceId); return }
        if (entry.url) { if (!Qt.openUrlExternally(entry.url)) noticeRequested(entry.name, qsTr("The app could not be opened.")); return }
        if (!catalogue.open(entry.id)) noticeRequested(entry.name, qsTr("This app is not installed or could not be opened."))
    }
    function deviceAction(device) {
        if (device.id === "local") network.synchronizeNow()
        else if (device.peerId) browseDeviceRequested(device.id, device.name, device.peerId)
        else addDeviceRequested()
    }
    EnvironmentAppsModel { id: catalogue; onSocietyRequested: root.societyRequested() }
    Settings { id: savedWebApps; category: "EnvironmentWebApps"; property string entries: "[]" }
    readonly property var customWebApps: {
        try { const parsed = JSON.parse(savedWebApps.entries); return Array.isArray(parsed) ? parsed : [] } catch (error) { return [] }
    }
    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            visible: !root.compact
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            color: LV.Theme.panelBackground04
            ColumnLayout {
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                spacing: 8
                LV.Label { text: qsTr("Environment"); style: header2 }
                LV.Label { text: qsTr("Devices, apps & licenses"); style: caption }
                Repeater {
                    model: ["Overview", "Devices", "Apps"]
                    EnvironmentRow {
                        required property string modelData
                        required property int index
                        objectName: "environmentNav" + modelData
                        label: modelData
                        standardItemHeight: 36
                        selected: root.page === modelData
                        selectedBackgroundColor: "#253627"
                        textColor: selected ? LV.Theme.accentGreen : LV.Theme.bodyColor
                        iconSource: Qt.resolvedUrl("icons/" + ["home", "device", "application"][index] + ".svg")
                        onClicked: root.page = modelData
                    }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            spacing: 0
            LV.TabBar {
                objectName: "environmentCompactNavigation"
                visible: root.compact
                Layout.fillWidth: true
                model: [qsTr("Overview"), qsTr("Devices"), qsTr("Apps")]
                currentIndex: ["Overview", "Devices", "Apps"].indexOf(root.page)
                autoSelect: false
                onActivated: index => root.page = ["Overview", "Devices", "Apps"][index]
            }
            EnvironmentApps {
                id: appsView
                objectName: "environmentApps"
                visible: root.page === "Apps"
                Layout.fillWidth: true
                Layout.fillHeight: true
                apps: root.appEntries
                deviceName: root.currentDevice.name
                otherDevices: root.devices.filter(device => device.id !== "local").map(device => ({name: device.name, status: device.status,
                    apps: root.remoteInventories.filter(app => app.deviceId === device.id)}))
                webApps: root.webEntries.concat(root.customWebApps)
                onOpenRequested: entry => root.openApp(entry)
                onDetailsRequested: entry => { appDetails.entry = entry; appDetails.open() }
                onAddRequested: { if (collection === 2) root.showDevices(); else addWebApp.open() }
            }
            Controls.ScrollView {
                id: mainScroll
                objectName: "environmentScroll"
                visible: root.page !== "Apps"
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: mainScroll.availableWidth
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.margins: root.compact ? 16 : 24
                        spacing: 16
                        Rectangle {
                            visible: root.page === "Overview"
                            Layout.fillWidth: true
                            color: LV.Theme.panelBackground04
                            radius: 16
                            implicitHeight: launchDock.implicitHeight + 40
                            Flow {
                                id: launchDock
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20 }
                                spacing: 24
                                Repeater {
                                    model: root.installedApps
                                    LV.AbstractButton {
                                        id: launchButton
                                        required property var modelData
                                        objectName: "environmentLaunch-" + modelData.id
                                        width: 64; height: 64
                                horizontalPadding: 0; verticalPadding: 0
                                background: Item {}
                                        Accessible.name: modelData.name
                                        contentItem: EnvironmentBadge { symbol: launchButton.modelData.symbol; paletteIndex: launchButton.modelData.palette || 0 }
                                        onClicked: root.openApp(modelData)
                                    }
                                }
                            }
                        }
                        EnvironmentSection {
                            visible: root.page === "Overview"
                            title: qsTr("Your environment")
                            EnvironmentRow { label: qsTr("%1 registered devices").arg(root.devices.length); description: qsTr("%1 online · %2 offline").arg(root.devices.filter(d => d.online).length).arg(root.devices.filter(d => !d.online).length); value: qsTr("Manage"); iconSource: Qt.resolvedUrl("icons/device.svg"); onClicked: root.showDevices() }
                            EnvironmentRow { label: qsTr("My apps"); description: qsTr("Installed apps, updates and app licenses"); value: qsTr("Manage"); iconSource: Qt.resolvedUrl("icons/device.svg"); onClicked: root.showApps(0) }
                        }
                        EnvironmentSection {
                            visible: root.page === "Overview"
                            title: qsTr("Devices & connections")
                            Repeater {
                                model: root.devices
                                EnvironmentRow {
                                    required property var modelData
                                    label: modelData.name
                                    description: modelData.description
                                    value: modelData.status
                                    iconSource: Qt.resolvedUrl("icons/device.svg")
                                    onClicked: root.selectDevice(modelData.id)
                                }
                            }
                        }
                        ColumnLayout {
                            visible: root.page === "Devices"
                            Layout.fillWidth: true
                            spacing: 32
                            Repeater {
                                model: [{name: "Desktop", kind: "desktop"}, {name: "Tablet", kind: "tablet"}, {name: "Smartphone", kind: "phone"}]
                                ColumnLayout {
                                    id: group
                                    required property var modelData
                                    readonly property var entries: root.devices.filter(device => device.kind === modelData.kind || (modelData.kind === "desktop" && !["phone", "tablet"].includes(device.kind)))
                                    Layout.fillWidth: true
                                    spacing: 16
                                    LV.Label { text: group.modelData.name; style: header2 }
                                    LV.Label { text: qsTr("%1 devices").arg(group.entries.length); style: caption }
                                    Grid {
                                        id: cardsGrid
                                        Layout.fillWidth: true
                                        columns: width >= 780 ? 3 : width >= 510 ? 2 : 1
                                        spacing: 16
                                        Repeater {
                                            model: group.entries
                                            EnvironmentCard {
                                                required property var modelData
                                                width: (cardsGrid.width - 16 * (cardsGrid.columns - 1)) / cardsGrid.columns
                                                height: 286
                                                objectName: "environmentDevice-" + modelData.id
                                                entry: modelData
                                                deviceCard: true
                                                onDetailsRequested: root.selectDevice(entry.id)
                                                onPrimaryRequested: root.deviceAction(entry)
                                            }
                                        }
                                        LV.AbstractButton {
                                            objectName: "environmentAddDevice-" + group.modelData.kind
                                            width: (cardsGrid.width - 16 * (cardsGrid.columns - 1)) / cardsGrid.columns
                                                height: 286
                                            Accessible.name: qsTr("Add %1 device").arg(group.modelData.name)
                                            background: Rectangle { color: LV.Theme.panelBackground04; radius: 12; border.color: LV.Theme.panelBackground10 }
                                            contentItem: ColumnLayout {
                                                spacing: 16
                                                Item { Layout.fillHeight: true }
                                                LV.Label { Layout.alignment: Qt.AlignHCenter; text: "+"; style: title; font.pixelSize: 48 }
                                                LV.Label { Layout.alignment: Qt.AlignHCenter; text: qsTr("Add device") }
                                                LV.Label { Layout.alignment: Qt.AlignHCenter; text: qsTr("Discover or pair a device"); style: caption }
                                                Item { Layout.fillHeight: true }
                                            }
                                            onClicked: root.addDeviceRequested()
                                        }
                                    }
                                }
                            }
                        }
                        EnvironmentSection {
                            id: selectedDeviceSection
                            objectName: "environmentSelectedDevice"
                            visible: root.page === "Devices"
                            title: root.selectedDevice.name
                            EnvironmentRow { label: qsTr("Storage"); description: root.selectedDevice.id === "local" ? root.drive.rootPath : qsTr("Browse this device to view its files"); value: root.selectedDevice.status; iconSource: Qt.resolvedUrl("icons/device.svg"); onClicked: root.deviceAction(root.selectedDevice) }
                            EnvironmentRow { label: qsTr("Primary host"); description: root.network.hosting ? root.currentDevice.name : root.network.status; value: root.network.connected ? qsTr("Connected") : qsTr("Offline"); onClicked: root.preferencesRequested() }
                            SyncRow { }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 8
                                EnvironmentButton { text: qsTr("Sync now"); tone: LV.AbstractButton.Primary; enabled: root.network.synchronizationAvailable; onClicked: root.network.synchronizeNow() }
                                EnvironmentButton { text: qsTr("Installed apps"); tone: LV.AbstractButton.Borderless; onClicked: root.showApps(root.selectedDevice.id === "local" ? 0 : 2) }
                                EnvironmentButton { text: qsTr("Manage activations"); tone: LV.AbstractButton.Borderless; onClicked: root.accountRequested() }
                            }
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 8
                            EnvironmentButton { objectName: "environmentDiscover"; text: qsTr("Discover devices"); tone: LV.AbstractButton.Primary; onClicked: root.addDeviceRequested() }
                            EnvironmentButton { objectName: "environmentPairQr"; text: qsTr("Pair with QR code"); onClicked: root.pairingRequested() }
                            EnvironmentButton { text: root.page === "Overview" ? qsTr("Hosting settings") : qsTr("Manage registered devices"); onClicked: { if (root.page === "Overview") root.preferencesRequested(); else root.accountRequested() } }
                        }
                        EnvironmentSection {
                            visible: root.page === "Overview"
                            title: qsTr("Needs attention")
                            EnvironmentRow { label: root.network.authError ? qsTr("Connection needs attention") : qsTr("App updates and licenses"); description: root.network.authError || qsTr("Review app availability and license status."); value: qsTr("Review"); iconSource: Qt.resolvedUrl("icons/application.svg"); onClicked: { if (root.network.authError) root.accountRequested(); else root.showApps(1) } }
                        }
                        LV.Label { visible: root.page === "Overview"; text: qsTr("Hosting"); style: title }
                        LV.Label { visible: root.page === "Overview"; Layout.fillWidth: true; text: qsTr("Manage your host, shared container and Society server."); style: caption; wrapMode: Text.Wrap; sizeToContentHeight: true }
                        EnvironmentSection {
                            visible: root.page === "Overview"
                            title: qsTr("Primary host")
                            EnvironmentRow { label: root.network.hosting ? root.currentDevice.name : qsTr("Society host"); description: root.network.status; value: root.network.connected ? qsTr("Online") : qsTr("Offline"); iconSource: Qt.resolvedUrl("icons/device.svg"); onClicked: root.preferencesRequested() }
                            EnvironmentRow { label: qsTr("Shared container"); description: root.drive.rootPath; value: root.drive.contentsAvailable ? qsTr("Available") : qsTr("Unavailable"); iconSource: Qt.resolvedUrl("icons/device.svg"); onClicked: root.preferencesRequested() }
                            EnvironmentRow { label: qsTr("This device"); description: root.currentDevice.name; value: qsTr("Preferences"); iconSource: Qt.resolvedUrl("icons/device.svg"); onClicked: root.preferencesRequested() }
                        }
                        LV.Label { visible: root.page === "Devices"; text: qsTr("Discover devices"); style: title }
                        LV.Label { visible: root.page === "Devices"; Layout.fillWidth: true; text: qsTr("Find nearby devices, pair with QR code, or connect through your Society server."); style: caption; wrapMode: Text.Wrap; sizeToContentHeight: true }
                        EnvironmentSection {
                            visible: root.page === "Devices"
                            title: qsTr("Nearby devices")
                            Repeater {
                                model: root.network.nearbyDevices
                                EnvironmentRow {
                                    required property var modelData
                                    label: modelData.name
                                    description: modelData.address || ""
                                    value: modelData.connected ? qsTr("Files") : qsTr("Pair")
                                    iconSource: Qt.resolvedUrl("icons/device.svg")
                                    onClicked: root.addDeviceRequested()
                                }
                            }
                            LV.Label { Layout.fillWidth: true; text: root.network.discoveryStatus; style: caption; wrapMode: Text.Wrap; sizeToContentHeight: true }
                        }
                        EnvironmentSection {
                            title: root.page === "Overview" ? qsTr("Synchronization") : qsTr("Automatic connection")
                            SyncRow { }
                            EnvironmentRow { label: qsTr("Connected clients"); description: root.network.synchronizationStatus; value: qsTr("%1 devices").arg(root.devices.filter(d => d.id !== "local" && d.connected).length); iconSource: Qt.resolvedUrl("icons/device.svg"); onClicked: root.showDevices() }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 8
                                EnvironmentButton { text: qsTr("Sync now"); tone: LV.AbstractButton.Primary; enabled: root.network.synchronizationAvailable; onClicked: root.network.synchronizeNow() }
                                EnvironmentButton { text: qsTr("Refresh"); tone: LV.AbstractButton.Borderless; onClicked: root.network.refresh() }
                                EnvironmentButton { text: qsTr("Disconnect"); tone: LV.AbstractButton.Borderless; enabled: root.network.connected; onClicked: root.network.disconnectSession() }
                            }
                        }
                        EnvironmentSection {
                            visible: root.page === "Devices"
                            title: qsTr("Connection options")
                            EnvironmentRow { label: qsTr("Your Society server"); description: qsTr("Use a server you operate or trust."); value: qsTr("Configure"); onClicked: root.addDeviceRequested() }
                            EnvironmentRow { label: qsTr("Pair with QR code"); description: qsTr("Connect to your registered host on the same Wi-Fi or LAN."); value: qsTr("Pair"); onClicked: root.pairingRequested() }
                            EnvironmentRow { label: qsTr("No devices found?"); description: qsTr("Keep Society open on both devices and check your account."); value: qsTr("Help"); onClicked: root.noticeRequested(qsTr("Connect your devices"), qsTr("Sign in to the same iisacc account on each device, keep Society open, and use the same local network or your Society server.")) }
                        }
                    }
                }
            }
        }
        Rectangle {
            objectName: "environmentAccountPanel"
            visible: root.width >= 1200
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            color: LV.Theme.panelBackground04
            border.color: LV.Theme.panelBackground10
            Controls.ScrollView {
                anchors.fill: parent
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: parent.availableWidth
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.margins: 16
                        spacing: 16
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 128; Layout.preferredHeight: 128
                            radius: 64
                            color: LV.Theme.panelBackground10
                            clip: true
                            Image { anchors.fill: parent; source: root.account.account.avatarUrl; fillMode: Image.PreserveAspectCrop; visible: root.account.account.hasAvatar }
                            LV.Label { anchors.centerIn: parent; text: (root.account.displayName || "?").slice(0, 1); style: title; visible: !root.account.account.hasAvatar }
                        }
                        LV.Label { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: root.account.displayName || qsTr("iisacc account"); style: header; elide: Text.ElideRight }
                        LV.Label { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: root.account.signedIn ? "@" + root.account.userId : qsTr("Sign in to connect your environment"); style: description; wrapMode: Text.Wrap; sizeToContentHeight: true }
                        RowLayout {
                            Layout.fillWidth: true
                            Repeater {
                                model: [qsTr("Published"), qsTr("Followers"), qsTr("Following")]
                                ColumnLayout {
                                    id: metric
                                    required property string modelData
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    LV.Label { Layout.alignment: Qt.AlignHCenter; text: metric.modelData; style: description }
                                    LV.Label { Layout.alignment: Qt.AlignHCenter; text: "—"; style: caption }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            EnvironmentButton { Layout.fillWidth: true; text: qsTr("Account"); tone: LV.AbstractButton.Primary; onClicked: root.accountRequested() }
                            EnvironmentButton { Layout.fillWidth: true; text: qsTr("Author"); onClicked: root.noticeRequested(qsTr("Author"), root.account.account.authorDetails.biography || qsTr("No author biography has been added.")) }
                            EnvironmentButton { Layout.fillWidth: true; text: qsTr("Edit profile"); onClicked: root.accountRequested() }
                        }
                        EnvironmentSection {
                            title: qsTr("Account & environment")
                            EnvironmentRow { label: qsTr("Registered devices"); description: qsTr("%1 devices").arg(root.devices.length); onClicked: root.showDevices() }
                            EnvironmentRow { label: qsTr("My apps"); description: qsTr("Installed apps & licenses"); onClicked: root.showApps(0) }
                            EnvironmentRow { label: qsTr("Account settings"); description: qsTr("Profile, security & sessions"); onClicked: root.accountRequested() }
                        }
                        LV.Label { Layout.fillWidth: true; text: qsTr("Same iisacc account on every device."); style: caption; wrapMode: Text.Wrap; sizeToContentHeight: true }
                    }
                }
            }
        }
    }
    component SyncRow: EnvironmentRow {
        type: LV.ListItem.Toggle
        label: qsTr("Automatic synchronization")
        description: qsTr("Connect and synchronize when your devices are available.")
        Binding on checked { value: root.network.automaticPairingEnabled }
        onEdited: (field, value) => { if (field === "checked") { if (value) root.network.resumeAutomaticPairing(); else root.network.pauseAutomaticPairing() } }
    }
    LV.Sheet {
        id: appDetails
        objectName: "environmentAppDetails"
        property var entry: ({})
        parent: Controls.Overlay.overlay
        title: entry.name || qsTr("App details")
        preferredWidth: 480
        preferredHeight: 320
        ColumnLayout {
            anchors.fill: parent
            LV.Label { Layout.fillWidth: true; text: appDetails.entry.description || ""; wrapMode: Text.Wrap; sizeToContentHeight: true }
            EnvironmentRow { label: qsTr("Installation"); description: appDetails.entry.status || qsTr("Not installed") }
            EnvironmentRow { label: qsTr("License"); description: appDetails.entry.license || qsTr("Not verified") }
            EnvironmentButton { text: qsTr("Manage account"); onClicked: { appDetails.close(); root.accountRequested() } }
            EnvironmentButton { text: qsTr("Close"); onClicked: appDetails.close() }
        }
    }
    LV.Sheet {
        id: addWebApp
        objectName: "environmentAddWebApp"
        parent: Controls.Overlay.overlay
        title: qsTr("Add web app")
        preferredWidth: 480
        preferredHeight: 280
        onOpened: { webName.text = ""; webUrl.text = "" }
        ColumnLayout {
            anchors.fill: parent
            LV.InputField { id: webName; objectName: "environmentWebName"; Layout.fillWidth: true; placeholder: qsTr("App name") }
            LV.InputField { id: webUrl; objectName: "environmentWebUrl"; Layout.fillWidth: true; placeholder: "https://" }
            LV.Label { Layout.fillWidth: true; text: qsTr("Use the app’s HTTPS website address."); style: caption }
            RowLayout {
                EnvironmentButton {
                    objectName: "environmentSaveWebApp"
                    text: qsTr("Add app")
                    tone: LV.AbstractButton.Primary
                    enabled: webName.text.trim().length > 0 && /^https:\/\/[^\s/]+(?:\/[^\s]*)?$/.test(webUrl.text.trim())
                    onClicked: {
                        const entries = root.customWebApps.slice()
                        const url = webUrl.text.trim()
                        if (!entries.some(entry => entry.url === url)) entries.push({id: "web-" + Date.now(), name: webName.text.trim(), symbol: webName.text.trim().slice(0, 2), palette: entries.length % 6, description: url, status: qsTr("Web app · Browser"), license: qsTr("Personal shortcut"), action: qsTr("Open web"), url: url})
                        savedWebApps.entries = JSON.stringify(entries)
                        addWebApp.close()
                    }
                }
                EnvironmentButton { text: qsTr("Cancel"); onClicked: addWebApp.close() }
            }
        }
    }
}
