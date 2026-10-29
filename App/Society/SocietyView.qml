pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society
import "Dashboard"
import "Tools"
import "Drive"
import "Environment"
import "Network"

LV.VStack {
    id: root
    objectName: "societyContent"
    required property DriveController drive
    property CloudFilesController cloudFiles: null
    required property StorageNavigation navigation
    required property ModelImporter modelImporter
    required property DashboardFiles files
    required property bool desktop
    required property NetworkDriveController network
    required property AccountController account
    function showEnvironmentDevices() { environment.showDevices() }
    signal addDeviceRequested()
    signal pairingRequested()
    signal browseDeviceRequested(string id, string name, string peerId)
    signal noticeRequested(string title, string message)
    property bool hostModeAvailable: false
    property bool signedIn: false
    property string deviceStatus: ""
    property string synchronizationStatus: ""
    property var photos: null
    property string selectedTab: "Dashboard"
    property string query: ""
    property bool searchExpanded: false
    readonly property var platformInputMethod: Qt.inputMethod
    readonly property bool searchHasFocus: dashboardSearch.inputItem.activeFocus || mobileSearch.inputItem.activeFocus
    readonly property bool compactNavigation: !desktop && width < 760
    readonly property bool navigationOpen: navigationSheet.visible || storage.actionsOpen
    property real toolbarLeadingInset: 0
    readonly property real toolbarHeight: desktop && toolbar.visible ? 56 : 0
    readonly property var toolbarInteractiveItems: [toolbarNavigation, dashboardSearch, dashboardNotifications, dashboardAccount, wideStorageActions]
    readonly property bool toolsCanGoBack: tools.canGoBack
    function goBackTool(): void { tools.goBack() }

    signal tabRequested(string tab)
    signal devicesRequested()
    signal preferencesRequested()
    signal accountRequested()
    signal notificationsRequested()
    signal chooseContainerRequested()
    signal importModelsRequested()
    signal sectionRequested(string section)
    signal fileRequested(string path)
    signal revealRequested(string path)
    signal featureRequested(string feature)
    signal generateRequested(string prompt, string mediaType, string aspectRatio, int count)

    spacing: 0
    onSelectedTabChanged: navigationSheet.close()
    onCompactNavigationChanged: navigationSheet.close()
    onQueryChanged: if (query.length > 0 && selectedTab !== "Dashboard") root.tabRequested("Dashboard")

    Item {
        id: toolbar
        objectName: "dashboardToolbar"
        visible: !root.compactNavigation
        Layout.fillWidth: true
        implicitHeight: root.desktop ? 56 : 64
        Rectangle {
            id: toolbarSurface
            objectName: "globalToolbarSurface"
            anchors.fill: parent
            color: "transparent"
        }
        LV.HStack {
            anchors.fill: parent
            anchors.margins: root.desktop ? 12 : 6
            anchors.leftMargin: root.toolbarLeadingInset + 12
            spacing: 12
            Flickable {
                id: toolbarNavigation
                objectName: "dashboardTabViewport"
                implicitWidth: dashboardTabs.implicitWidth
                implicitHeight: dashboardTabs.implicitHeight
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                Layout.maximumWidth: implicitWidth
                contentWidth: dashboardTabs.implicitWidth
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                LV.LabelSegmentedControl {
                    id: dashboardTabs
                    objectName: "dashboardTabs"
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.alignWhenCentered: false
                    forceBorderlessTone: false
                    LV.LabelButton {
                        objectName: "dashboardTab"
                        text: qsTr("Dashboard")
                        tone: root.selectedTab === "Dashboard" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                        Accessible.selected: root.selectedTab === "Dashboard"
                        onClicked: root.tabRequested("Dashboard")
                    }
                    LV.LabelButton {
                        objectName: "toolsTab"
                        text: qsTr("Tools")
                        tone: root.selectedTab === "Tools" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                        Accessible.name: text
                        Accessible.selected: root.selectedTab === "Tools"
                        onClicked: root.tabRequested("Tools")
                    }
                    LV.LabelButton {
                        objectName: "storageTab"
                        text: qsTr("Storage")
                        tone: root.selectedTab === "Storage" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                        Accessible.selected: root.selectedTab === "Storage"
                        onClicked: root.tabRequested("Storage")
                    }
                    LV.LabelButton {
                        objectName: "browseTab"
                        text: qsTr("Browse")
                        tone: root.selectedTab === "Browse" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                        Accessible.selected: root.selectedTab === "Browse"
                        onClicked: root.tabRequested("Browse")
                    }
                    LV.LabelButton {
                        objectName: "environmentTab"
                        text: qsTr("Environment")
                        tone: root.selectedTab === "Environment" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                        Accessible.selected: root.selectedTab === "Environment"
                        onClicked: root.tabRequested("Environment")
                    }
                }
            }
            LV.Spacer { Layout.fillWidth: true }
            LV.LabelButton {
                objectName: "desktopCloudFiles"
                visible: root.desktop && root.selectedTab === "Storage"
                text: qsTr("Cloud Files · 5 GB")
                enabled: root.signedIn && root.cloudFiles !== null
                onClicked: storage.openCloudFiles()
            }
            LV.InputField {
                id: dashboardSearch
                objectName: "dashboardSearch"
                visible: toolbar.width >= 700
                Layout.preferredWidth: Math.min(300, Math.max(130, toolbar.width - 500))
                backdropSource: toolbarSurface
                // Preserve the Figma search artwork inside the LVRS material field.
                mode: defaultMode
                leadingItems: Image {
                    objectName: "globalSearchIcon"
                    width: 12
                    height: 12
                    source: Qt.resolvedUrl("Toolbar/icons/search.svg")
                    sourceSize: Qt.size(24, 24)
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }
                placeholder: qsTr("Search")
                placeholderColor: LV.Theme.titleHeaderColor
                Accessible.name: qsTr("Search Society files")
                Accessible.description: qsTr("Search names, titles and keywords. Use keyword:approved, rating:5, class:ProjectAsset or in:Projects/Launch.")
                text: root.query
                onTextChanged: if (root.query !== text) root.query = text
            }
            LV.IconButton {
                id: wideStorageActions
                objectName: "wideStorageActionsToggle"
                visible: !root.desktop && root.selectedTab === "Storage"
                iconName: "generalsettings"
                tone: LV.AbstractButton.Borderless
                Accessible.name: qsTr("Storage actions")
                onClicked: storage.openActions()
            }
            LV.IconButton {
                id: dashboardNotifications
                objectName: "dashboardNotifications"
                tone: LV.AbstractButton.Default
                iconSource: Qt.resolvedUrl("Toolbar/icons/notifications.svg")
                Accessible.name: qsTr("Notifications")
                onClicked: root.notificationsRequested()
            }
            LV.IconButton {
                id: dashboardAccount
                objectName: "dashboardAccount"
                tone: LV.AbstractButton.Default
                iconSource: Qt.resolvedUrl("Toolbar/icons/account.svg")
                Accessible.name: root.signedIn ? qsTr("Your iisacc account") : qsTr("Sign in to iisacc")
                onClicked: root.accountRequested()
            }
        }
    }

    LV.VStack {
        objectName: "mobileToolbar"
        visible: root.compactNavigation
        Layout.fillWidth: true
        spacing: 0
        LV.HStack {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.preferredHeight: 52
            spacing: 4
            LV.IconButton {
                objectName: "mobileNavigationToggle"
                iconName: "toolwindowstructure"
                tone: LV.AbstractButton.Borderless
                Accessible.name: qsTr("Open navigation")
                onClicked: navigationSheet.open()
            }
            LV.Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                style: header
                text: root.selectedTab
                elide: Text.ElideRight
            }
            LV.IconButton {
                objectName: "storageActionsToggle"
                visible: root.selectedTab === "Storage"
                iconName: "generalsettings"
                tone: LV.AbstractButton.Borderless
                Accessible.name: qsTr("Storage actions")
                onClicked: storage.openActions()
            }
            LV.IconButton {
                objectName: "mobileSearchToggle"
                iconName: "inputFieldSearch"
                tone: LV.AbstractButton.Borderless
                Accessible.name: qsTr("Search Society files")
                onClicked: {
                    root.tabRequested("Dashboard")
                    root.searchExpanded = !root.searchExpanded
                    if (root.searchExpanded) Qt.callLater(function() { mobileSearch.inputItem.forceActiveFocus() })
                    else root.platformInputMethod.hide()
                }
            }
            LV.IconButton {
                objectName: "mobileAccount"
                iconName: "loggedInUser"
                tone: LV.AbstractButton.Default
                Accessible.name: root.signedIn ? qsTr("Your iisacc account") : qsTr("Sign in to iisacc")
                onClicked: root.accountRequested()
            }
        }
        LV.InputField {
            id: mobileSearch
            objectName: "mobileSearch"
            visible: root.searchExpanded && root.selectedTab === "Dashboard"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 8
            mode: searchMode
            placeholder: qsTr("Search")
            Accessible.name: qsTr("Search Society files")
            Accessible.description: qsTr("Search names, titles and keywords. Use keyword:approved, rating:5, class:ProjectAsset or in:Projects/Launch.")
            text: root.query
            onTextChanged: if (root.query !== text) root.query = text
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Dashboard {
            id: dashboard
            anchors.fill: parent
            visible: root.selectedTab === "Dashboard"
            touchNavigation: !root.desktop
            viewModel: root.files
            navigation: root.navigation
            driveError: root.drive.errorString
            deviceStatus: root.deviceStatus
            onSectionRequested: function(section) { root.sectionRequested(section) }
            onFileRequested: function(path) { root.fileRequested(path) }
            onRevealRequested: function(path) { root.revealRequested(path) }
            onDevicesRequested: root.devicesRequested()
            onFeatureRequested: function(feature) { root.featureRequested(feature) }
        }
        ToolsView {
            id: tools
            anchors.fill: parent
            visible: root.selectedTab === "Tools"
            touchNavigation: !root.desktop
            modelsDirectory: root.drive.contentsAvailable ? root.drive.rootPath + "/Models" : ""
            modelsBusy: root.modelImporter.busy
            onPackageCreated: function(path) {
                root.files.refresh()
                if (root.drive.contentsAvailable && path.startsWith(root.drive.rootPath + "/Models/"))
                    root.modelImporter.organizeModels()
            }
            onGenerateRequested: function(prompt, mediaType, aspectRatio, count) {
                root.generateRequested(prompt, mediaType, aspectRatio, count)
            }
        }
        EnvironmentView {
            id: environment
            anchors.fill: parent
            visible: root.selectedTab === "Environment"
            navigation: root.navigation
            drive: root.drive
            network: root.network
            account: root.account
            onAddDeviceRequested: root.addDeviceRequested()
            onPairingRequested: root.pairingRequested()
            onPreferencesRequested: root.preferencesRequested()
            onAccountRequested: root.accountRequested()
            onSocietyRequested: root.tabRequested("Dashboard")
            onBrowseDeviceRequested: (id, name, peerId) => root.browseDeviceRequested(id, name, peerId)
            onNoticeRequested: (title, message) => root.noticeRequested(title, message)
        }
        BrowseView {
            anchors.fill: parent
            visible: root.selectedTab === "Browse"
            network: root.network
            onDevicesRequested: { root.tabRequested("Environment"); environment.showDevices() }
        }
        StorageView {
            id: storage
            anchors.fill: parent
            visible: root.selectedTab === "Storage"
            drive: root.drive
            cloudFiles: root.cloudFiles
            navigation: root.navigation
            modelImporter: root.modelImporter
            hostModeAvailable: root.hostModeAvailable
            desktop: root.desktop
            signedIn: root.signedIn
            synchronizationStatus: root.synchronizationStatus
            photos: root.photos
            onDevicesRequested: root.devicesRequested()
            onPreferencesRequested: root.preferencesRequested()
            onAccountRequested: root.accountRequested()
            onChooseContainerRequested: root.chooseContainerRequested()
            onImportModelsRequested: root.importModelsRequested()
        }
    }

    LV.MobileTabBar {
        id: mobileTabs
        objectName: "mobileTabBar"
        visible: root.compactNavigation
        Layout.fillWidth: true
        Layout.preferredHeight: implicitHeight
        // Main places SocietyView inside the system safe area already.
        bottomSafeInset: 0
        autoSelect: false
        currentIndex: ["Dashboard", "Tools", "Storage", "Browse", "Environment"].indexOf(root.selectedTab)
        model: [
            { key: "Dashboard", text: qsTr("Home"), accessibleName: qsTr("Dashboard"), iconName: "home", objectName: "mobileDashboardTab", labelObjectName: "mobileDashboardLabel" },
            { key: "Tools", text: qsTr("Tools"), iconName: "toolwindowbuild", objectName: "mobileToolsTab", labelObjectName: "mobileToolsLabel" },
            { key: "Storage", text: qsTr("Storage"), iconName: "nodesfolder", objectName: "mobileStorageTab", labelObjectName: "mobileStorageLabel" },
            { key: "Browse", text: qsTr("Browse"), iconName: "RemoteChanges", objectName: "mobileBrowseTab", labelObjectName: "mobileBrowseLabel" },
            { key: "Environment", text: root.width < 440 ? qsTr("Env.") : qsTr("Environment"), accessibleName: qsTr("Environment"), iconName: "generalsettings", objectName: "mobileEnvironmentTab", labelObjectName: "mobileEnvironmentLabel" }
        ]
        onActivated: function(index) {
            root.platformInputMethod.hide()
            const key = model[index].key
            root.tabRequested(key)
        }
    }

    LV.Sheet {
        id: navigationSheet
        objectName: "mobileNavigation"
        parent: Controls.Overlay.overlay
        title: root.selectedTab === "Storage" ? qsTr("Storage") : qsTr("Workspace")
        presentation: LV.Sheet.Mobile
        detent: LV.Sheet.Large
        scrollContent: false
        contentPadding: 0
        DashboardSidebar {
            objectName: "mobileDashboardSidebar"
            objectNamePrefix: "mobile_"
            anchors.fill: parent
            visible: root.selectedTab !== "Storage"
            touchNavigation: true
            onHomeRequested: { navigationSheet.close(); root.tabRequested("Dashboard"); dashboard.goHome() }
            guilds: root.navigation.guilds
            organizations: root.navigation.organizations
            onCalendarRequested: { navigationSheet.close(); root.tabRequested("Dashboard"); dashboard.showCalendar("") }
            onActivityRequested: { navigationSheet.close(); root.tabRequested("Dashboard"); dashboard.showCalendar("activity") }
            onFeatureRequested: function(feature) { navigationSheet.close(); root.featureRequested(feature) }
            onWorkspaceRequested: function(kind, id) { navigationSheet.close(); root.navigation.activate(kind, id) }
        }
        StorageSidebar {
            objectName: "mobileStorageSidebar"
            objectNamePrefix: "mobile_"
            anchors.fill: parent
            visible: root.selectedTab === "Storage"
            navigation: root.navigation
            touchNavigation: true
            onActivated: navigationSheet.close()
        }
    }
}
