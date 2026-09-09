pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV
import Society
import "Dashboard"

LV.VStack {
    id: root
    objectName: "societyContent"
    required property DriveController drive
    required property ModelImporter modelImporter
    required property DashboardFiles files
    required property bool desktop
    property bool hostModeAvailable: false
    property bool signedIn: false
    property string deviceStatus: ""
    property string selectedTab: "Dashboard"
    property alias query: dashboardSearch.text

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

    Item {
        id: toolbar
        objectName: "dashboardToolbar"
        visible: root.desktop
        Layout.fillWidth: true
        implicitHeight: 56
        LV.HStack {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 12
            LV.LabelSegmentedControl {
                objectName: "dashboardTabs"
                forceBorderlessTone: false
                LV.LabelButton {
                    objectName: "dashboardTab"
                    text: qsTr("Dashboard")
                    tone: root.selectedTab === "Dashboard" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                    Accessible.selected: root.selectedTab === "Dashboard"
                    onClicked: root.tabRequested("Dashboard")
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
            LV.Spacer { Layout.fillWidth: true }
            LV.InputField {
                id: dashboardSearch
                objectName: "dashboardSearch"
                visible: toolbar.width >= 700
                Layout.preferredWidth: Math.min(300, Math.max(130, toolbar.width - 500))
                mode: searchMode
                placeholder: qsTr("Search")
                Accessible.name: qsTr("Search Society files")
                onTextChanged: if (text.length > 0) root.tabRequested("Dashboard")
            }
            LV.IconButton {
                objectName: "dashboardAccount"
                tone: LV.AbstractButton.Default
                iconName: "loggedInUser"
                Accessible.name: root.signedIn ? qsTr("Your iisacc account") : qsTr("Sign in to iisacc")
                onClicked: root.accountRequested()
            }
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Dashboard {
            anchors.fill: parent
            visible: root.selectedTab === "Dashboard" && root.desktop
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
        StorageView {
            anchors.fill: parent
            visible: root.selectedTab === "Storage" || !root.desktop
            drive: root.drive
            modelImporter: root.modelImporter
            hostModeAvailable: root.hostModeAvailable
            signedIn: root.signedIn
            onDevicesRequested: root.devicesRequested()
            onPreferencesRequested: root.preferencesRequested()
            onAccountRequested: root.accountRequested()
            onChooseContainerRequested: root.chooseContainerRequested()
            onImportModelsRequested: root.importModelsRequested()
        }
    }
}
