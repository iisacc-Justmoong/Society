pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society
import "../Dashboard"

Item {
    id: root
    objectName: "toolsView"
    property string modelsDirectory: ""
    property bool modelsBusy: false
    property bool preloadAllowed: true
    property bool preloadAdmitted: false
    readonly property bool preloadBusy: sharedModelCatalog.loading
    property bool touchNavigation: false
    property string activeTool: ""
    property string query: ""
    property bool mergeVisited: false
    property bool packagingVisited: false
    property bool mergeReady: false
    property bool packagingReady: false
    signal generateRequested(string prompt, string mediaType, string aspectRatio, int count)
    signal packageCreated(string path)
    readonly property bool canGoBack: activeTool.length > 0
    readonly property var tools: [
        { key: "model-merge", title: qsTr("Model merge"), category: qsTr("Models"), symbol: "M", tint: "#92B57B",
          description: qsTr("Combine checkpoints and LoRA into a new model.") },
        { key: "model-packaging", title: qsTr("Model Packaging"), category: qsTr("Models"), symbol: "P", icon: "localSwiftPackageDependency", tint: "#92B57B",
          description: qsTr("Package models from your Society library or a local folder into one safetensors file.") },
        { key: "model-calibrator", title: qsTr("Model calibrator"), category: qsTr("Models"), symbol: "C", icon: "toolWindowModelChecker", tint: LV.Theme.accentGreen,
          description: qsTr("Analyze compatibility and weights, and create a corrected model copy.") }
    ]
    readonly property var filteredTools: tools.filter(function(tool) {
        return (tool.title + " " + tool.description + " " + tool.category).toLowerCase().includes(query.trim().toLowerCase())
    })
    function openTool(key: string): void {
        if (tools.some(function(tool) { return tool.key === key })) {
            if (key === "model-calibrator") {
                calibratorLauncher.open(root.modelsDirectory)
                return
            }
            activeTool = key
            if (key === "model-merge") mergeVisited = true
            if (key === "model-packaging") packagingVisited = true
            Qt.callLater(root.focusActiveTool)
        }
    }
    function focusActiveTool(): void {
        const loader = activeTool === "model-merge" ? mergeLoader
            : activeTool === "model-packaging" ? packagingLoader : null
        if (loader && loader.item) loader.item.focusBackButton()
    }
    function goBack(): void { activeTool = "" }
    onPreloadAllowedChanged: if (preloadAllowed) preloadAdmitted = true
    Component.onCompleted: if (preloadAllowed) preloadAdmitted = true
    onModelsBusyChanged: if (!modelsBusy && sharedModelCatalog.directory.length > 0) sharedModelCatalog.refresh()
    // Share one model-library snapshot between the two editors.
    ModelCalibratorLauncher { id: calibratorLauncher; objectName: "modelCalibratorLauncher" }
    MergeModelCatalog {
        id: sharedModelCatalog
        objectName: "mergeModelCatalog"
        directory: root.preloadAdmitted || root.mergeVisited || root.packagingVisited ? root.modelsDirectory : ""
    }
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
                        LV.Label {
                            Layout.fillWidth: true
                            visible: calibratorLauncher.errorString.length > 0
                            text: calibratorLauncher.errorString
                            wrapMode: Text.Wrap
                            sizeToContentHeight: true
                        }
                        GridLayout {
                            id: cards
                            objectName: "toolCards"
                            Layout.fillWidth: true
                            Layout.maximumWidth: 856
                            Layout.alignment: Qt.AlignLeft
                            columns: root.width >= 900 ? 2 : 1
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
            Loader {
                id: mergeLoader
                objectName: "mergeToolLoader"
                anchors.fill: parent
                visible: root.activeTool === "model-merge"
                active: (root.visible && root.activeTool === "model-merge") || root.mergeReady
                asynchronous: true
                onLoaded: {
                    root.mergeReady = true
                    if (root.activeTool === "model-merge") Qt.callLater(root.focusActiveTool)
                }
                sourceComponent: ModelMergeTool {
                    modelsDirectory: root.modelsDirectory
                    modelsBusy: root.modelsBusy
                    touchNavigation: root.touchNavigation
                    preloadedCatalog: sharedModelCatalog
                    onBackRequested: root.goBack()
                }
            }
            Loader {
                id: packagingLoader
                objectName: "packagingToolLoader"
                anchors.fill: parent
                visible: root.activeTool === "model-packaging"
                active: (root.visible && root.activeTool === "model-packaging") || root.packagingReady
                asynchronous: true
                onLoaded: {
                    root.packagingReady = true
                    if (root.activeTool === "model-packaging") Qt.callLater(root.focusActiveTool)
                }
                sourceComponent: ModelPackagingTool {
                    modelsDirectory: root.modelsDirectory
                    modelsBusy: root.modelsBusy
                    touchNavigation: root.touchNavigation
                    preloadedCatalog: sharedModelCatalog
                    onBackRequested: root.goBack()
                    onPackageCreated: function(path) {
                        sharedModelCatalog.refresh()
                        root.packageCreated(path)
                    }
                }
            }
            LV.Label {
                anchors.centerIn: parent
                visible: (root.activeTool === "model-merge" && mergeLoader.status === Loader.Loading)
                    || (root.activeTool === "model-packaging" && packagingLoader.status === Loader.Loading)
                text: qsTr("Loading…")
                style: description
            }
        }
    }
}
