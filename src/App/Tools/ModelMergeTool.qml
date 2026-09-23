pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import LVRS 1.0 as LV
import Society

Item {
    id: root
    objectName: "modelMergeTool"
    property string modelsDirectory: ""
    property bool modelsBusy: false
    property bool touchNavigation: false
    property string baseModel: ""
    property string mode: "unified"
    property string compatibilityModel: ""
    property string weightMode: "automatic"
    property string sharedWeight: "0.5"
    property string outputName: ""
    property string outputDirectory: ""
    property string cacheDirectory: ""
    property string mergeExecutable: ""
    property string pythonExecutable: mergeController.defaultPython
    property bool detailsVisible: false
    property bool advancedVisible: false
    property bool hasSubmitted: false
    property string inspectedRequest: ""
    readonly property bool wideLayout: width >= 1000
    readonly property real pageMargin: touchNavigation && width < 760 ? 16 : wideLayout ? 32 : 24
    readonly property bool editingEnabled: mergeController.supported && !mergeController.busy && !modelsBusy
    readonly property string effectiveOutputDirectory: outputDirectory.trim() || modelOutputDirectory
    readonly property bool requestCurrent: inspectedRequest === JSON.stringify(requestOptions())
    signal backRequested()
    readonly property int materialCount: materials.count
    readonly property string modelOutputDirectory: modelCatalog.models.length > 0 ? modelCatalog.outputDirectory(baseModel) : modelsDirectory
    readonly property string outputPath: mergeController.outputPathForName(outputName, outputDirectory.trim() || modelOutputDirectory, mode)
    readonly property bool inputModelsReady: {
        if (modelsBusy || modelCatalog.loading || modelCatalog.models.length === 0 || !modelCatalog.contains(baseModel, true))
            return false;
        for (let i = 0; i < materials.count; ++i) {
            if (!modelCatalog.contains(materials.get(i).modelPath))
                return false;
        }
        return materials.count > 0;
    }

    onModelsDirectoryChanged: {
        baseModel = "";
        compatibilityModel = "";
        outputName = "";
        outputDirectory = "";
        if (materials) {
            for (let i = 0; i < materials.count; ++i)
                materials.setProperty(i, "modelPath", "");
        }
    }
    onVisibleChanged: {
        if (visible && modelCatalog)
            modelCatalog.refresh();
    }
    onModelsBusyChanged: {
        if (!modelsBusy && modelCatalog)
            modelCatalog.refresh();
    }

    function reconcileModels() {
        if (modelCatalog.loading)
            return;
        if (baseModel && !modelCatalog.contains(baseModel, true))
            baseModel = "";
        for (let i = 0; i < materials.count; ++i) {
            const path = materials.get(i).modelPath;
            if (path && !modelCatalog.contains(path))
                materials.setProperty(i, "modelPath", "");
        }
    }

    function addMaterial(path) {
        materials.append({
            modelPath: path || "",
            weight: "0.5"
        });
    }
    function setMaterial(index, path, weight) {
        materials.setProperty(index, "modelPath", path);
        materials.setProperty(index, "weight", weight);
    }
    function requestOptions() {
        const values = [];
        for (let i = 0; i < materials.count; ++i) {
            const material = materials.get(i);
            values.push({
                path: material.modelPath,
                weight: material.weight
            });
        }
        return {
            baseModel: baseModel,
            mode: mode,
            compatibilityModels: compatibilityModel ? [compatibilityModel] : [],
            materials: values,
            weightMode: weightMode,
            sharedWeight: sharedWeight,
            output: outputPath,
            cacheDirectory: cacheDirectory,
            executable: mergeExecutable || mergeController.defaultExecutable,
            pythonExecutable: pythonExecutable
        };
    }

    function focusBackButton() {
        scroll.contentItem.contentY = 0;
        backButton.forceActiveFocus();
    }

    function revealFocusedControl(item) {
        if (!visible || !item)
            return;
        let ancestor = item;
        while (ancestor && ancestor !== root)
            ancestor = ancestor.parent;
        if (!ancestor)
            return;
        const flick = scroll.contentItem;
        const top = item.mapToItem(flick.contentItem, 0, 0).y;
        let offset = flick.contentY;
        if (top < offset)
            offset = top - LV.Theme.gap12;
        else if (top + item.height > offset + flick.height)
            offset = top + item.height - flick.height + LV.Theme.gap12;
        flick.contentY = Math.max(0, Math.min(offset, flick.contentHeight - flick.height));
    }

    function revealAfterLayout() {
        Qt.callLater(function() {
            if (root.Window.window)
                root.revealFocusedControl(root.Window.window.activeFocusItem);
        });
    }

    function submit(validationOnly) {
        const options = requestOptions();
        inspectedRequest = JSON.stringify(options);
        hasSubmitted = true;
        mergeController.run(options, validationOnly);
    }

    function displayOutputFolder() {
        const directory = effectiveOutputDirectory;
        if (modelsDirectory && (directory === modelsDirectory || directory.startsWith(modelsDirectory + "/"))) {
            const relative = directory.slice(modelsDirectory.length).replace(/^\//, "");
            return relative ? "Models / " + relative.replace(/\//g, " / ") : "Models";
        }
        return directory || qsTr("Choose a folder…");
    }

    ModelMergeController {
        id: mergeController
        objectName: "modelMergeController"
        onFinished: function(success, validationOnly) {
            if (success && !validationOnly)
                modelCatalog.refresh();
        }
    }
    MergeModelCatalog {
        id: modelCatalog
        objectName: "mergeModelCatalog"
        directory: root.modelsDirectory
        onLoadingChanged: root.reconcileModels()
    }
    ListModel {
        id: materials
        objectName: "mergeMaterials"
        ListElement {
            modelPath: ""
            weight: "0.5"
        }
    }

    Rectangle {
        anchors.fill: parent
        color: LV.Theme.panelBackground03
    }
    Connections {
        id: focusObserver
        target: root.Window.window
        function onActiveFocusItemChanged() {
            if (root.Window.window)
                root.revealFocusedControl(root.Window.window.activeFocusItem);
        }
    }
    Connections {
        target: scroll.contentItem
        function onContentHeightChanged() { root.revealAfterLayout(); }
        function onHeightChanged() { root.revealAfterLayout(); }
    }

    // Figma Society 121:752. Keep the existing controller and catalog as the owners of work.
    component MergePanel: LV.Card {
        id: panel
        default property alias panelData: panelLayout.data
        type: LV.Card.Model
        displayState: LV.Card.DefaultState
        selectable: false
        showMenu: false
        showAction: false
        focusPolicy: Qt.NoFocus
        activeFocusOnTab: false
        horizontalPadding: LV.Theme.gap18 + borderWidth
        verticalPadding: LV.Theme.gap18 + borderWidth
        implicitHeight: panelLayout.implicitHeight + topPadding + bottomPadding
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        background: Rectangle {
            color: panel.surfaceColor
            radius: panel.resolvedCornerRadius
            border.width: panel.borderWidth
            border.color: panel.borderColor
        }
        contentItem: ColumnLayout {
            id: panelLayout
            spacing: LV.Theme.gap12
        }
    }

    component MergeCopy: LV.Label {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        style: body
        color: LV.Theme.descriptionColor
        lineHeight: LV.Theme.gap20
        wrapMode: Text.Wrap
        sizeToContentHeight: true
        textFormat: Text.PlainText
    }

    Controls.ScrollView {
        id: scroll
        objectName: "mergeScroll"
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        contentHeight: page.implicitHeight + root.pageMargin + LV.Theme.gap24
        Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff

        ColumnLayout {
            id: page
            x: root.pageMargin
            y: root.pageMargin
            width: Math.max(0, scroll.availableWidth - 2 * root.pageMargin)
            spacing: LV.Theme.gap24

            ColumnLayout {
                Layout.fillWidth: true
                spacing: LV.Theme.gap10
                LV.LabelButton {
                    id: backButton
                    objectName: "toolsBack"
                    Layout.preferredWidth: LV.Theme.scaleMetric(82)
                    Layout.preferredHeight: root.touchNavigation ? 44 : 24
                    text: qsTr("‹ All tools")
                    Accessible.name: qsTr("All tools")
                    tone: LV.AbstractButton.Borderless
                    onClicked: root.backRequested()
                }
                LV.Label {
                    objectName: "mergeTitle"
                    text: qsTr("Model merge")
                    style: title
                }
                MergeCopy {
                    text: qsTr("Create a new model from compatible checkpoints and LoRAs.")
                    lineHeight: LV.Theme.textBodyLineHeight
                }
            }

            GridLayout {
                objectName: "mergeColumns"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                columns: root.wideLayout ? 2 : 1
                columnSpacing: LV.Theme.gap24
                rowSpacing: LV.Theme.gap20

                ColumnLayout {
                    objectName: "mergeConfiguration"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.alignment: Qt.AlignTop
                    spacing: LV.Theme.gap20

                    MergePanel {
                        objectName: "mergeInputPanel"
                        title: qsTr("Input models")
                        enabled: root.editingEnabled
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: LV.Theme.gap12
                            LV.Label {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: qsTr("Input models")
                                style: header2
                            }
                            LV.LabelButton {
                                objectName: "mergeRefreshModels"
                                Layout.minimumHeight: root.touchNavigation ? 44 : 0
                                text: qsTr("Refresh models")
                                tone: LV.AbstractButton.Borderless
                                enabled: root.modelsDirectory.length > 0 && !modelCatalog.loading
                                onClicked: modelCatalog.refresh()
                            }
                        }
                        MergeCopy {
                            objectName: "mergeModelsMessage"
                            visible: root.modelsBusy || modelCatalog.loading || root.modelsDirectory.length === 0
                                || modelCatalog.errorString.length > 0 || modelCatalog.models.length === 0
                            text: root.modelsBusy ? qsTr("Organizing or importing models…")
                                : modelCatalog.loading ? qsTr("Loading models…")
                                : root.modelsDirectory.length === 0 ? qsTr("Open a Society container to choose models.")
                                : modelCatalog.errorString || qsTr("No models found in Models. Import models from Storage, then refresh this list.")
                        }
                        MergeModelPicker {
                            objectName: "mergeBaseField"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            touchNavigation: root.touchNavigation
                            catalog: modelCatalog
                            label: qsTr("Base model")
                            path: root.baseModel
                            baseOnly: true
                            onEdited: function(value) { root.baseModel = value; }
                        }
                        Repeater {
                            model: materials
                            delegate: ColumnLayout {
                                id: material
                                required property int index
                                required property string modelPath
                                required property string weight
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: LV.Theme.gap8
                                MergeModelPicker {
                                    objectName: "mergeMaterial" + material.index
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    touchNavigation: root.touchNavigation
                                    catalog: modelCatalog
                                    label: qsTr("Material %1").arg(material.index + 1)
                                    path: material.modelPath
                                    onEdited: function(value) { materials.setProperty(material.index, "modelPath", value); }
                                }
                                RowLayout {
                                    visible: root.weightMode === "per-model" || materials.count > 1
                                    Layout.fillWidth: true
                                    spacing: LV.Theme.gap8
                                    LV.InputField {
                                        objectName: "mergeMaterialWeight" + material.index
                                        visible: root.weightMode === "per-model"
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        Layout.preferredHeight: root.touchNavigation ? 44 : 30
                                        text: material.weight
                                        placeholderText: qsTr("Weight ≥ 0")
                                        Accessible.name: qsTr("Material %1 weight").arg(material.index + 1)
                                        onTextEdited: function(value) { materials.setProperty(material.index, "weight", value); }
                                    }
                                    Item { visible: root.weightMode !== "per-model"; Layout.fillWidth: true }
                                    LV.LabelButton {
                                        objectName: "mergeRemoveMaterial" + material.index
                                        Layout.minimumHeight: root.touchNavigation ? 44 : 0
                                        text: qsTr("Remove")
                                        Accessible.name: qsTr("Remove material %1").arg(material.index + 1)
                                        tone: LV.AbstractButton.Borderless
                                        enabled: materials.count > 1
                                        onClicked: materials.remove(material.index)
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: LV.Theme.gap12
                            LV.LabelButton {
                                objectName: "mergeAddMaterial"
                                Layout.minimumHeight: root.touchNavigation ? 44 : 0
                                text: qsTr("+ Add material")
                                tone: LV.AbstractButton.Default
                                onClicked: root.addMaterial("")
                            }
                            Item { Layout.fillWidth: true }
                            LV.LabelButton {
                                visible: materials.count === 1
                                Layout.minimumHeight: root.touchNavigation ? 44 : 0
                                text: qsTr("Remove material")
                                Accessible.name: qsTr("Clear material selection")
                                tone: LV.AbstractButton.Borderless
                                enabled: materials.get(0).modelPath.length > 0
                                onClicked: materials.setProperty(0, "modelPath", "")
                            }
                        }
                    }

                    MergePanel {
                        objectName: "mergeSettingsPanel"
                        title: qsTr("Merge settings")
                        enabled: root.editingEnabled
                        LV.Label { text: qsTr("Merge settings"); style: header2 }
                        LV.Label { text: qsTr("Method"); style: caption; color: LV.Theme.descriptionColor }
                        LV.LabelSegmentedControl {
                            id: methodControl
                            Layout.preferredWidth: LV.Theme.scaleMetric(480)
                            Layout.maximumWidth: parent.width
                            Layout.minimumWidth: 0
                            Layout.preferredHeight: root.touchNavigation ? 51 : 30
                            forceBorderlessTone: false
                            LV.LabelButton {
                                objectName: "mergeUnifiedMode"
                                width: (methodControl.width - methodControl.horizontalPadding * 2 - methodControl.spacing * 2) / 3
                                height: methodControl.height - methodControl.verticalPadding * 2
                                text: qsTr("Unified")
                                Accessible.name: text
                                Accessible.selected: root.mode === "unified"
                                tone: root.mode === "unified" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                                onClicked: root.mode = "unified"
                            }
                            LV.LabelButton {
                                objectName: "mergeSumMode"
                                width: (methodControl.width - methodControl.horizontalPadding * 2 - methodControl.spacing * 2) / 3
                                height: methodControl.height - methodControl.verticalPadding * 2
                                text: qsTr("Weighted sum")
                                Accessible.name: text
                                Accessible.selected: root.mode === "weighted-sum"
                                tone: root.mode === "weighted-sum" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                                onClicked: root.mode = "weighted-sum"
                            }
                            LV.LabelButton {
                                objectName: "mergeDifferenceMode"
                                width: (methodControl.width - methodControl.horizontalPadding * 2 - methodControl.spacing * 2) / 3
                                height: methodControl.height - methodControl.verticalPadding * 2
                                text: qsTr("Weighted difference")
                                Accessible.name: text
                                Accessible.selected: root.mode === "weighted-difference"
                                tone: root.mode === "weighted-difference" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                                onClicked: root.mode = "weighted-difference"
                            }
                        }
                        LV.Label {
                            Layout.fillWidth: true
                            text: root.mode === "unified"
                                ? qsTr("Combine architectures in one package. Each LoRA uses a compatible checkpoint; independent models refine the image in order.")
                                : qsTr("Weighted arithmetic requires matching network structures. Use Unified for different architectures.")
                            wrapMode: Text.WordWrap
                            sizeToContentHeight: true
                            style: caption
                            color: LV.Theme.descriptionColor
                        }
                        LV.Label { text: qsTr("Weights"); style: caption; color: LV.Theme.descriptionColor }
                        LV.LabelSegmentedControl {
                            id: weightsControl
                            objectName: "mergeWeightMode"
                            Layout.preferredWidth: LV.Theme.scaleMetric(432)
                            Layout.maximumWidth: parent.width
                            Layout.minimumWidth: 0
                            Layout.preferredHeight: root.touchNavigation ? 51 : 30
                            forceBorderlessTone: false
                            LV.LabelButton {
                                objectName: "mergeAutomaticMode"
                                width: (weightsControl.width - weightsControl.horizontalPadding * 2 - weightsControl.spacing * 2) / 3
                                height: weightsControl.height - weightsControl.verticalPadding * 2
                                text: qsTr("Automatic")
                                Accessible.name: qsTr("Automatic weights")
                                Accessible.selected: root.weightMode === "automatic"
                                tone: root.weightMode === "automatic" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                                onClicked: root.weightMode = "automatic"
                            }
                            LV.LabelButton {
                                objectName: "mergeSharedMode"
                                width: (weightsControl.width - weightsControl.horizontalPadding * 2 - weightsControl.spacing * 2) / 3
                                height: weightsControl.height - weightsControl.verticalPadding * 2
                                text: qsTr("Shared weight")
                                Accessible.name: text
                                Accessible.selected: root.weightMode === "shared"
                                tone: root.weightMode === "shared" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                                onClicked: root.weightMode = "shared"
                            }
                            LV.LabelButton {
                                objectName: "mergePerModelMode"
                                width: (weightsControl.width - weightsControl.horizontalPadding * 2 - weightsControl.spacing * 2) / 3
                                height: weightsControl.height - weightsControl.verticalPadding * 2
                                text: qsTr("Per-model")
                                Accessible.name: qsTr("Per-model weights")
                                Accessible.selected: root.weightMode === "per-model"
                                tone: root.weightMode === "per-model" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                                onClicked: root.weightMode = "per-model"
                            }
                        }
                        LV.InputField {
                            objectName: "mergeSharedWeight"
                            visible: root.weightMode === "shared"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredHeight: root.touchNavigation ? 44 : 30
                            text: root.sharedWeight
                            placeholderText: qsTr("Weight ≥ 0")
                            Accessible.name: qsTr("Shared weight for every additional model")
                            onTextEdited: function(value) { root.sharedWeight = value; }
                        }
                        MergeCopy {
                            text: root.weightMode === "automatic"
                                ? root.mode === "weighted-sum"
                                    ? qsTr("Automatic balances checkpoints and applies the default LoRA strength.")
                                    : qsTr("Automatic subtracts 0.5 per checkpoint and applies LoRAs at strength 1.")
                                : qsTr("Use nonnegative weights. Sum checkpoint weights must total ≤ 1. LoRA strengths and difference weights may exceed 1.")
                        }
                    }
                    LV.LabelButton {
                        objectName: "mergeAdvancedToggle"
                        Layout.minimumHeight: root.touchNavigation ? 44 : 0
                        text: root.advancedVisible ? qsTr("Advanced settings  ⌄") : qsTr("Advanced settings  ›")
                        Accessible.name: qsTr("Advanced settings")
                        Accessible.description: root.advancedVisible ? qsTr("Expanded") : qsTr("Collapsed")
                        tone: LV.AbstractButton.Borderless
                        onClicked: root.advancedVisible = !root.advancedVisible
                    }
                    MergePanel {
                        objectName: "mergeAdvancedPanel"
                        title: qsTr("Advanced settings")
                        visible: root.advancedVisible
                        enabled: root.editingEnabled
                        LV.Label { text: qsTr("Advanced settings"); style: header2 }
                        MergePathField {
                            objectName: "mergeCompatibilityField"
                            Layout.fillWidth: true
                            visible: root.mode === "unified"
                            touchNavigation: root.touchNavigation
                            merger: mergeController
                            label: qsTr("LoRA compatibility checkpoint (optional)")
                            placeholder: qsTr("Automatic · local compatible checkpoint")
                            path: root.compatibilityModel
                            onEdited: function(value) { root.compatibilityModel = value; }
                        }
                        MergePathField {
                            objectName: "mergeCacheField"
                            Layout.fillWidth: true
                            touchNavigation: root.touchNavigation
                            merger: mergeController
                            label: qsTr("Conversion cache (optional)")
                            path: root.cacheDirectory
                            placeholder: qsTr("Automatic · Society application cache")
                            allowFile: false
                            onEdited: function(value) { root.cacheDirectory = value; }
                        }
                        MergePathField {
                            objectName: "mergeExecutableField"
                            Layout.fillWidth: true
                            touchNavigation: root.touchNavigation
                            merger: mergeController
                            label: qsTr("iiLocalDiffusion executable")
                            path: root.mergeExecutable
                            placeholder: mergeController.defaultExecutable || qsTr("Path to iild-merge")
                            allowFolder: false
                            nameFilters: [qsTr("All files (*)")]
                            onEdited: function(value) { root.mergeExecutable = value; }
                        }
                        MergePathField {
                            objectName: "mergePythonField"
                            Layout.fillWidth: true
                            touchNavigation: root.touchNavigation
                            merger: mergeController
                            label: qsTr("Python executable (optional)")
                            path: root.pythonExecutable
                            placeholder: qsTr("Automatic · SDK Python environment")
                            allowFolder: false
                            nameFilters: [qsTr("All files (*)")]
                            onEdited: function(value) { root.pythonExecutable = value; }
                        }
                    }
                }

                ColumnLayout {
                    objectName: "mergeSummary"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: root.wideLayout ? LV.Theme.scaleMetric(348) : -1
                    Layout.maximumWidth: root.wideLayout ? LV.Theme.scaleMetric(348) : Infinity
                    Layout.alignment: Qt.AlignTop
                    spacing: LV.Theme.gap20
                    MergePanel {
                        objectName: "mergeOutputPanel"
                        title: qsTr("Output")
                        enabled: root.editingEnabled
                        LV.Label { text: qsTr("Output"); style: header2 }
                        LV.Label { text: qsTr("Model name · Required"); style: body }
                        LV.InputField {
                            objectName: "mergeOutputName"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredHeight: root.touchNavigation ? 44 : 30
                            text: root.outputName
                            placeholderText: qsTr("Enter a model name")
                            Accessible.name: qsTr("Output model name (required)")
                            onTextEdited: function(value) { root.outputName = value; }
                        }
                        LV.Label { text: root.mode === "unified" ? qsTr("Saved as .iildmodel") : qsTr("Saved as .safetensors"); style: caption; color: LV.Theme.descriptionColor }
                        LV.Label { text: qsTr("Save to"); style: body }
                        LV.ListItem {
                            objectName: "mergeOutputDirectoryField"
                            type: LV.ListItem.Navigation
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            standardItemHeight: root.touchNavigation ? 44 : 36
                            implicitHeight: standardItemHeight
                            label: root.displayOutputFolder()
                            iconName: "nodesfolder"
                            showDescription: false
                            showValue: false
                            showTrailingIcon: true
                            cornerRadius: LV.Theme.radiusMd
                            backgroundColor: LV.Theme.panelBackground04
                            backgroundColorHover: LV.Theme.panelBackground07
                            Accessible.name: qsTr("Choose output folder")
                            Accessible.description: root.effectiveOutputDirectory
                            onClicked: outputFolderDialog.open()
                        }
                        LV.Label {
                            id: outputPreview
                            objectName: "mergeOutputPreview"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: root.outputPath ? root.outputPath.replace(/\\/g, "/").split("/").pop()
                                : qsTr("Enter a model name and choose a valid output folder.")
                            Accessible.description: root.outputPath
                            style: caption
                            color: LV.Theme.descriptionColor
                            textFormat: Text.PlainText
                            wrapMode: Text.WrapAnywhere
                            sizeToContentHeight: true
                        }
                        MergeCopy { text: qsTr("A new file is created. Your source models stay in place.") }
                    }
                    MergePanel {
                        objectName: "mergeReviewPanel"
                        title: qsTr("Input check")
                        LV.Label { text: qsTr("Input check"); style: header2 }
                        MergeCopy {
                            objectName: "mergeStatus"
                            text: !mergeController.supported ? qsTr("Model merging is available on desktop.")
                                : mergeController.busy || (root.hasSubmitted && root.requestCurrent) ? mergeController.status
                                : qsTr("Not checked yet. Inspect model compatibility before merging.")
                        }
                        LV.LabelButton {
                            objectName: "mergeValidate"
                            Layout.fillWidth: true
                            Layout.preferredHeight: root.touchNavigation ? 44 : 30
                            text: qsTr("Check inputs")
                            tone: LV.AbstractButton.Default
                            enabled: root.editingEnabled && root.inputModelsReady && root.outputPath.length > 0
                            onClicked: root.submit(true)
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: LV.Theme.scaleMetric(1)
                            color: LV.Theme.panelBackground12
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            LV.Label {
                                objectName: "mergeInputCount"
                                text: qsTr("%1 input models").arg((root.baseModel ? 1 : 0) + root.requestOptions().materials.filter(item => item.path.length > 0).length)
                                style: caption
                                color: LV.Theme.descriptionColor
                            }
                            Item { Layout.fillWidth: true }
                            LV.Label { text: qsTr("1 output file"); style: caption; color: LV.Theme.descriptionColor }
                        }
                        LV.LabelButton {
                            objectName: "mergeRun"
                            Layout.fillWidth: true
                            Layout.preferredHeight: root.touchNavigation ? 44 : 34
                            text: qsTr("Merge models")
                            textColor: LV.Theme.panelBackground03
                            tone: LV.AbstractButton.Primary
                            enabled: root.editingEnabled && root.inputModelsReady && root.outputPath.length > 0
                            onClicked: root.submit(false)
                        }
                        MergeCopy {
                            objectName: "mergeError"
                            visible: root.requestCurrent && mergeController.errorString.length > 0
                            text: mergeController.errorString.slice(0, 4000)
                            wrapMode: Text.WrapAnywhere
                        }
                        LV.Label {
                            objectName: "mergeElapsed"
                            visible: mergeController.busy || mergeController.elapsedSeconds > 0
                            text: qsTr("Elapsed: %1 s").arg(mergeController.elapsedSeconds)
                            style: caption
                        }
                        LV.LabelButton {
                            objectName: "mergeCancel"
                            Layout.fillWidth: true
                            Layout.minimumHeight: root.touchNavigation ? 44 : 0
                            visible: mergeController.busy
                            text: qsTr("Cancel")
                            tone: LV.AbstractButton.Borderless
                            enabled: !mergeController.cancelling
                            onClicked: mergeController.cancel()
                        }
                        LV.LabelButton {
                            objectName: "mergeOpenOutput"
                            Layout.fillWidth: true
                            Layout.minimumHeight: root.touchNavigation ? 44 : 0
                            visible: mergeController.completedOutput.length > 0
                            text: qsTr("Open output folder")
                            tone: LV.AbstractButton.Default
                            onClicked: mergeController.openOutputFolder()
                        }
                        Flow {
                            visible: mergeController.details.length > 0
                            Layout.fillWidth: true
                            spacing: LV.Theme.gap8
                            LV.LabelButton {
                                objectName: "mergeShowReport"
                                height: root.touchNavigation ? 44 : implicitHeight
                                text: root.detailsVisible ? qsTr("Hide report") : qsTr("Show report")
                                tone: LV.AbstractButton.Borderless
                                onClicked: root.detailsVisible = !root.detailsVisible
                            }
                            LV.LabelButton {
                                objectName: "mergeCopyReport"
                                height: root.touchNavigation ? 44 : implicitHeight
                                text: qsTr("Copy full report")
                                tone: LV.AbstractButton.Default
                                onClicked: mergeController.copyDetails()
                            }
                        }
                    }
                }
            }
            MergeCopy {
                objectName: "mergeReport"
                visible: root.detailsVisible && mergeController.details.length > 0
                text: mergeController.details.slice(0, 16000)
                wrapMode: Text.WrapAnywhere
                style: caption
            }
        }
    }
    FolderDialog {
        id: outputFolderDialog
        title: qsTr("Output folder")
        onAccepted: root.outputDirectory = mergeController.localPath(selectedFolder)
    }
    LV.Tooltip {
        objectName: "mergeOutputPathTooltip"
        target: outputPreview
        automatic: root.outputPath.length > 0
        text: root.outputPath
    }
}
