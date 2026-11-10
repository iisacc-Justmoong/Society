pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society
import "../Files"
import ".." as SocietyUi

LV.HStack {
    id: root
    objectName: "dashboardView"
    property bool touchNavigation: false
    readonly property int contentInset: touchNavigation && width < 760 ? 16 : 24
    property bool calendarPageVisible: false
    property bool activityPageVisible: false
    property bool calendarInitialized: false
    property bool calendarReady: false
    property bool projectsPageVisible: false
    property bool projectsReady: false
    property string projectActorId: ""
    property DriveController drive: null
    // Host applications may connect this interface when an agent session is configured.
    property bool agentAvailable: false
    signal agentPromptRequested(string prompt)
    readonly property string filesRoot: {
        if (!drive || !drive.contentsAvailable) return ""
        const section = drive.sections.find(function(item) { return item.key === "files" })
        return section ? String(section.path) : ""
    }
    property string dashboardPath: ""
    onFilesRootChanged: dashboardPath = filesRoot
    function goHome() {
        calendarPageVisible = false; activityPageVisible = false; projectsPageVisible = false
        dashboardScroll.contentItem.contentY = 0
    }
    function showProjects() { goHome(); projectsPageVisible = true }
    onCalendarPageVisibleChanged: if (!calendarPageVisible) calendarReady = false
    required property DashboardFiles viewModel
    property StorageDirectoryModel sharedFilesDirectory: null
    readonly property bool filesBrowserLoading: dashboardFilesLoader.item ? dashboardFilesLoader.item.loading : false
    property StorageNavigation navigation: null
    function showCalendar(section: string): void {
        goHome()
        calendarInitialized = true
        calendarView.resetDetails()
        calendarView.expandedSection = section
        calendarPageVisible = section.length === 0
        activityPageVisible = section.length > 0
    }
    function navigateDashboard(path: string): void {
        if (!drive || !drive.navigate(path)) return
        const resolved = drive.currentPath
        if (resolved === filesRoot || resolved.indexOf(filesRoot + "/") === 0)
            dashboardPath = resolved
    }
    function upDashboard(): void {
        if (dashboardPath === filesRoot) return
        const separator = dashboardPath.lastIndexOf("/")
        if (separator >= filesRoot.length) navigateDashboard(dashboardPath.substring(0, separator))
    }
    property DashboardCalendar calendarModel: DashboardCalendar {
        objectName: "dashboardCalendarModel"
        containerPath: root.calendarInitialized ? root.viewModel.containerPath : ""
        active: root.visible && (root.calendarPageVisible || root.activityPageVisible)
            && Qt.application.state === Qt.ApplicationActive
    }
    property CalendarWorkspace calendarWorkspace: CalendarWorkspace {
        objectName: "calendarWorkspaceModel"
        calendar: root.calendarModel
        active: root.visible && root.calendarPageVisible && Qt.application.state === Qt.ApplicationActive
    }
    property ProjectWorkspace projectWorkspace: ProjectWorkspace {
        objectName: "projectWorkspaceModel"
        containerPath: root.viewModel.containerPath
        actorId: root.projectActorId
        active: root.visible && root.projectsPageVisible
    }
    Connections {
        target: root.viewModel
        function onLoadingChanged() {
            if (root.calendarInitialized && !root.viewModel.loading && root.calendarModel.databasePath.length === 0)
                root.calendarModel.refresh()
        }
    }
    readonly property var recentFiles: viewModel.recentFiles
    readonly property var historyFiles: viewModel.generationHistory
    readonly property bool loading: viewModel.loading
    readonly property string query: viewModel.query
    property string driveError: ""
    readonly property string errorString: viewModel.errorString || driveError
    property bool initialized: false
    property string deviceStatus: ""
    signal sectionRequested(string section)
    signal fileRequested(string path)
    signal revealRequested(string path)
    signal devicesRequested()
    signal featureRequested(string feature)
    spacing: 0
    alignment: Qt.AlignTop
    // DashboardFiles refreshes when its container path changes. Do not turn
    // the initial binding and visibility changes into a second full scan.
    Component.onCompleted: initialized = true
    onVisibleChanged: {
        if (visible && initialized) {
            if (!viewModel.loading) viewModel.refresh()
            if (calendarPageVisible || activityPageVisible) calendarModel.refresh()
        }
        if (!visible) fileMenu.close()
    }
    function openFileMenu(file, card) {
        fileMenu.filePath = file.path
        fileMenu.folderPath = file.folderPath
        fileMenu.openFor(card, 0, card.height + LV.Theme.gap2)
    }
    component AgentStatusPanel: LV.VStack {
        objectName: "dashboardAgentActivity"
        spacing: 12
        LV.HStack {
            Layout.fillWidth: true
            LV.Label { Layout.fillWidth: true; text: qsTr("Activity"); style: caption }
            LV.LabelButton { text: qsTr("Stop"); tone: LV.AbstractButton.Default; enabled: false }
        }
        LV.Label {
            Layout.fillWidth: true; text: root.agentAvailable ? qsTr("Session activity is unavailable.") : qsTr("Agent session is not connected.")
            style: description; wrapMode: Text.WordWrap; sizeToContentHeight: true
        }
        LV.HStack {
            Layout.fillWidth: true
            LV.Label { Layout.fillWidth: true; text: qsTr("Approvals"); style: caption }
            LV.LabelButton { text: qsTr("Review"); tone: LV.AbstractButton.Default; enabled: false }
        }
        LV.Label {
            Layout.fillWidth: true; text: root.agentAvailable ? qsTr("Approval details are unavailable.") : qsTr("Connect an agent session to review requests.")
            style: description; wrapMode: Text.WordWrap; sizeToContentHeight: true
        }
    }
    DashboardSidebar {
        objectName: "dashboardSidebar"
        visible: root.width >= 760
        Layout.preferredWidth: 170
        Layout.fillHeight: true
        touchNavigation: root.touchNavigation
        activeKey: root.projectsPageVisible ? "projects" : root.calendarPageVisible ? "calendar" : root.activityPageVisible ? "activity" : "home"
        guilds: root.navigation ? root.navigation.guilds : []
        organizations: root.navigation ? root.navigation.organizations : []
        onHomeRequested: root.goHome()
        onCalendarRequested: root.showCalendar("")
        onProjectsRequested: root.showProjects()
        onActivityRequested: root.showCalendar("activity")
        onFeatureRequested: function(feature) { root.featureRequested(feature) }
        onWorkspaceRequested: function(kind, id) { if (root.navigation) root.navigation.activate(kind, id) }
    }
    Item {
        id: content
        Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumWidth: 0
        Loader {
            id: calendarLoader
            objectName: "calendarWorkspaceLoader"
            anchors.fill: parent
            active: root.calendarPageVisible && (root.visible || root.calendarReady)
            visible: root.calendarPageVisible && status === Loader.Ready
            asynchronous: true
            onLoaded: root.calendarReady = true
            sourceComponent: CalendarWorkspaceView {
                workspace: root.calendarWorkspace; calendar: root.calendarModel
                onFileRequested: function(path) { root.fileRequested(path) }
            }
        }
        Loader {
            id: projectsLoader
            objectName: "dashboardProjectsLoader"
            anchors.fill: parent
            asynchronous: true
            active: (root.visible && root.projectsPageVisible) || root.projectsReady
            visible: root.projectsPageVisible && status === Loader.Ready
            onLoaded: root.projectsReady = true
            sourceComponent: ProjectsView { workspace: root.projectWorkspace }
        }
        LV.Label {
            anchors.centerIn: parent
            visible: (root.projectsPageVisible && projectsLoader.status === Loader.Loading)
                || (root.calendarPageVisible && calendarLoader.status === Loader.Loading)
            text: root.calendarPageVisible ? qsTr("Loading calendar…") : qsTr("Loading projects…")
            style: caption
        }
        Controls.ScrollView {
            anchors.fill: parent
            visible: root.activityPageVisible
            contentWidth: availableWidth
            clip: true
            DashboardCalendarView {
                id: calendarView
                width: parent ? parent.width : 0
                model: root.calendarModel
                onFileRequested: function(path) { root.fileRequested(path) }
            }
        }
        SocietyUi.PanelOutline {
            id: activityPanel
            visible: !root.calendarPageVisible && !root.projectsPageVisible && !root.activityPageVisible && content.width >= 1000
            anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right
            width: Math.min(355, content.width * 0.27)
            border.color: LV.Theme.panelBackground12
            AgentStatusPanel { anchors.fill: parent; anchors.margins: 16 }
        }
        Controls.ScrollView {
            id: dashboardScroll
            objectName: "dashboardScroll"
            anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.left: parent.left
            anchors.right: activityPanel.visible ? activityPanel.left : parent.right
            visible: !root.calendarPageVisible && !root.projectsPageVisible && !root.activityPageVisible
            contentWidth: availableWidth
            clip: true
            Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
            LV.VStack {
                width: dashboardScroll.availableWidth
                spacing: 0
                Item {
                    Layout.fillWidth: true
                    implicitHeight: page.implicitHeight + root.contentInset * 2
                    LV.VStack {
                        id: page
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        anchors.margins: root.contentInset
                        height: implicitHeight
                        spacing: 16
                        SocietyUi.PanelOutline {
                            objectName: "dashboardAgentComposer"
                            Layout.fillWidth: true
                            implicitHeight: composer.implicitHeight + 24
                            radius: LV.Theme.radiusMd; border.color: LV.Theme.panelBackground12
                            LV.VStack {
                                id: composer
                                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12
                                spacing: 8
                                LV.HStack {
                                    Layout.fillWidth: true; spacing: 8
                                    LV.Label { Layout.fillWidth: true; text: qsTr("Start a draft project with your agent"); style: title2; elide: Text.ElideRight }
                                    LV.IconButton {
                                        iconName: "generalmoreHorizontal"; tone: LV.AbstractButton.Default
                                        enabled: false; Accessible.name: qsTr("Choose agent model")
                                    }
                                }
                                LV.InputField {
                                    id: agentInput
                                    objectName: "dashboardAgentPrompt"
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("Describe what you want to create")
                                    Accessible.name: qsTr("Agent prompt")
                                }
                                LV.HStack {
                                    Layout.fillWidth: true; spacing: 8
                                    LV.IconButton {
                                        iconName: "addFile"; tone: LV.AbstractButton.Default
                                        enabled: false; Accessible.name: qsTr("Attach a file")
                                    }
                                    LV.DropdownButton {
                                        visible: composer.width >= 480
                                        text: qsTr("Reasoning"); tone: LV.AbstractButton.Default
                                        enabled: false; Accessible.name: qsTr("Reasoning level")
                                    }
                                    LV.DropdownButton {
                                        visible: composer.width >= 600
                                        text: qsTr("Permissions"); tone: LV.AbstractButton.Default
                                        enabled: false; Accessible.name: qsTr("Agent permissions")
                                    }
                                    LV.Label { Layout.fillWidth: true; text: root.agentAvailable ? qsTr("Agent ready") : qsTr("Agent not connected"); style: caption }
                                    LV.IconButton {
                                        visible: composer.width >= 480
                                        iconName: "cwmMicOn"; tone: LV.AbstractButton.Default
                                        enabled: false; Accessible.name: qsTr("Voice input")
                                    }
                                    LV.IconButton {
                                        objectName: "dashboardAgentSubmit"
                                        iconName: "submitDB"; tone: LV.AbstractButton.Primary
                                        Accessible.name: qsTr("Send")
                                        enabled: root.agentAvailable && agentInput.text.trim().length > 0
                                        onClicked: root.agentPromptRequested(agentInput.text)
                                    }
                                }
                            }
                        }
                        DashboardFileSection {
                            Layout.fillWidth: true
                            title: qsTr("Recent files"); sectionKey: "files"; objectSuffix: "RecentFiles"
                            rows: root.recentFiles; loading: root.loading; query: root.query; menuVisible: fileMenu.visible
                            onViewAllRequested: function(key) { root.sectionRequested(key) }
                            onFileRequested: function(path) { root.fileRequested(path) }
                            onMenuRequested: function(file, card) { root.openFileMenu(file, card) }
                        }
                        SocietyUi.PanelOutline {
                            objectName: "dashboardFilesPanel"
                            Layout.fillWidth: true; implicitHeight: 497
                            radius: LV.Theme.radiusMd; border.color: LV.Theme.panelBackground12
                            LV.VStack {
                                anchors.fill: parent; anchors.margins: 12; spacing: 8
                                LV.HStack {
                                    Layout.fillWidth: true; spacing: 8
                                    LV.Label { Layout.fillWidth: true; text: qsTr("Files"); style: body }
                                    LV.LabelButton {
                                        objectName: "dashboardFilesBack"
                                        visible: root.dashboardPath.length > 0 && root.dashboardPath !== root.filesRoot
                                        text: qsTr("Back"); tone: LV.AbstractButton.Default; onClicked: root.upDashboard()
                                    }
                                    LV.LabelButton { text: qsTr("View all"); tone: LV.AbstractButton.Borderless; onClicked: root.sectionRequested("files") }
                                }
                                LV.Label {
                                    visible: root.filesRoot.length === 0
                                    Layout.fillWidth: true
                                    text: qsTr("Files are available when the Society drive is ready.")
                                    style: description; wrapMode: Text.WordWrap; sizeToContentHeight: true
                                }
                                Loader {
                                    id: dashboardFilesLoader
                                    Layout.fillWidth: true; Layout.fillHeight: true
                                    active: root.visible && dashboardScroll.visible && root.filesRoot.length > 0
                                    sourceComponent: SocietyUi.FilesBrowser {
                                        objectName: "dashboardFilesBrowser"
                                        path: root.dashboardPath
                                        sharedDirectoryModel: root.sharedFilesDirectory
                                        contentInset: 0; contentSpacing: LV.Theme.gap8
                                        onActivated: function(path, directory) {
                                            if (directory) root.navigateDashboard(path)
                                            else root.fileRequested(path)
                                        }
                                    }
                                }
                            }
                        }
                        DashboardFileSection {
                            Layout.fillWidth: true
                            title: qsTr("Generation history"); sectionKey: "generation-history"; objectSuffix: "GenerationHistory"
                            rows: root.historyFiles; loading: root.loading; query: root.query; menuVisible: fileMenu.visible
                            onViewAllRequested: function(key) { root.sectionRequested(key) }
                            onFileRequested: function(path) { root.fileRequested(path) }
                            onMenuRequested: function(file, card) { root.openFileMenu(file, card) }
                        }
                        AgentStatusPanel { visible: !activityPanel.visible; Layout.fillWidth: true }
                        LV.Label {
                            visible: root.errorString.length > 0 || fileMenu.actions.status.length > 0
                            Layout.fillWidth: true; style: description
                            text: fileMenu.actions.errorString || root.errorString || fileMenu.actions.status
                            textFormat: Text.PlainText; wrapMode: Text.WordWrap; sizeToContentHeight: true
                        }
                    }
                }
            }
        }
    }
    FileActionMenu {
        id: fileMenu
        objectName: "dashboardFileMenu"
        onOpenRequested: function(path) { root.fileRequested(path) }
        onRevealRequested: function(path) { root.revealRequested(path) }
        onModified: root.viewModel.refresh()
    }
}
