pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import "../Dashboard"

Item {
    id: root
    objectName: "toolsView"
    property string modelsDirectory: ""
    property bool modelsBusy: false
    property bool touchNavigation: false
    property string activeTool: ""
    property string query: ""
    signal generateRequested(string prompt, string mediaType, string aspectRatio, int count)
    readonly property bool canGoBack: activeTool.length > 0
    readonly property var tools: [
        { key: "model-merge", title: qsTr("Model merge"), category: qsTr("Models"), symbol: "M", tint: "#92B57B",
          description: qsTr("Combine checkpoints and LoRA into a new model.") }
    ]
    readonly property var filteredTools: tools.filter(function(tool) {
        return (tool.title + " " + tool.description + " " + tool.category).toLowerCase().includes(query.trim().toLowerCase())
    })
    function openTool(key: string): void {
        if (tools.some(function(tool) { return tool.key === key })) {
            activeTool = key
            Qt.callLater(function() { merge.focusBackButton() })
        }
    }
    function goBack(): void { activeTool = "" }
    Keys.onEscapePressed: function(event) { if (canGoBack) { goBack(); event.accepted = true } }
    Shortcut { sequences: [StandardKey.Back]; enabled: root.visible && root.canGoBack; onActivated: root.goBack() }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Controls.ScrollView {
                id: catalog
                objectName: "toolsCatalog"
                anchors.fill: parent
                visible: !root.canGoBack
                clip: true
                contentWidth: availableWidth
                Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
                ColumnLayout {
                    width: catalog.availableWidth
                    spacing: 0
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.margins: root.width < 600 ? 16 : 24
                        spacing: 18
                        QuickGenerate {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            onGenerateRequested: function(prompt, mediaType, aspectRatio, count) {
                                root.generateRequested(prompt, mediaType, aspectRatio, count)
                            }
                        }
                        LV.Label { text: qsTr("Tools"); style: title; objectName: "toolsTitle" }
                        LV.Label {
                            Layout.fillWidth: true
                            text: qsTr("A quick start for everyday creative work.")
                            color: LV.Theme.descriptionColor
                            wrapMode: Text.Wrap
                            sizeToContentHeight: true
                        }
                        LV.InputField {
                            objectName: "toolsSearch"
                            Layout.fillWidth: true
                            Layout.maximumWidth: 420
                            mode: searchMode
                            placeholderText: qsTr("Find a tool")
                            Accessible.name: qsTr("Find a tool")
                            text: root.query
                            onTextChanged: root.query = text
                        }
                        GridLayout {
                            id: cards
                            objectName: "toolCards"
                            Layout.fillWidth: true
                            Layout.maximumWidth: 420
                            Layout.alignment: Qt.AlignLeft
                            columns: 1
                            columnSpacing: 16
                            rowSpacing: 16
                            uniformCellWidths: true
                            Repeater {
                                model: root.filteredTools
                                delegate: ToolCard {
                                    required property var modelData
                                    tool: modelData
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 172
                                    onClicked: root.openTool(tool.key)
                                }
                            }
                        }
                        LV.Label {
                            Layout.fillWidth: true
                            visible: root.filteredTools.length === 0
                            text: qsTr("No tools found. Try another search.")
                            wrapMode: Text.Wrap
                            sizeToContentHeight: true
                        }
                    }
                }
            }
            ModelMergeTool {
                id: merge
                anchors.fill: parent
                visible: root.activeTool === "model-merge"
                modelsDirectory: root.modelsDirectory
                modelsBusy: root.modelsBusy
                touchNavigation: root.touchNavigation
                onBackRequested: root.goBack()
            }
        }
    }
}
