pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society
import ".." as SocietyUi

Item {
    id: root
    objectName: "modelsView"
    required property StorageModels catalog
    required property ModelImporter modelImporter
    property var notifications: null
    property string path: ""
    property string modelRoot: ""
    property var breadcrumbs: []
    property bool touchNavigation: false
    property bool importing: false
    property bool organizing: false
    property bool importEnabled: true
    property bool showImportHistory: false
    property string importStatus: ""
    property string importError: ""
    property bool previouslyVisible: false
    readonly property alias selectedPath: browser.selectedPath
    readonly property alias listMode: browser.listMode
    signal importRequested()
    signal cancelImportRequested()
    signal fileRequested(string path)
    signal folderRequested(string path)
    signal upRequested()

    function jobStateLabel(state) {
        const labels = { waiting: qsTr("Waiting"), waitingNode: qsTr("Waiting for device"),
            waitingPermission: qsTr("Waiting for access"), running: qsTr("In progress"),
            partial: qsTr("Partially completed"), failed: qsTr("Failed"),
            cancelRequested: qsTr("Cancelling"), cancelled: qsTr("Cancelled"),
            succeeded: qsTr("Completed"), attention: qsTr("Needs verification"),
            pending: qsTr("Waiting"), unknown: qsTr("Needs verification"), skipped: qsTr("Skipped") }
        return labels[state] || state
    }
    function checkpointDescription(checkpoint) {
        if (!checkpoint) return ""
        try {
            const value = JSON.parse(checkpoint)
            if (value.phase === "published") return qsTr("Saved file: %1").arg(value.path)
            if (value.phase === "copying") return qsTr("Copy checkpoint: %1 bytes").arg(value.bytes)
        } catch (error) {}
        return ""
    }
    onVisibleChanged: {
        if (visible) {
            if (previouslyVisible && !catalog.loading) catalog.refresh()
            previouslyVisible = true
        }
    }
    Component.onCompleted: if (visible) previouslyVisible = true
    Connections {
        target: root.modelImporter
        function onRecategorized(containerPath, previousPath, path) {
            root.catalog.refresh()
            if (browser.directoryModel) browser.directoryModel.refresh()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        LV.HStack {
            objectName: "modelImportBanner"
            Layout.fillWidth: true
            Layout.leftMargin: browser.contentInset
            Layout.rightMargin: browser.contentInset
            Layout.topMargin: visible ? browser.contentInset : 0
            visible: root.importing || root.organizing || root.modelImporter.jobs.length > 0
            spacing: LV.Theme.gap8
            LV.Label {
                objectName: "modelImportProgress"
                Layout.fillWidth: true
                style: description
                text: root.importing || root.organizing ? root.importStatus : ""
                textFormat: Text.PlainText
                elide: Text.ElideRight
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            LV.LabelButton {
                visible: root.importing
                text: qsTr("Cancel")
                onClicked: root.cancelImportRequested()
            }
            LV.LabelButton {
                objectName: "modelImportHistoryToggle"
                visible: root.modelImporter.jobs.length > 0
                text: root.showImportHistory ? qsTr("Hide import history") : qsTr("Import history (%1)").arg(root.modelImporter.jobs.length)
                onClicked: root.showImportHistory = !root.showImportHistory
            }
        }
        Controls.ScrollView {
            objectName: "modelImportHistory"
            Layout.fillWidth: true
            Layout.leftMargin: browser.contentInset
            Layout.rightMargin: browser.contentInset
            Layout.preferredHeight: visible ? Math.min(root.height * 0.35, history.implicitHeight) : 0
            visible: root.showImportHistory && root.modelImporter.jobs.length > 0
            clip: true
            contentWidth: availableWidth
            LV.VStack {
                id: history
                width: parent.width
                spacing: LV.Theme.gap8
                Repeater {
                    model: root.showImportHistory ? root.modelImporter.jobs : []
                    delegate: LV.VStack {
                        id: jobEntry
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: LV.Theme.gap8
                        LV.HStack {
                            Layout.fillWidth: true
                            spacing: LV.Theme.gap8
                            LV.Label {
                                Layout.fillWidth: true
                                style: description
                                text: qsTr("%1 · %2 succeeded · %3 failed · %4 need verification")
                                    .arg(root.jobStateLabel(jobEntry.modelData.state)).arg(jobEntry.modelData.succeeded)
                                    .arg(jobEntry.modelData.failed).arg(jobEntry.modelData.unknown)
                                wrapMode: Text.WordWrap
                                sizeToContentHeight: true
                            }
                            LV.LabelButton {
                                text: qsTr("Details")
                                onClicked: root.modelImporter.selectJob(jobEntry.modelData.id)
                            }
                            LV.LabelButton {
                                text: jobEntry.modelData.pending > 0 ? qsTr("Resume") : qsTr("Retry failed")
                                visible: jobEntry.modelData.failed > 0 || jobEntry.modelData.pending > 0
                                enabled: !root.importing && root.importEnabled
                                onClicked: root.modelImporter.retryFailed(jobEntry.modelData.id)
                            }
                        }
                        LV.Label {
                            visible: !!jobEntry.modelData.reason || !!jobEntry.modelData.stage
                            Layout.fillWidth: true
                            style: caption
                            text: jobEntry.modelData.reason || jobEntry.modelData.stage || ""
                            textFormat: Text.PlainText
                            wrapMode: Text.WordWrap
                            sizeToContentHeight: true
                        }
                        Repeater {
                            model: root.modelImporter.selectedJobId === jobEntry.modelData.id ? root.modelImporter.jobItems : []
                            delegate: LV.Label {
                                required property var modelData
                                Layout.fillWidth: true
                                style: caption
                                text: modelData.name + " · " + root.jobStateLabel(modelData.state)
                                    + qsTr(" · %1 attempt(s)").arg(modelData.attempts)
                                    + (modelData.error ? " · " + modelData.error : "")
                                    + (modelData.artifact ? " · " + modelData.artifact : "")
                                    + (root.checkpointDescription(modelData.checkpoint) ? " · " + root.checkpointDescription(modelData.checkpoint) : "")
                                textFormat: Text.PlainText
                                wrapMode: Text.WordWrap
                                sizeToContentHeight: true
                            }
                        }
                        LV.LabelButton {
                            visible: root.modelImporter.selectedJobId === jobEntry.modelData.id && root.modelImporter.hasMoreJobItems
                            text: qsTr("Show more items")
                            onClicked: root.modelImporter.loadMoreJobItems()
                        }
                    }
                }
            }
        }
        SocietyUi.FilesBrowser {
            id: browser
            objectName: "modelFilesBrowser"
            Layout.fillWidth: true
            Layout.fillHeight: true
            path: root.path
            breadcrumbs: root.breadcrumbs
            contentInset: root.touchNavigation && root.width < 760 ? LV.Theme.gap16 : LV.Theme.gap24
            modelsMode: true
            touchNavigation: root.touchNavigation
            modelRoot: root.modelRoot
            modelCategories: root.catalog.categories
            modelGroups: root.catalog.groups
            modelImportEnabled: root.importEnabled
            modelCategoryEnabled: !root.importing
            onActivated: function(path, directory) {
                if (directory) root.folderRequested(path)
                else root.fileRequested(path)
            }
            onModelImportRequested: root.importRequested()
            onModelUpRequested: root.upRequested()
            onModelCategoryRequested: function(path, category) { root.modelImporter.recategorize(path, category) }
            onModelModified: root.catalog.refresh()
        }
    }
}
