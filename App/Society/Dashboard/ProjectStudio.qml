pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV
import Society
import ".." as SocietyUi

Controls.ScrollView {
    id: root
    objectName: "projectStudioPane"
    required property ProjectWorkspace workspace
    signal addResourceRequested()
    contentWidth: availableWidth
    clip: true
    Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff

    LV.VStack {
        width: root.availableWidth
        spacing: 12
        LV.HStack {
            Layout.fillWidth: true
            spacing: 8
            LV.VStack {
                Layout.fillWidth: true
                spacing: 3
                LV.Label {
                    objectName: "projectStudioName"
                    Layout.fillWidth: true
                    text: root.workspace.project.name || qsTr("Select a project")
                    textFormat: Text.PlainText
                    style: title2
                    elide: Text.ElideRight
                }
                LV.Label {
                    visible: !!root.workspace.project.projectId
                    Layout.fillWidth: true
                    text: String(root.workspace.project.status || "draft").replace("_", " ")
                        + " · " + qsTr("Revision %1").arg(root.workspace.project.revision)
                    style: caption
                }
            }
            LV.LabelButton {
                text: qsTr("Refresh")
                tone: LV.AbstractButton.Default
                enabled: !root.workspace.busy
                onClicked: root.workspace.refresh()
            }
        }
        LV.Label {
            visible: !root.workspace.project.projectId
            Layout.fillWidth: true
            text: qsTr("Select a project to see its goal, resources and activity.")
            style: description; wrapMode: Text.WordWrap; sizeToContentHeight: true
        }
        LV.VStack {
            visible: !!root.workspace.project.projectId
            Layout.fillWidth: true
            spacing: 12
            SocietyUi.PanelOutline {
                Layout.fillWidth: true
                implicitHeight: Math.max(150, Math.min(250, width * 0.35))
                radius: LV.Theme.radiusMd
                border.color: LV.Theme.panelBackground12
                clip: true
                Image {
                    id: cover
                    anchors.fill: parent
                    source: root.workspace.project.preview || ""
                    asynchronous: true
                    sourceSize.width: 1000
                    fillMode: Image.PreserveAspectCrop
                }
                LV.Label {
                    anchors.centerIn: parent
                    visible: cover.status !== Image.Ready
                    text: qsTr("Choose a cover in Project definition")
                    style: description
                }
            }
            LV.Label { text: qsTr("GOAL"); style: caption }
            LV.Label {
                objectName: "projectStudioGoal"
                Layout.fillWidth: true
                text: root.workspace.project.goal || ""
                textFormat: Text.PlainText
                style: body; wrapMode: Text.WordWrap; sizeToContentHeight: true
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: LV.Theme.panelBackground12 }
            LV.HStack {
                Layout.fillWidth: true
                LV.Label { Layout.fillWidth: true; text: qsTr("Canvases & assets · %1").arg(root.workspace.resources.length); style: body }
                LV.LabelButton { objectName: "projectStudioAddResource"; text: qsTr("+ Add"); tone: LV.AbstractButton.Default; enabled: !root.workspace.busy; onClicked: root.addResourceRequested() }
            }
            LV.Label {
                visible: root.workspace.resources.length === 0
                Layout.fillWidth: true
                text: qsTr("No resources have been added to this project.")
                style: description; wrapMode: Text.WordWrap; sizeToContentHeight: true
            }
            Repeater {
                model: root.workspace.resources
                SocietyUi.PanelOutline {
                    id: resource
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 56
                    radius: LV.Theme.radiusSm
                    border.color: LV.Theme.panelBackground12
                    LV.HStack {
                        anchors.fill: parent; anchors.margins: 8; spacing: 8
                        LV.VStack {
                            Layout.fillWidth: true; spacing: 3
                            LV.Label { Layout.fillWidth: true; text: resource.modelData.name; textFormat: Text.PlainText; style: body; elide: Text.ElideRight }
                            LV.Label { Layout.fillWidth: true; text: resource.modelData.purpose || ""; textFormat: Text.PlainText; style: caption; elide: Text.ElideRight }
                        }
                        LV.LabelButton { text: qsTr("Open"); tone: LV.AbstractButton.Borderless; enabled: !root.workspace.busy; onClicked: root.workspace.openResource(resource.modelData.id) }
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: LV.Theme.panelBackground12 }
            LV.Label { text: qsTr("Generation"); style: body }
            LV.Label {
                Layout.fillWidth: true
                text: qsTr("%1 resources marked as derived. Generation history is not linked to projects yet.")
                    .arg(root.workspace.resources.filter(function(resource) { return resource.derived }).length)
                style: description; wrapMode: Text.WordWrap; sizeToContentHeight: true
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: LV.Theme.panelBackground12 }
            LV.Label { text: qsTr("Versions & activity"); style: body }
            LV.Label {
                visible: root.workspace.history.length === 0
                Layout.fillWidth: true
                text: qsTr("No recorded revisions.")
                style: description
            }
            Repeater {
                model: root.workspace.history
                LV.VStack {
                    id: revision
                    required property var modelData
                    Layout.fillWidth: true; spacing: 2
                    LV.Label {
                        Layout.fillWidth: true
                        text: qsTr("Revision %1 · %2").arg(revision.modelData.revision).arg(revision.modelData.timestamp)
                        textFormat: Text.PlainText; style: caption; elide: Text.ElideRight
                    }
                    LV.Label {
                        Layout.fillWidth: true
                        text: (revision.modelData.changes || []).join(" · ")
                        textFormat: Text.PlainText; style: description; wrapMode: Text.WordWrap; sizeToContentHeight: true
                    }
                    LV.Label {
                        visible: !!revision.modelData.actor
                        Layout.fillWidth: true; text: revision.modelData.actor
                        textFormat: Text.PlainText; style: caption; elide: Text.ElideMiddle
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: LV.Theme.panelBackground12 }
            LV.Label { text: qsTr("Delivery & milestones"); style: body }
            LV.Label {
                Layout.fillWidth: true
                text: qsTr("Delivery and task providers are not connected.")
                style: description; wrapMode: Text.WordWrap; sizeToContentHeight: true
            }
        }
    }
}
