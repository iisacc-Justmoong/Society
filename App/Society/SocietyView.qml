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
    property var notifications: null
    readonly property string notificationCountText: notifications && notifications.count > 0
        ? String(notifications.count) : ""
    required property DashboardFiles files
    required property bool desktop
    required property NetworkDriveController network
    required property AccountController account
    property bool toolsReady: false
    property bool storageReady: false
    property bool environmentReady: false
    property bool pendingStorageActions: false
    function openStorageActions(): void {
        if (storageLoader.item) storageLoader.item.openActions()
        else pendingStorageActions = true
    }
    readonly property string availableModelsDirectory: drive && drive.contentsAvailable ? drive.rootPath + "/Models" : ""
    property string preloadedModelsDirectory: ""
    function prepareStorageModels(): void {
        if (availableModelsDirectory.length > 0) preloadedModelsDirectory = availableModelsDirectory
    }
    onAvailableModelsDirectoryChanged: if (preloadedModelsDirectory !== availableModelsDirectory)
        preloadedModelsDirectory = ""
    property bool pendingEnvironmentDevices: false
    function showEnvironmentDevices() {
        if (environmentLoader.item) environmentLoader.item.showDevices()
        else pendingEnvironmentDevices = true
    }
    signal addDeviceRequested()
    signal pairingRequested()
    signal browseDeviceRequested(string id, string name, string peerId)
    signal noticeRequested(string title, string message)
    property bool hostModeAvailable: false
    property bool signedIn: account ? account.signedIn : false
    property string deviceStatus: ""
    property string synchronizationStatus: ""
    property var photos: null
    property string selectedTab: "Dashboard"
    property string query: ""
    property bool searchExpanded: false
    readonly property var platformInputMethod: Qt.inputMethod
    readonly property bool searchHasFocus: dashboardSearch.inputItem.activeFocus || mobileSearch.inputItem.activeFocus
    readonly property bool compactNavigation: !desktop && width < 760
    readonly property bool navigationOpen: navigationSheet.visible || (storageLoader.item ? storageLoader.item.actionsOpen : false)
    property real toolbarLeadingInset: 0
    readonly property real toolbarHeight: desktop && toolbar.visible ? 56 : 0
    readonly property var toolbarInteractiveItems: [toolbarNavigation, dashboardSearch, dashboardNotifications, dashboardAccount, wideStorageActions]
    readonly property bool toolsCanGoBack: toolsLoader.item ? toolsLoader.item.canGoBack : false
    function goBackTool(): void { if (toolsLoader.item) toolsLoader.item.goBack() }

    signal tabRequested(string tab)
    signal devicesRequested()
    signal preferencesRequested()
    signal accountRequested()
    signal chooseContainerRequested()
    signal importModelsRequested()
    signal sectionRequested(string section)
    signal fileRequested(string path)
    signal revealRequested(string path)
    signal featureRequested(string feature)
    signal generateRequested(string prompt, string mediaType, string aspectRatio, int count)

    spacing: 0
    onSelectedTabChanged: {
        navigationSheet.close()
        if (selectedTab !== "Storage") pendingStorageActions = false
        if (selectedTab !== "Environment") pendingEnvironmentDevices = false
    }
    onCompactNavigationChanged: navigationSheet.close()
    onQueryChanged: if (query.length > 0 && selectedTab !== "Dashboard") root.tabRequested("Dashboard")

    // Keep one metadata-only file model across Dashboard and Storage. It is
    // populated by the first visible browser and reused when the path matches.
    StorageDirectoryModel {
        id: workspaceFilesDirectory
        objectName: "sharedFilesDirectoryModel"
    }
    StorageModels {
        id: sharedStorageModels
        objectName: "modelCatalog"
        directory: root.availableModelsDirectory.length > 0
            && root.preloadedModelsDirectory === root.availableModelsDirectory ? root.availableModelsDirectory : ""
    }
    Connections {
        target: sharedStorageModels
        function onModelsChanged() {
            if (root.notifications) root.notifications.record(qsTr("Models"), sharedStorageModels.errorString)
        }
    }
    Timer {
        id: modelPreloadTimer
        objectName: "modelPreloadTimer"
        interval: 1500
        repeat: false
        // Do not compete with startup, a visible file scan, an import, search,
        // another page or a backgrounded application.
        running: root.visible && root.selectedTab === "Dashboard" && Qt.application.state === Qt.ApplicationActive
            && root.files && !root.files.loading && !dashboard.filesBrowserLoading
            && !dashboard.calendarPageVisible && !dashboard.activityPageVisible && !dashboard.projectsPageVisible
            && !dashboard.calendarModel.loading && !dashboard.projectWorkspace.busy
            && !root.modelImporter.busy && !root.searchHasFocus && root.query.length === 0
            && (!toolsLoader.item || !toolsLoader.item.preloadBusy)
            && root.availableModelsDirectory.length > 0
            && root.preloadedModelsDirectory !== root.availableModelsDirectory
        onTriggered: root.prepareStorageModels()
    }

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
            anchors.topMargin: root.desktop ? 8 : 6
            anchors.bottomMargin: root.desktop ? 8 : 6
            anchors.leftMargin: root.toolbarLeadingInset + (root.toolbarLeadingInset > 0 ? 8 : 12)
            spacing: 8
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
                LV.IconSegmentedControl {
                    id: dashboardTabs
                    objectName: "dashboardTabs"
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.alignWhenCentered: false
                    forceBorderlessTone: false
                    LV.IconButton {
                        objectName: "dashboardTab"; iconName: "database"
                        Accessible.name: qsTr("Workspace")
                        tone: root.selectedTab === "Dashboard" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                        Accessible.selected: root.selectedTab === "Dashboard"
                        onClicked: root.tabRequested("Dashboard")
                    }
                    LV.IconButton {
                        objectName: "tasksTab"; iconName: "toolwindowrun"
                        Accessible.name: qsTr("Tasks")
                        tone: root.selectedTab === "Tools" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                        Accessible.selected: root.selectedTab === "Tools"
                        onClicked: root.tabRequested("Tools")
                    }
                    LV.IconButton {
                        objectName: "storageTab"; iconName: "serverObjectsGroup"
                        Accessible.name: qsTr("Storage")
                        tone: root.selectedTab === "Storage" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                        Accessible.selected: root.selectedTab === "Storage"
                        onClicked: root.tabRequested("Storage")
                    }
                    LV.IconButton { objectName: "spaceTab"; iconName: "toolwindownewUI"; Accessible.name: qsTr("Space"); tone: LV.AbstractButton.Borderless; backgroundColorDisabled: "transparent"; enabled: false }
                    LV.IconButton { objectName: "analysisTab"; iconName: "chartLine"; Accessible.name: qsTr("Analysis"); tone: LV.AbstractButton.Borderless; backgroundColorDisabled: "transparent"; enabled: false }
                    LV.IconButton { objectName: "portfolioTab"; iconName: "pattern"; Accessible.name: qsTr("Portfolio"); tone: LV.AbstractButton.Borderless; backgroundColorDisabled: "transparent"; enabled: false }
                    LV.IconButton {
                        objectName: "browseTab"; iconName: "webApp"
                        Accessible.name: qsTr("Browse")
                        tone: root.selectedTab === "Browse" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                        Accessible.selected: root.selectedTab === "Browse"
                        onClicked: root.tabRequested("Browse")
                    }
                    LV.IconButton { objectName: "learningTab"; iconName: "toolwindowlearn"; Accessible.name: qsTr("Learning"); tone: LV.AbstractButton.Borderless; backgroundColorDisabled: "transparent"; enabled: false }
                    LV.IconButton {
                        objectName: "environmentTab"; iconName: "multiplatform"
                        Accessible.name: qsTr("Environment")
                        tone: root.selectedTab === "Environment" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                        Accessible.selected: root.selectedTab === "Environment"
                        onClicked: root.tabRequested("Environment")
                    }
                }
            }
            LV.Spacer { Layout.fillWidth: true }
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
                onClicked: root.openStorageActions()
            }
            LV.IconButton {
                id: dashboardNotifications
                objectName: "dashboardNotifications"
                tone: LV.AbstractButton.Default
                iconSource: Qt.resolvedUrl("Toolbar/icons/notifications.svg")
                text: root.notificationCountText
                Accessible.name: root.notifications && root.notifications.count > 0
                    ? qsTr("Notifications (%1)").arg(root.notifications.count) : qsTr("Notifications")
                onClicked: notificationsPopup.visible ? notificationsPopup.close() : notificationsPopup.openFor(dashboardNotifications)
            }
            LV.AbstractButton {
                id: dashboardAccount
                objectName: "dashboardAccount"
                implicitWidth: LV.Theme.scaleMetric(40)
                implicitHeight: LV.Theme.scaleMetric(40)
                Layout.preferredWidth: implicitWidth
                Layout.preferredHeight: implicitHeight
                horizontalPadding: 0
                verticalPadding: 0
                background: Item {}
                contentItem: LV.Avatar {
                    id: accountAvatar
                    objectName: "dashboardAvatar"
                    size: LV.Avatar.Medium
                    source: root.signedIn && root.account && root.account.account
                        ? root.account.account.avatarUrl : ""
                    Accessible.ignored: true
                    Image {
                        objectName: "accountAvatarFallback"
                        anchors.centerIn: parent
                        width: LV.Theme.scaleMetric(20)
                        height: LV.Theme.scaleMetric(20)
                        source: Qt.resolvedUrl("Toolbar/icons/avatar-placeholder.svg")
                        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                        fillMode: Image.PreserveAspectFit
                        visible: accountAvatar.status !== Image.Ready
                        Accessible.ignored: true
                    }
                }
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
                onClicked: root.openStorageActions()
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
                id: mobileNotifications
                objectName: "mobileNotifications"
                iconSource: Qt.resolvedUrl("Toolbar/icons/notifications.svg")
                tone: LV.AbstractButton.Default
                text: root.notificationCountText
                Accessible.name: root.notifications && root.notifications.count > 0
                    ? qsTr("Notifications (%1)").arg(root.notifications.count) : qsTr("Notifications")
                onClicked: notificationsPopup.visible ? notificationsPopup.close() : notificationsPopup.openFor(mobileNotifications)
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

    NotificationPopover {
        id: notificationsPopup
        notifications: root.notifications
        touchNavigation: !root.desktop
    }

    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Dashboard {
            id: dashboard
            anchors.fill: parent
            visible: root.selectedTab === "Dashboard"
            touchNavigation: !root.desktop
            drive: root.drive
            viewModel: root.files
            sharedFilesDirectory: workspaceFilesDirectory
            projectActorId: root.account.signedIn ? root.account.userId : ""
            navigation: root.navigation
            driveError: root.drive.errorString
            deviceStatus: root.deviceStatus
            onSectionRequested: function(section) { root.sectionRequested(section) }
            onFileRequested: function(path) { root.fileRequested(path) }
            onRevealRequested: function(path) { root.revealRequested(path) }
            onDevicesRequested: root.devicesRequested()
            onFeatureRequested: function(feature) { root.featureRequested(feature) }
        }
        Loader {
            id: toolsLoader
            objectName: "toolsLoader"
            anchors.fill: parent
            visible: root.selectedTab === "Tools"
            active: (root.visible && root.selectedTab === "Tools") || root.toolsReady
            asynchronous: true
            onLoaded: root.toolsReady = true
            sourceComponent: ToolsView {
                touchNavigation: !root.desktop
                modelsDirectory: root.drive.contentsAvailable ? root.drive.rootPath + "/Models" : ""
                modelsBusy: root.modelImporter.busy
                preloadAllowed: root.selectedTab === "Tools" && !sharedStorageModels.loading
                    && !root.files.loading && !dashboard.filesBrowserLoading && !root.modelImporter.busy
                    && !dashboard.calendarModel.loading && !dashboard.projectWorkspace.busy
                onPackageCreated: function(path) {
                    root.files.refresh()
                    if (root.drive.contentsAvailable && path.startsWith(root.drive.rootPath + "/Models/"))
                        root.modelImporter.organizeModels()
                }
                onGenerateRequested: function(prompt, mediaType, aspectRatio, count) {
                    root.generateRequested(prompt, mediaType, aspectRatio, count)
                }
            }
        }
        Loader {
            id: environmentLoader
            objectName: "environmentLoader"
            anchors.fill: parent
            visible: root.selectedTab === "Environment"
            active: (root.visible && root.selectedTab === "Environment") || root.environmentReady
            asynchronous: true
            onLoaded: {
                root.environmentReady = true
                if (root.pendingEnvironmentDevices) {
                    root.pendingEnvironmentDevices = false
                    item.showDevices()
                }
            }
            sourceComponent: EnvironmentView {
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
        }
        BrowseView {
            anchors.fill: parent
            visible: root.selectedTab === "Browse"
            network: root.network
            onDevicesRequested: { root.tabRequested("Environment"); root.showEnvironmentDevices() }
        }
        Loader {
            id: storageLoader
            objectName: "storageLoader"
            anchors.fill: parent
            visible: root.selectedTab === "Storage"
            active: (root.visible && root.selectedTab === "Storage") || root.storageReady
            asynchronous: true
            onLoaded: {
                root.storageReady = true
                if (root.pendingStorageActions) {
                    root.pendingStorageActions = false
                    if (root.selectedTab === "Storage") item.openActions()
                }
            }
            sourceComponent: StorageView {
                drive: root.drive
                cloudFiles: root.cloudFiles
                navigation: root.navigation
                modelImporter: root.modelImporter
                notifications: root.notifications
                models: sharedStorageModels
                sharedFilesDirectory: workspaceFilesDirectory
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
                onModelCatalogRequested: root.prepareStorageModels()
            }
        }
        LV.Label {
            anchors.centerIn: parent
            visible: (root.selectedTab === "Tools" && toolsLoader.status === Loader.Loading)
                || (root.selectedTab === "Storage" && storageLoader.status === Loader.Loading)
                || (root.selectedTab === "Environment" && environmentLoader.status === Loader.Loading)
            text: qsTr("Loading…")
            style: description
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
            onProjectsRequested: { navigationSheet.close(); root.tabRequested("Dashboard"); dashboard.showProjects() }
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
