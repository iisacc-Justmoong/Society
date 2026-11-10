pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV
import Society
import ".." as SocietyUi

LV.VStack {
    id: root
    objectName: "projectsView"
    required property ProjectWorkspace workspace
    property bool showInspector: false
    property bool showNavigator: true
    property string lastSelectedKey: ""
    property string statusFilter: ""
    property string ownerFilter: ""
    property string sortBy: "name"
    readonly property bool wideLayout: width >= 1040
    readonly property var ownerOptions: {
        const values = [{value: "", label: qsTr("All owners")}]
        const seen = {}
        workspace.projects.forEach(function(p) {
            const owner = String(p.owner || "")
            if (owner && !seen[owner]) { seen[owner] = true; values.push({value: owner, label: owner}) }
        })
        return values
    }
    spacing: 8
    Component.onCompleted: workspace.viewMode = "studio"
    Connections {
        target: root.workspace
        function onResourceReady(url) { Qt.openUrlExternally(url); }
        function onChanged() {
            if (root.workspace.selectedKey !== root.lastSelectedKey) {
                root.lastSelectedKey = root.workspace.selectedKey;
                if (!root.wideLayout && root.lastSelectedKey.length) {
                    root.showNavigator = false;
                    root.showInspector = false;
                }
            }
        }
    }
    LV.HStack {
        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12; spacing: 8
        LV.VStack {
            Layout.fillWidth: true; spacing: 2
            LV.Label { text: qsTr("Projects"); style: title2 }
            LV.Label { text: qsTr("Organize goals, files and resources"); style: caption }
        }
        LV.Label { visible: root.workspace.busy; text: qsTr("Working…"); style: caption }
        LV.LabelButton {
            objectName: "projectListToggle"; visible: !root.wideLayout
            text: qsTr("Projects"); tone: LV.AbstractButton.Default
            onClicked: { root.showNavigator = true; root.showInspector = false }
        }
        LV.LabelButton {
            objectName: "projectInspectorToggle"; visible: !root.wideLayout
            text: root.showInspector ? qsTr("Overview") : qsTr("Definition"); tone: LV.AbstractButton.Default
            onClicked: { root.showNavigator = false; root.showInspector = !root.showInspector }
        }
        LV.LabelButton {
            objectName: "newProject"; text: root.width < 600 ? qsTr("+ New") : qsTr("+ New project")
            tone: LV.AbstractButton.Primary
            enabled: !root.workspace.busy && root.workspace.containerPath.length > 0
            onClicked: createDialog.open()
        }
    }
    LV.HStack {
        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12; spacing: 8
        LV.InputField {
            id: search; objectName: "projectSearch"; Layout.fillWidth: true
            placeholderText: qsTr("Search projects and goals"); Accessible.name: qsTr("Search projects")
        }
        CalendarChoice {
            visible: root.width >= 700; Layout.preferredWidth: 120
            value: root.statusFilter
            options: [{value:"",label:qsTr("All statuses")},{value:"draft",label:qsTr("Draft")},
                {value:"in_progress",label:qsTr("In progress")},{value:"completed",label:qsTr("Completed")},
                {value:"on_hold",label:qsTr("On hold")}]
            onChosen: function(value) { root.statusFilter = value }
        }
        CalendarChoice {
            visible: root.width >= 900; Layout.preferredWidth: 130
            value: root.ownerFilter; options: root.ownerOptions
            onChosen: function(value) { root.ownerFilter = value }
        }
        CalendarChoice {
            visible: root.width >= 700; Layout.preferredWidth: 115
            value: root.sortBy; options: [{value:"name",label:qsTr("Name")},{value:"created",label:qsTr("Created")}]
            onChosen: function(value) { root.sortBy = value }
        }
    }
    LV.Label {
        visible: root.workspace.errorString.length > 0 || root.workspace.warning.length > 0
        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
        text: root.workspace.errorString || root.workspace.warning
        style: description; wrapMode: Text.WordWrap; sizeToContentHeight: true
    }
    LV.HStack {
        Layout.fillWidth: true; Layout.fillHeight: true
        Layout.leftMargin: 12; Layout.rightMargin: 12; Layout.bottomMargin: 12
        spacing: 8
        SocietyUi.PanelOutline {
            visible: root.wideLayout || root.showNavigator
            Layout.preferredWidth: root.wideLayout ? 278 : root.width - 24
            Layout.minimumWidth: root.wideLayout ? 230 : 0
            Layout.fillWidth: !root.wideLayout; Layout.fillHeight: true
            radius: LV.Theme.radiusMd; border.color: LV.Theme.panelBackground12
            ProjectsCollection {
                anchors.fill: parent; anchors.margins: 12
                workspace: root.workspace; query: search.text
                statusFilter: root.statusFilter; ownerFilter: root.ownerFilter; sortBy: root.sortBy
                onNewProjectRequested: createDialog.open()
            }
        }
        SocietyUi.PanelOutline {
            visible: root.wideLayout || (!root.showNavigator && !root.showInspector)
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumWidth: 0
            radius: LV.Theme.radiusMd; border.color: LV.Theme.panelBackground12
            ProjectStudio {
                anchors.fill: parent; anchors.margins: 12
                workspace: root.workspace
                onAddResourceRequested: {
                    root.showNavigator = false;
                    if (!root.wideLayout) root.showInspector = true;
                    inspector.showAddResource()
                }
            }
        }
        SocietyUi.PanelOutline {
            visible: root.wideLayout || (!root.showNavigator && root.showInspector)
            Layout.preferredWidth: root.wideLayout ? 312 : root.width - 24
            Layout.minimumWidth: root.wideLayout ? 260 : 0
            Layout.fillWidth: !root.wideLayout; Layout.fillHeight: true
            radius: LV.Theme.radiusMd; border.color: LV.Theme.panelBackground12
            ProjectInspector { id: inspector; anchors.fill: parent; anchors.margins: 12; workspace: root.workspace }
        }
    }
    Controls.Popup {
        id: createDialog
        objectName: "createProjectDialog"
        parent: Controls.Overlay.overlay
        x: parent ? (parent.width - width) / 2 : 0
        y: parent ? (parent.height - height) / 2 : 0
        width: Math.min(540, parent ? parent.width - 32 : 540)
        height: Math.min(490, parent ? parent.height - 32 : 490)
        modal: true; focus: true; padding: 20
        closePolicy: Controls.Popup.CloseOnEscape
        Controls.Overlay.modal: Rectangle { color: LV.Theme.overlayBackdrop }
        background: Rectangle { color: LV.Theme.surfacePanel; radius: LV.Theme.radiusLg; border.color: LV.Theme.panelBackground12 }
        property bool creating: false
        Connections {
            target: root.workspace
            function onChanged() {
                if (createDialog.creating && !root.workspace.busy) {
                    createDialog.creating = false;
                    if (!root.workspace.errorString.length) createDialog.close();
                }
            }
        }
        contentItem: LV.VStack {
            spacing: 8
            LV.Label { text: qsTr("New project"); style: title2 }
            LV.Label { Layout.fillWidth: true; text: qsTr("Define the goal first. Add the files, resources and relationships needed to achieve it after creation."); style: description; wrapMode: Text.WordWrap; sizeToContentHeight: true }
            LV.Label { text: qsTr("Name"); style: caption }
            LV.InputField { id: newName; objectName: "newProjectName"; Layout.fillWidth: true; placeholderText: qsTr("Project name") }
            LV.Label { text: qsTr("Goal"); style: caption }
            LV.TextEditor { id: newGoal; objectName: "newProjectGoal"; Layout.fillWidth: true; filePath: ""; fieldMinHeight: 72; editorHeight: 72; Component.onCompleted: editorItem.textFormat = TextEdit.PlainText; placeholderText: qsTr("What is this project intended to achieve?") }
            LV.Label { text: qsTr("Requirements, constraints, completion conditions"); style: caption }
            LV.InputField { id: newRequirements; objectName: "newProjectRequirements"; Layout.fillWidth: true; placeholderText: qsTr("Requirements; separate with semicolons") }
            LV.InputField { id: newConstraints; Layout.fillWidth: true; placeholderText: qsTr("Constraints; separate with semicolons") }
            LV.InputField { id: newCompletion; Layout.fillWidth: true; placeholderText: qsTr("Completion conditions; separate with semicolons") }
            LV.Label { visible: createDialog.creating || root.workspace.errorString.length > 0; Layout.fillWidth: true; text: root.workspace.errorString || qsTr("Creating project…"); style: description; wrapMode: Text.WordWrap; sizeToContentHeight: true }
            Item { Layout.fillHeight: true }
            LV.HStack {
                Layout.fillWidth: true
                LV.LabelButton { text: qsTr("Cancel"); tone: LV.AbstractButton.Default; enabled: !root.workspace.busy; onClicked: createDialog.close() }
                Item { Layout.fillWidth: true }
                LV.LabelButton {
                    objectName: "confirmCreateProject"
                    text: qsTr("Create project"); tone: LV.AbstractButton.Primary
                    enabled: !root.workspace.busy && newName.text.trim().length > 0 && newGoal.text.trim().length > 0
                    onClicked: {
                        function items(text) { return text.split(";").map(function(s) { return s.trim(); }).filter(function(s) { return s.length > 0; }); }
                        createDialog.creating = true;
                        root.workspace.createProject({name:newName.text,goal:newGoal.text,requirements:items(newRequirements.text),constraints:items(newConstraints.text),completionConditions:items(newCompletion.text)});
                    }
                }
            }
        }
    }
}
