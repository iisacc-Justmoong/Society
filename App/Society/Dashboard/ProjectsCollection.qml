pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV
import Society
import ".." as SocietyUi

LV.VStack {
    id: root
    objectName: "projectNavigator"
    required property ProjectWorkspace workspace
    property string query: ""
    property string statusFilter: ""
    property string ownerFilter: ""
    property string sortBy: "name"
    signal newProjectRequested()
    readonly property var visibleProjects: {
        const q = (query + " " + localSearch.text).trim().toLocaleLowerCase()
        const terms = q.split(/\s+/).filter(function(term) { return term.length > 0 })
        return workspace.projects.filter(function(p) {
            const searchable = String(p.name || "") + " " + String(p.goal || "")
            const content = searchable.toLocaleLowerCase()
            return terms.every(function(term) { return content.indexOf(term) >= 0 })
                && (!root.statusFilter || p.status === root.statusFilter)
                && (!root.ownerFilter || p.owner === root.ownerFilter)
        }).slice().sort(function(a, b) {
            if (root.sortBy === "created")
                return String(b.createdAt || "").localeCompare(String(a.createdAt || ""))
            return String(a.name || "").localeCompare(String(b.name || ""))
        })
    }
    readonly property int visibleCount: visibleProjects.length
    spacing: 8

    LV.HStack {
        Layout.fillWidth: true
        LV.Label { Layout.fillWidth: true; text: qsTr("Projects"); style: body }
        LV.Label { text: root.visibleCount; style: caption }
        LV.LabelButton {
            objectName: "projectNavigatorNew"
            text: qsTr("+ New")
            tone: LV.AbstractButton.Borderless
            enabled: !root.workspace.busy && root.workspace.containerPath.length > 0
            onClicked: root.newProjectRequested()
        }
    }
    LV.InputField {
        id: localSearch
        objectName: "projectNavigatorSearch"
        Layout.fillWidth: true
        placeholderText: qsTr("Find a project")
        Accessible.name: qsTr("Find a project")
    }
    LV.Label {
        visible: root.visibleCount === 0
        Layout.fillWidth: true
        text: root.workspace.busy ? qsTr("Loading projects…")
            : (root.query.length || localSearch.text.length || root.statusFilter || root.ownerFilter)
                ? qsTr("No projects match these filters.")
                : qsTr("Create a project to define a goal and collect its resources.")
        style: description
        wrapMode: Text.WordWrap
        sizeToContentHeight: true
    }
    ListView {
        id: projectList
        objectName: "projectNavigatorList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 4
        model: root.visibleProjects
        boundsBehavior: Flickable.StopAtBounds
        Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
        delegate: SocietyUi.PanelOutline {
            id: row
            required property int index
            required property var modelData
            objectName: "projectNavigatorRow" + index
            width: projectList.width
            height: 54
            radius: LV.Theme.radiusSm
            border.width: 1
            border.color: row.modelData.key === root.workspace.selectedKey
                ? LV.Theme.accentGreen : LV.Theme.panelBackground12
            Accessible.role: Accessible.Button
            Accessible.name: row.modelData.name
            Accessible.selected: row.modelData.key === root.workspace.selectedKey
            activeFocusOnTab: true
            Keys.onReturnPressed: if (!root.workspace.busy) root.workspace.openProject(row.modelData.key)
            Keys.onEnterPressed: if (!root.workspace.busy) root.workspace.openProject(row.modelData.key)
            Keys.onSpacePressed: if (!root.workspace.busy) root.workspace.openProject(row.modelData.key)
            LV.VStack {
                anchors.fill: parent; anchors.margins: 8; spacing: 3
                LV.Label { Layout.fillWidth: true; text: row.modelData.name; textFormat: Text.PlainText; style: body; elide: Text.ElideRight }
                LV.Label {
                    Layout.fillWidth: true
                    text: String(row.modelData.status || "draft").replace("_", " ") + " · r" + row.modelData.revision
                    style: caption; elide: Text.ElideRight
                }
            }
            MouseArea {
                anchors.fill: parent
                enabled: !root.workspace.busy
                onClicked: root.workspace.openProject(row.modelData.key)
            }
        }
    }
}
