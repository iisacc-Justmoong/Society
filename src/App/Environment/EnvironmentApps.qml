pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV

ColumnLayout {
    id: root
    required property var apps
    property var otherDevices: []
    property var webApps: []
    property string deviceName: qsTr("This device")
    property int collection: 0
    property string query: ""
    property bool updatesOnly: false
    signal openRequested(var entry)
    signal detailsRequested(var entry)
    signal addRequested()
    readonly property var installed: apps.filter(app => app.installed)
    readonly property var available: apps.filter(app => !app.installed)
    readonly property int updates: apps.filter(app => app.updateAvailable).length
    readonly property var collectionNames: [qsTr("My apps"), qsTr("All apps"), qsTr("Other devices"), qsTr("Web apps")]
    spacing: 0
    function matching(entries) {
        return entries.filter(app => (!updatesOnly || app.updateAvailable)
            && (app.name + " " + (app.description || "") + " " + (app.publisher || "")).toLowerCase().includes(query.trim().toLowerCase()))
    }
    onCollectionChanged: { query = ""; updatesOnly = false; viewport.contentItem.contentY = 0 }
    LV.TabBar {
        objectName: "environmentAppTabs"
        id: collectionTabs
        delegate: LV.Tab {
            required property int index
            required property var modelData
            objectName: modelData.objectName
            text: modelData.text
            badge: modelData.badge
            iconSource: Qt.resolvedUrl("icons/folder.svg")
            selected: index === root.collection
            tabStyle: LV.Tab.Surface
            width: collectionTabs.itemWidth(index, implicitWidth)
            height: collectionTabs.availableHeight
            navigationBar: collectionTabs
            tabIndex: index
            onClicked: collectionTabs.activate(index)
        }
        Layout.fillWidth: true
        tabStyle: LV.Tab.Surface
        widthPolicy: width < 660 ? LV.TabBar.Scrollable : LV.TabBar.Equal
        scrollableTabWidth: 166
        autoSelect: false
        currentIndex: root.collection
        model: root.collectionNames.map((name, index) => ({text: name, iconName: "folder@14x14", objectName: "environmentCollection" + index,
            badge: String([root.installed.length, root.apps.length, root.otherDevices.reduce((n, d) => n + (d.apps || []).length, 0), root.webApps.length][index])}))
        onActivated: index => root.collection = index
    }
    Controls.ScrollView {
        id: viewport
        objectName: "environmentAppsScroll"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: viewport.availableWidth
            spacing: 16
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: root.width < 600 ? 16 : 24
                spacing: 20
                RowLayout {
                    Layout.fillWidth: true
                    LV.InputField {
                        objectName: "environmentAppSearch"
                        Layout.fillWidth: true
                        Layout.maximumWidth: 280
                        Layout.minimumWidth: 80
                        mode: searchMode
                        placeholder: qsTr("Search %1").arg(root.collectionNames[root.collection].toLowerCase())
                        text: root.query
                        onTextChanged: root.query = text
                    }
                    Item { Layout.fillWidth: true }
                    EnvironmentButton { objectName: "environmentAddApp"; visible: root.collection >= 2; text: qsTr("Add app"); tone: LV.AbstractButton.Borderless; onClicked: root.addRequested() }
                    EnvironmentButton { objectName: "environmentUpdates"; text: qsTr("Updates · %1").arg(root.updates); tone: root.updatesOnly ? LV.AbstractButton.Default : LV.AbstractButton.Borderless; onClicked: root.updatesOnly = !root.updatesOnly }
                }
                AppGroup { visible: root.collection === 0; heading: qsTr("Installed on this device"); summary: root.deviceName + " · " + qsTr("%1 apps").arg(root.installed.length); entries: root.matching(root.installed) }
                AppGroup { visible: root.collection === 1; heading: qsTr("Available to install"); summary: qsTr("Explore more apps and manage their licenses in one place."); entries: root.matching(root.available) }
                AppGroup { visible: root.collection === 1; heading: qsTr("All apps"); summary: qsTr("Browse %1 apps for your devices.").arg(root.apps.length); entries: root.matching(root.apps) }
                Repeater {
                    model: root.collection === 2 ? root.otherDevices : []
                    AppGroup {
                        required property var modelData
                        heading: modelData.name
                        summary: modelData.status + " · " + qsTr("%1 apps").arg((modelData.apps || []).length)
                        entries: root.matching(modelData.apps || [])
                        emptyMessage: qsTr("App inventory has not been shared by this device.")
                    }
                }
                EnvironmentSection {
                    visible: root.collection === 2 && root.otherDevices.length === 0
                    title: qsTr("Other devices")
                    EnvironmentRow { label: qsTr("No connected device inventory"); description: qsTr("Add a device in Environment → Devices."); value: qsTr("Devices"); onClicked: root.addRequested() }
                }
                LV.Label { visible: root.collection === 3; Layout.fillWidth: true; style: header2; text: qsTr("Quick launch") }
                Rectangle {
                    visible: root.collection === 3
                    Layout.fillWidth: true
                    implicitHeight: dock.implicitHeight + 40
                    color: LV.Theme.panelBackground04
                    radius: 16
                    Flow {
                        id: dock
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20 }
                        spacing: 24
                        Repeater {
                            model: root.webApps
                            LV.AbstractButton {
                                id: webLaunch
                                required property var modelData
                                width: 64; height: 64
                                horizontalPadding: 0; verticalPadding: 0
                                background: Item {}
                                Accessible.name: modelData.name
                                contentItem: EnvironmentBadge { symbol: webLaunch.modelData.symbol; paletteIndex: webLaunch.modelData.palette || 0 }
                                onClicked: root.openRequested(modelData)
                            }
                        }
                    }
                }
                AppGroup { visible: root.collection === 3; heading: qsTr("Available in your browser"); summary: qsTr("No installation required · %1 web apps").arg(root.webApps.length); entries: root.matching(root.webApps) }
                LV.Label {
                    Layout.fillWidth: true
                    visible: root.collection === 1
                    text: qsTr("Installation and updates become available when a verified release is published. License status is shown only when verified.")
                    style: caption; wrapMode: Text.Wrap; sizeToContentHeight: true
                }
            }
        }
    }
    component AppGroup: ColumnLayout {
        id: group
        required property string heading
        required property string summary
        required property var entries
        property string emptyMessage: qsTr("No apps match this view.")
        Layout.fillWidth: true
        spacing: 16
        LV.Label { Layout.fillWidth: true; text: group.heading; style: header2 }
        LV.Label { Layout.fillWidth: true; text: group.summary; style: caption; wrapMode: Text.Wrap; sizeToContentHeight: true }
        Grid {
            id: cardsGrid
            Layout.fillWidth: true
            columns: width >= 780 ? 3 : width >= 510 ? 2 : 1
            spacing: 16
            Repeater {
                model: group.entries
                EnvironmentCard {
                    required property var modelData
                    entry: modelData
                    objectName: "environmentApp-" + modelData.id
                    width: (cardsGrid.width - 16 * (cardsGrid.columns - 1)) / cardsGrid.columns
                    height: 286
                    onDetailsRequested: root.detailsRequested(entry)
                    onPrimaryRequested: root.openRequested(entry)
                }
            }
        }
        LV.Label { visible: group.entries.length === 0; Layout.fillWidth: true; text: group.emptyMessage; style: description; wrapMode: Text.Wrap; sizeToContentHeight: true }
    }
}
