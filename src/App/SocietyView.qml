pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society
import "Dashboard"
import "Tools"
import "Drive"

LV.VStack {
    id: root
    objectName: "societyContent"
    required property DriveController drive
    required property StorageNavigation navigation
    required property ModelImporter modelImporter
    required property DashboardFiles files
    required property bool desktop
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
    readonly property var toolbarInteractiveItems: [toolbarNavigation, dashboardSearch, dashboardAccount]
    readonly property bool toolsCanGoBack: tools.canGoBack
    function goBackTool(): void { tools.goBack() }

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
    onSelectedTabChanged: navigationSheet.close()
    onCompactNavigationChanged: navigationSheet.close()
    onQueryChanged: if (query.length > 0 && selectedTab !== "Dashboard") root.tabRequested("Dashboard")

    Item {
        id: toolbar
        objectName: "dashboardToolbar"
        visible: !root.compactNavigation
        Layout.fillWidth: true
        implicitHeight: root.desktop ? 56 : 64
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
                        tone: LV.AbstractButton.Borderless
                        onClicked: root.devicesRequested()
                    }
                    LV.LabelButton {
                        objectName: "environmentTab"
                        text: qsTr("Environment")
                        tone: LV.AbstractButton.Borderless
                        onClicked: root.preferencesRequested()
                    }
                }
            }
            LV.Spacer { Layout.fillWidth: true }
            LV.InputField {
                id: dashboardSearch
                objectName: "dashboardSearch"
                visible: toolbar.width >= 700
                Layout.preferredWidth: Math.min(300, Math.max(130, toolbar.width - 500))
                mode: searchMode
                placeholder: qsTr("Search")
                Accessible.name: qsTr("Search Society files")
                text: root.query
                onTextChanged: if (root.query !== text) root.query = text
            }
            LV.IconButton {
                objectName: "wideStorageActionsToggle"
                visible: !root.desktop && root.selectedTab === "Storage"
                iconName: "generalsettings"
                tone: LV.AbstractButton.Borderless
                Accessible.name: qsTr("Storage actions")
                onClicked: storage.openActions()
            }
            LV.IconButton {
                id: dashboardAccount
                objectName: "dashboardAccount"
                tone: LV.AbstractButton.Default
                iconName: "loggedInUser"
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
            onGenerateRequested: function(prompt, mediaType, aspectRatio, count) {
                root.generateRequested(prompt, mediaType, aspectRatio, count)
            }
        }
        StorageView {
            id: storage
            anchors.fill: parent
            visible: root.selectedTab === "Storage"
            drive: root.drive
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
        currentIndex: root.selectedTab === "Tools" ? 1 : root.selectedTab === "Storage" ? 2 : 0
        model: [
            { key: "Dashboard", text: qsTr("Home"), accessibleName: qsTr("Dashboard"), iconName: "home", objectName: "mobileDashboardTab", labelObjectName: "mobileDashboardLabel" },
            { key: "Tools", text: qsTr("Tools"), iconName: "toolwindowbuild", objectName: "mobileToolsTab", labelObjectName: "mobileToolsLabel" },
            { key: "Storage", text: qsTr("Storage"), iconName: "nodesfolder", objectName: "mobileStorageTab", labelObjectName: "mobileStorageLabel" },
            { key: "Browse", text: qsTr("Browse"), iconName: "RemoteChanges", objectName: "mobileBrowseTab", labelObjectName: "mobileBrowseLabel" },
            { key: "Environment", text: qsTr("Settings"), accessibleName: qsTr("Environment"), iconName: "generalsettings", objectName: "mobileEnvironmentTab", labelObjectName: "mobileEnvironmentLabel" }
        ]
        onActivated: function(index) {
            root.platformInputMethod.hide()
            const key = model[index].key
            if (key === "Browse") root.devicesRequested()
            else if (key === "Environment") root.preferencesRequested()
            else root.tabRequested(key)
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
