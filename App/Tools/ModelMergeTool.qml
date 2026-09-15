pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society

Item {
    id: root
    objectName: "toolsView"
    property string modelsDirectory: ""
    property bool modelsBusy: false
    property bool touchNavigation: false
    property string baseModel: ""
    property string mode: "weighted-sum"
    property string weightMode: "automatic"
    property string sharedWeight: "0.5"
    property string outputPath: ""
    property string cacheDirectory: ""
    property string mergeExecutable: ""
    property string pythonExecutable: mergeController.defaultPython
    property bool detailsVisible: false
    readonly property int materialCount: materials.count
    readonly property string modelOutputDirectory: modelCatalog.models.length > 0 ? modelCatalog.outputDirectory(baseModel) : modelsDirectory
    readonly property string suggestedPath: mergeController.suggestedOutput(baseModel, modelOutputDirectory, mode)
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
        outputPath = "";
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
            materials: values,
            weightMode: weightMode,
            sharedWeight: sharedWeight,
            output: outputPath,
            outputDirectory: modelOutputDirectory,
            cacheDirectory: cacheDirectory,
            executable: mergeExecutable || mergeController.defaultExecutable,
            pythonExecutable: pythonExecutable
        };
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

    LV.VStack {

        alignment: Qt.AlignLeft
        anchors.fill: parent
        anchors.margins: root.touchNavigation && root.width < 760 ? 16 : 24
        spacing: 16

        LV.VStack {

            alignment: Qt.AlignLeft
            Layout.fillWidth: true
            spacing: 6
            LV.Label {
                text: qsTr("Tools")
                style: description
            }
            LV.Label {
                text: qsTr("Model merge")
                style: title
            }
            LV.Label {
                Layout.fillWidth: true
                visible: root.height >= 500
                text: qsTr("Combine local checkpoints and LoRA adapters into a new model with iiLocalDiffusion.")
                wrapMode: Text.Wrap
                sizeToContentHeight: true
            }
        }

        Controls.ScrollView {
            id: scroll
            objectName: "mergeScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
            LV.VStack {
                alignment: Qt.AlignLeft
                width: scroll.availableWidth
                spacing: 24
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width >= 1000 ? 2 : 1
                    columnSpacing: 32
                    rowSpacing: 24
                    uniformCellWidths: true
                    enabled: mergeController.supported && !mergeController.busy && !root.modelsBusy

                    LV.VStack {

                        alignment: Qt.AlignLeft
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: 16
                        LV.Label {
                            text: qsTr("Models & weights")
                            style: header
                        }
                        LV.LabelSegmentedControl {
                            forceBorderlessTone: false
                            LV.LabelButton {
                                Layout.minimumHeight: root.touchNavigation ? 44 : 0
                                objectName: "mergeSumMode"
                                height: root.touchNavigation ? 44 : implicitHeight
                                text: qsTr("Weighted sum")
                                Accessible.name: text
                                Accessible.selected: root.mode === "weighted-sum"
                                tone: root.mode === "weighted-sum" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                                onClicked: root.mode = "weighted-sum"
                            }
                            LV.LabelButton {
                                Layout.minimumHeight: root.touchNavigation ? 44 : 0
                                objectName: "mergeDifferenceMode"
                                height: root.touchNavigation ? 44 : implicitHeight
                                text: qsTr("Weighted difference")
                                Accessible.name: text
                                Accessible.selected: root.mode === "weighted-difference"
                                tone: root.mode === "weighted-difference" ? LV.AbstractButton.Default : LV.AbstractButton.Borderless
                                onClicked: root.mode = "weighted-difference"
                            }
                        }
                        LV.Label {
                            Layout.fillWidth: true
                            text: root.mode === "weighted-sum" ? qsTr("(1 − Σ checkpoint weights) × A + Σ weighted checkpoints + Σ weighted LoRA deltas") : qsTr("A − Σ weighted checkpoints − Σ weighted LoRA deltas")
                            wrapMode: Text.Wrap
                            sizeToContentHeight: true
                            textFormat: Text.PlainText
                        }
                        LV.HStack {
                            Layout.fillWidth: root.weightMode === "shared"
                            spacing: 8
                            LV.LabelMenuButton {
                                Layout.minimumHeight: root.touchNavigation ? 44 : 0
                                id: weightButton
                                objectName: "mergeWeightMode"
                                text: root.weightMode === "automatic" ? qsTr("Automatic weights") : root.weightMode === "shared" ? qsTr("Shared weight") : qsTr("Per-model weights")
                                Accessible.name: qsTr("Weight mode")
                                tone: LV.AbstractButton.Default
                                onClicked: weightMenu.openFor(weightButton, 0, weightButton.height + 2)
                            }
                            LV.InputField {
                                Layout.minimumHeight: root.touchNavigation ? 44 : 0
                                objectName: "mergeSharedWeight"
                                visible: root.weightMode === "shared"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: root.sharedWeight
                                placeholderText: qsTr("Weight ≥ 0")
                                Accessible.name: qsTr("Shared weight for every additional model")
                                onTextEdited: function (value) {
                                    root.sharedWeight = value;
                                }
                            }
                        }
                        LV.Label {
                            Layout.fillWidth: true
                            text: root.weightMode === "automatic" ? qsTr("Automatic: equal checkpoint shares for sum, 0.5 per checkpoint for difference. LoRA strength is 1.") : qsTr("Use nonnegative numbers, including scientific notation. Sum checkpoint weights must total ≤ 1. LoRA strengths and difference weights may exceed 1.")
                            wrapMode: Text.Wrap
                            sizeToContentHeight: true
                            style: caption
                        }
                        LV.HStack {
                            Layout.fillWidth: true
                            spacing: 8
                            LV.Label {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: root.modelsBusy ? qsTr("Organizing or importing models…")
                                    : modelCatalog.loading ? qsTr("Loading models…")
                                    : qsTr("Models in this container · %1").arg(modelCatalog.models.length)
                                style: caption
                                wrapMode: Text.Wrap
                                sizeToContentHeight: true
                            }
                            LV.LabelButton {
                                Layout.minimumHeight: root.touchNavigation ? 44 : 0
                                objectName: "mergeRefreshModels"
                                text: qsTr("Refresh models")
                                tone: LV.AbstractButton.Default
                                enabled: root.modelsDirectory.length > 0 && !modelCatalog.loading
                                onClicked: modelCatalog.refresh()
                            }
                        }
                        LV.Label {
                            objectName: "mergeModelsMessage"
                            Layout.fillWidth: true
                            visible: root.modelsDirectory.length === 0 || modelCatalog.errorString.length > 0
                                || (!root.modelsBusy && !modelCatalog.loading && modelCatalog.models.length === 0)
                            text: root.modelsDirectory.length === 0 ? qsTr("Open a Society container to choose models.")
                                : modelCatalog.errorString || qsTr("No models found in Models. Import models from Storage, then refresh this list.")
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            sizeToContentHeight: true
                            style: caption
                        }
                        MergeModelPicker {
                            touchNavigation: root.touchNavigation
                            objectName: "mergeBaseField"
                            Layout.fillWidth: true
                            catalog: modelCatalog
                            label: qsTr("Base model (A)")
                            path: root.baseModel
                            baseOnly: true
                            onEdited: function (value) {
                                root.baseModel = value;
                            }
                        }
                        Repeater {
                            model: materials
                            delegate: LV.VStack {
                                id: material
                                alignment: Qt.AlignLeft
                                required property int index
                                required property string modelPath
                                required property string weight
                                Layout.fillWidth: true
                                spacing: 8
                                MergeModelPicker {
                                    touchNavigation: root.touchNavigation
                                    objectName: "mergeMaterial" + material.index
                                    Layout.fillWidth: true
                                    catalog: modelCatalog
                                    label: qsTr("Material %1 · checkpoint or LoRA").arg(material.index + 1)
                                    path: material.modelPath
                                    onEdited: function (value) {
                                        materials.setProperty(material.index, "modelPath", value);
                                    }
                                }
                                LV.HStack {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    LV.InputField {
                                        Layout.minimumHeight: root.touchNavigation ? 44 : 0
                                        objectName: "mergeMaterialWeight" + material.index
                                        visible: root.weightMode === "per-model"
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        text: material.weight
                                        placeholderText: qsTr("Weight ≥ 0")
                                        Accessible.name: qsTr("Material %1 weight").arg(material.index + 1)
                                        onTextEdited: function (value) {
                                            materials.setProperty(material.index, "weight", value);
                                        }
                                    }
                                    LV.LabelButton {
                                        Layout.minimumHeight: root.touchNavigation ? 44 : 0
                                        objectName: "mergeRemoveMaterial" + material.index
                                        text: qsTr("Remove")
                                        Accessible.name: qsTr("Remove material %1").arg(material.index + 1)
                                        tone: LV.AbstractButton.Borderless
                                        enabled: materials.count > 1
                                        onClicked: materials.remove(material.index)
                                    }
                                }
                            }
                        }
                        LV.LabelButton {
                            Layout.minimumHeight: root.touchNavigation ? 44 : 0
                            objectName: "mergeAddMaterial"
                            text: qsTr("Add material")
                            tone: LV.AbstractButton.Default
                            onClicked: root.addMaterial("")
                        }
                    }

                    LV.VStack {

                        alignment: Qt.AlignLeft
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: 16
                        LV.Label {
                            text: qsTr("Output")
                            style: header
                        }
                        MergePathField {
                            touchNavigation: root.touchNavigation
                            objectName: "mergeOutputField"
                            Layout.fillWidth: true
                            merger: mergeController
                            label: qsTr("New model path")
                            path: root.outputPath
                            placeholder: root.suggestedPath || qsTr("Choose the base model first")
                            allowFile: !mergeController.isDirectory(root.baseModel)
                            saveFile: true
                            folderIsParent: true
                            nameFilters: [qsTr("Safetensors (*.safetensors *.safetensor)"), qsTr("All files (*)")]
                            onEdited: function (value) {
                                root.outputPath = value;
                            }
                            onParentChosen: function (directory) {
                                root.outputPath = mergeController.suggestedOutput(root.baseModel, directory, root.mode);
                            }
                        }
                        LV.Label {
                            Layout.fillWidth: true
                            text: qsTr("Single checkpoints save as safetensors. Diffusers models save to a new folder. Existing outputs are never replaced.")
                            wrapMode: Text.Wrap
                            sizeToContentHeight: true
                            style: caption
                        }
                        MergePathField {
                            touchNavigation: root.touchNavigation
                            objectName: "mergeCacheField"
                            Layout.fillWidth: true
                            merger: mergeController
                            label: qsTr("Conversion cache (optional)")
                            path: root.cacheDirectory
                            placeholder: qsTr("Automatic · Society application cache")
                            allowFile: false
                            onEdited: function (value) {
                                root.cacheDirectory = value;
                            }
                        }
                        LV.Label {
                            text: qsTr("Runtime")
                            style: header
                        }
                        MergePathField {
                            touchNavigation: root.touchNavigation
                            objectName: "mergeExecutableField"
                            Layout.fillWidth: true
                            merger: mergeController
                            label: qsTr("iiLocalDiffusion executable")
                            path: root.mergeExecutable
                            placeholder: mergeController.defaultExecutable || qsTr("Path to iild-merge")
                            allowFolder: false
                            nameFilters: [qsTr("All files (*)")]
                            onEdited: function (value) {
                                root.mergeExecutable = value;
                            }
                        }
                        MergePathField {
                            touchNavigation: root.touchNavigation
                            objectName: "mergePythonField"
                            Layout.fillWidth: true
                            merger: mergeController
                            label: qsTr("Python executable (optional)")
                            path: root.pythonExecutable
                            placeholder: qsTr("Automatic · SDK Python environment")
                            allowFolder: false
                            nameFilters: [qsTr("All files (*)")]
                            onEdited: function (value) {
                                root.pythonExecutable = value;
                            }
                        }
                        LV.Label {
                            Layout.fillWidth: true
                            text: qsTr("Uses the SDK’s local PyTorch / safetensors environment. Arithmetic runs on CPU in FP32 or FP64 and preserves the base tensor precision. Base weight and LoRA alpha/rank scaling are resolved by the SDK.")
                            wrapMode: Text.Wrap
                            sizeToContentHeight: true
                            style: caption
                        }
                    }
                }

                LV.HStack {
                    visible: mergeController.details.length > 0
                    Layout.alignment: Qt.AlignLeft
                    spacing: 8
                    LV.LabelButton {
                        Layout.minimumHeight: root.touchNavigation ? 44 : 0
                        text: root.detailsVisible ? qsTr("Hide report") : qsTr("Show report")
                        tone: LV.AbstractButton.Borderless
                        onClicked: root.detailsVisible = !root.detailsVisible
                    }
                    LV.LabelButton {
                        Layout.minimumHeight: root.touchNavigation ? 44 : 0
                        text: qsTr("Copy full report")
                        tone: LV.AbstractButton.Default
                        onClicked: mergeController.copyDetails()
                    }
                }
                LV.Label {
                    objectName: "mergeReport"
                    Layout.fillWidth: true
                    visible: root.detailsVisible && mergeController.details.length > 0
                    text: mergeController.details.slice(0, 16000)
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    sizeToContentHeight: true
                    style: caption
                }
            }
        }

        LV.VStack {

            alignment: Qt.AlignLeft
            Layout.fillWidth: true
            spacing: 8
            LV.Label {
                objectName: "mergeStatus"
                Layout.fillWidth: true
                text: mergeController.supported ? mergeController.status : qsTr("Model merging is available on desktop.")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                maximumLineCount: root.height < 500 ? 2 : 3
                sizeToContentHeight: true
            }
            LV.Label {
                objectName: "mergeError"
                Layout.fillWidth: true
                visible: mergeController.errorString.length > 0
                text: mergeController.errorString.slice(0, 4000)
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                maximumLineCount: root.height < 500 ? 2 : 4
                sizeToContentHeight: true
            }
            LV.Label {
                Layout.fillWidth: true
                visible: mergeController.busy || mergeController.elapsedSeconds > 0
                text: qsTr("Elapsed: %1 s").arg(mergeController.elapsedSeconds)
                style: caption
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                LV.LabelButton {
                    Layout.minimumHeight: root.touchNavigation ? 44 : 0
                    objectName: "mergeValidate"
                    height: Math.max(implicitHeight, root.touchNavigation ? 44 : 0)
                    text: qsTr("Check inputs")
                    tone: LV.AbstractButton.Default
                    enabled: mergeController.supported && !mergeController.busy && root.inputModelsReady
                    onClicked: mergeController.run(root.requestOptions(), true)
                }
                LV.LabelButton {
                    Layout.minimumHeight: root.touchNavigation ? 44 : 0
                    objectName: "mergeRun"
                    height: Math.max(implicitHeight, root.touchNavigation ? 44 : 0)
                    text: qsTr("Merge models")
                    tone: LV.AbstractButton.Primary
                    enabled: mergeController.supported && !mergeController.busy && root.inputModelsReady
                    onClicked: mergeController.run(root.requestOptions(), false)
                }
                LV.LabelButton {
                    Layout.minimumHeight: root.touchNavigation ? 44 : 0
                    objectName: "mergeCancel"
                    height: Math.max(implicitHeight, root.touchNavigation ? 44 : 0)
                    visible: mergeController.busy
                    text: qsTr("Cancel")
                    tone: LV.AbstractButton.Borderless
                    enabled: !mergeController.cancelling
                    onClicked: mergeController.cancel()
                }
            }
            LV.LabelButton {
                Layout.minimumHeight: root.touchNavigation ? 44 : 0
                objectName: "mergeOpenOutput"
                visible: mergeController.completedOutput.length > 0
                text: qsTr("Open output folder")
                tone: LV.AbstractButton.Default
                onClicked: mergeController.openOutputFolder()
            }
        }
    }

    LV.ContextMenu {
        id: weightMenu
        objectName: "mergeWeightMenu"
        showIconSlot: false
        items: [qsTr("Automatic weights"), qsTr("Shared weight"), qsTr("Per-model weights")]
        selectedIndex: root.weightMode === "automatic" ? 0 : root.weightMode === "shared" ? 1 : 2
        onItemTriggered: function (index, entry) {
            root.weightMode = ["automatic", "shared", "per-model"][index];
        }
    }
}
