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
                        height: root.desktop ? implicitHeight : 44
                        text: qsTr("Dashboard")
                        tone: root.selectedTab === "Dashboard" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                        Accessible.selected: root.selectedTab === "Dashboard"
                        onClicked: root.tabRequested("Dashboard")
                    }
                    LV.LabelButton {
                        objectName: "toolsTab"
                        height: root.desktop ? implicitHeight : 44
                        text: qsTr("Tools")
                        tone: root.selectedTab === "Tools" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                        Accessible.name: text
                        Accessible.selected: root.selectedTab === "Tools"
                        onClicked: root.tabRequested("Tools")
                    }
                    LV.LabelButton {
                        objectName: "storageTab"
                        height: root.desktop ? implicitHeight : 44
                        text: qsTr("Storage")
                        tone: root.selectedTab === "Storage" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                        Accessible.selected: root.selectedTab === "Storage"
                        onClicked: root.tabRequested("Storage")
                    }
                    LV.LabelButton {
                        objectName: "browseTab"
                        height: root.desktop ? implicitHeight : 44
                        text: qsTr("Browse")
                        tone: LV.AbstractButton.Borderless
                        onClicked: root.devicesRequested()
                    }
                    LV.LabelButton {
                        objectName: "environmentTab"
                        height: root.desktop ? implicitHeight : 44
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
                Layout.minimumHeight: root.desktop ? 0 : 44
                visible: toolbar.width >= 700
                Layout.preferredWidth: Math.min(300, Math.max(130, toolbar.width - 500))
                mode: searchMode
                placeholder: qsTr("Search")
                Accessible.name: qsTr("Search Society files")
                text: root.query
                onTextChanged: if (root.query !== text) root.query = text
            }
            LV.IconButton {
                id: dashboardAccount
                objectName: "dashboardAccount"
                Layout.minimumWidth: root.desktop ? 0 : 44
                Layout.minimumHeight: root.desktop ? 0 : 44
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
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44
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
                objectName: "mobileSearchToggle"
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44
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
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44
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
            Layout.preferredHeight: 44
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
            recentFiles: root.files.recentFiles
            historyFiles: root.files.generationHistory
            loading: root.files.loading
            errorString: root.files.errorString.length > 0 ? root.files.errorString : root.drive.errorString
            query: root.files.query
            deviceStatus: root.deviceStatus
            onSectionRequested: function(section) { root.sectionRequested(section) }
            onFileRequested: function(path) { root.fileRequested(path) }
            onRevealRequested: function(path) { root.revealRequested(path) }
            onDevicesRequested: root.devicesRequested()
            onFeatureRequested: function(feature) { root.featureRequested(feature) }
            onGenerateRequested: function(prompt, mediaType, aspectRatio, count) {
                root.generateRequested(prompt, mediaType, aspectRatio, count)
            }
        }
        ModelMergeTool {
            anchors.fill: parent
            visible: root.selectedTab === "Tools"
            touchNavigation: !root.desktop
            modelsDirectory: root.drive.contentsAvailable ? root.drive.rootPath + "/Models" : ""
            modelsBusy: root.modelImporter.busy
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

    LV.HStack {
        objectName: "mobileTabBar"
        visible: root.compactNavigation
        Layout.fillWidth: true
        Layout.preferredHeight: 60
        spacing: 0
        Repeater {
            model: [
                { key: "Dashboard", label: qsTr("Dashboard"), icon: "home" },
                { key: "Tools", label: qsTr("Tools"), icon: "toolwindowbuild" },
                { key: "Storage", label: qsTr("Storage"), icon: "nodesfolder" },
                { key: "Browse", label: qsTr("Browse"), icon: "RemoteChanges" },
                { key: "Environment", label: qsTr("Environment"), icon: "generalsettings" }
            ]
            LV.AbstractButton {
                id: mobileTab
                required property var modelData
                objectName: "mobile" + modelData.key + "Tab"
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                Layout.minimumWidth: 0
                Layout.fillHeight: true
                horizontalPadding: 0
                verticalPadding: 12
                cornerRadius: 0
                text: modelData.label
                tone: root.selectedTab === modelData.key ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                Accessible.name: modelData.label
                Accessible.selected: root.selectedTab === modelData.key
                onClicked: {
                    root.platformInputMethod.hide()
                    if (modelData.key === "Browse") root.devicesRequested()
                    else if (modelData.key === "Environment") root.preferencesRequested()
                    else root.tabRequested(modelData.key)
                }
                contentItem: Column {
                    spacing: 4
                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 20; height: 20
                        source: LV.Theme.iconPath(mobileTab.modelData.icon)
                        sourceSize: Qt.size(40, 40)
                    }
                    LV.Label {
                        objectName: "mobile" + mobileTab.modelData.key + "Label"
                        width: parent.width
                        text: mobileTab.modelData.label
                        style: caption
                        font.pixelSize: root.width < 360 ? 10 : 11
                        color: root.selectedTab === mobileTab.modelData.key ? LV.Theme.primary : LV.Theme.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }
                }
            }
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
            deviceStatus: root.deviceStatus
            onHomeRequested: { navigationSheet.close(); root.tabRequested("Dashboard"); dashboard.goHome() }
            onSectionRequested: function(section) { navigationSheet.close(); root.sectionRequested(section) }
            onDevicesRequested: { navigationSheet.close(); root.devicesRequested() }
            onFeatureRequested: function(feature) { navigationSheet.close(); root.featureRequested(feature) }
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
