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
    objectName: "modelPackagingTool"
    property string modelsDirectory: ""
    property bool modelsBusy: false
    property bool touchNavigation: false
    property string outputName: ""
    property string outputDirectory: ""
    property alias controller: controller
    readonly property var report: controller.report
    readonly property var components: report.components || []
    readonly property bool showSidebar: width >= 1150 && !touchNavigation
    readonly property bool wideLayout: scroll.width >= 1000
    readonly property int pageMargin: scroll.width < 600 ? 16 : 32
    readonly property bool editingEnabled: controller.supported && !controller.busy && !modelsBusy
    readonly property string effectiveOutputDirectory: outputDirectory || (modelsDirectory ? modelsDirectory + "/Checkpoint" : "")
    readonly property string outputPath: controller.outputPathForName(outputName, effectiveOutputDirectory)
    signal backRequested()
    signal packageCreated(string path)

    function focusBackButton() { scroll.contentItem.contentY = 0; back.forceActiveFocus() }
    function revealFocusedControl(item) {
        if (!visible || !item) return
        let ancestor = item
        while (ancestor && ancestor !== root) ancestor = ancestor.parent
        if (!ancestor) return
        const flick = scroll.contentItem
        const top = item.mapToItem(flick.contentItem, 0, 0).y
        let offset = flick.contentY
        if (top < offset) offset = top - 12
        else if (top + item.height > offset + flick.height) offset = top + item.height - flick.height + 12
        flick.contentY = Math.max(0, Math.min(offset, flick.contentHeight - flick.height))
    }
    function displayOutputFolder() {
        if (modelsDirectory && effectiveOutputDirectory.startsWith(modelsDirectory + "/"))
            return "Models / " + effectiveOutputDirectory.slice(modelsDirectory.length + 1).replace(/\//g, " / ")
        return effectiveOutputDirectory || qsTr("Choose an output folder…")
    }
    function phaseNumber() {
        return controller.phase === "write" ? 1 : controller.phase === "verify" ? 2 : controller.phase === "save" ? 3 : 0
    }
    ModelPackagingController {
        id: controller
        objectName: "modelPackagingController"
        onFinished: function(success, packaging) {
            if (success && !packaging && !root.outputName)
                root.outputName = report.suggested_name || "Model package"
            if (success && packaging) { completion.open(); root.packageCreated(completedOutput) }
        }
    }
    Rectangle { anchors.fill: parent; color: LV.Theme.panelBackground03; z: -1 }
    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged() {
            Qt.callLater(function() { if (root.Window.window) root.revealFocusedControl(root.Window.window.activeFocusItem) })
        }
    }
    FolderDialog {
        id: sourcePicker
        objectName: "packagingSourceDialog"
        title: qsTr("Choose a model folder")
        onAccepted: {
            if (controller.scanFolder(controller.localPath(selectedFolder))) root.outputName = ""
        }
    }
    FolderDialog {
        id: outputPicker
        objectName: "packagingOutputDialog"
        title: qsTr("Choose where to save the model package")
        onAccepted: root.outputDirectory = controller.localPath(selectedFolder)
    }
    component Panel: LV.Card {
        id: panel
        default property alias panelData: contents.data
        type: LV.Card.Model
        displayState: LV.Card.DefaultState
        hoverEnabled: false
        selectable: false
        showMenu: false
        showAction: false
        focusPolicy: Qt.NoFocus
        activeFocusOnTab: false
        horizontalPadding: LV.Theme.gap18 + borderWidth
        verticalPadding: LV.Theme.gap18 + borderWidth
        implicitHeight: contents.implicitHeight + topPadding + bottomPadding
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        background: Rectangle {
            color: panel.surfaceColor
            radius: panel.resolvedCornerRadius
            border.width: panel.borderWidth
            border.color: panel.borderColor
        }
        contentItem: ColumnLayout { id: contents; spacing: 12 }
    }
    component Copy: LV.Label {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        style: body
        color: LV.Theme.descriptionColor
        lineHeight: 20
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        sizeToContentHeight: true
    }
    component Caption: Copy { style: caption; lineHeight: 16 }
    component PathRow: LV.ListItem {
        type: LV.ListItem.Navigation
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        standardItemHeight: root.touchNavigation ? 44 : 36
        iconName: "nodesfolder"
        showDescription: false
        showValue: false
        showTrailingIcon: true
        backgroundColor: LV.Theme.panelBackground04
        backgroundColorHover: LV.Theme.panelBackground07
        cornerRadius: LV.Theme.radiusMd
        enabled: root.editingEnabled
        Accessible.name: label
    }
    Rectangle {
        id: sidebar
        visible: root.showSidebar
        width: visible ? 228 : 0
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: LV.Theme.panelBackground04
        Rectangle { width: 1; anchors.right: parent.right; height: parent.height; color: LV.Theme.panelBackground10 }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            Item { Layout.preferredHeight: 4 }
            Caption { text: qsTr("WORKSPACE") }
            LV.ListItem {
                Layout.fillWidth: true
                type: LV.ListItem.Navigation
                label: qsTr("All tools")
                iconName: "application"
                value: "2"
                showDescription: false
                showTrailingIcon: false
                standardItemHeight: 36
                onClicked: root.backRequested()
            }
            Caption { text: qsTr("CATEGORIES"); Layout.topMargin: 6 }
            LV.ListItem {
                Layout.fillWidth: true
                type: LV.ListItem.Navigation
                label: qsTr("Models")
                iconName: "warehouse"
                value: "2"
                showDescription: false
                showTrailingIcon: false
                standardItemHeight: 36
                backgroundColor: LV.Theme.panelBackground04
                cornerRadius: LV.Theme.radiusMd
                background: Rectangle {
                    color: LV.Theme.panelBackground04
                    radius: LV.Theme.radiusMd
                    border.color: LV.Theme.accentGreen
                    border.width: 1
                }
                onClicked: root.backRequested()
            }
            Item { Layout.fillHeight: true }
            Rectangle { Layout.fillWidth: true; height: 1; color: LV.Theme.panelBackground10 }
            LV.ListItem {
                Layout.fillWidth: true
                type: LV.ListItem.Navigation
                label: qsTr("This Mac")
                description: qsTr("Local workspace")
                iconName: "application"
                showValue: false
                showTrailingIcon: false
                enabled: false
            }
        }
    }
    Controls.ScrollView {
        id: scroll
        objectName: "packagingScroll"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: sidebar.right
        anchors.right: parent.right
        clip: true
        contentWidth: availableWidth
        contentHeight: page.implicitHeight + 2 * root.pageMargin
        Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
        ColumnLayout {
            id: page
            x: root.pageMargin
            y: root.pageMargin
            width: Math.max(0, scroll.availableWidth - 2 * root.pageMargin)
            spacing: 20
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                LV.LabelButton {
                    id: back
                    objectName: "packagingBack"
                    Layout.preferredWidth: 82
                    Layout.preferredHeight: root.touchNavigation ? 36 : 24
                    text: qsTr("‹ All tools")
                    tone: LV.AbstractButton.Borderless
                    onClicked: root.backRequested()
                }
                LV.Label { objectName: "packagingTitle"; text: qsTr("Model Packaging"); style: title; font.pixelSize: 26 }
                Copy { text: qsTr("Turn a model folder into one .safetensors file. Components are detected automatically.") }
            }
            GridLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                columns: root.wideLayout ? 2 : 1
                columnSpacing: 24
                rowSpacing: 20
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.alignment: Qt.AlignTop
                    spacing: 20
                    Panel {
                        objectName: "packagingSourcePanel"
                        LV.Label { text: qsTr("1. Choose a model folder"); style: header2 }
                        Copy { text: qsTr("Use the folder downloaded from Hugging Face or your local model library.") }
                        PathRow {
                            objectName: "packagingSourceFolder"
                            label: controller.inputDirectory || qsTr("Choose a model folder…")
                            onClicked: sourcePicker.open()
                        }
                        Caption {
                            text: controller.busy && !controller.packaging ? controller.status
                                : report.family ? qsTr("%1 detected · %2 files scanned").arg(report.family).arg(report.scanned_file_count)
                                : qsTr("Safetensors, sharded checkpoints, GGUF, LoRA, VAE and model assets")
                        }
                    }
                    Panel {
                        objectName: "packagingComponentsPanel"
                        LV.Label { text: qsTr("2. Review what is included"); style: header2 }
                        Copy { text: qsTr("Everything needed for this package is selected. Original precision is preserved.") }
                        Copy {
                            visible: root.components.length === 0
                            text: qsTr("Choose a folder to detect its model, encoders, decoders and optional components.")
                        }
                        Repeater {
                            model: root.components
                            delegate: Rectangle {
                                id: componentRow
                                required property var modelData
                                objectName: "packagingComponent-" + modelData.id
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                implicitHeight: Math.max(root.touchNavigation ? 52 : 44, componentCopy.implicitHeight + 12)
                                radius: LV.Theme.radiusMd
                                color: LV.Theme.panelBackground04
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 8
                                    Image {
                                        Layout.preferredWidth: 18
                                        Layout.preferredHeight: 18
                                        source: LV.Theme.iconPath(componentRow.modelData.icon)
                                        fillMode: Image.PreserveAspectFit
                                    }
                                    ColumnLayout {
                                        id: componentCopy
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        spacing: 0
                                        LV.Label {
                                            Layout.fillWidth: true
                                            text: componentRow.modelData.label
                                            style: body
                                            textFormat: Text.PlainText
                                            elide: Text.ElideRight
                                        }
                                        LV.Label {
                                            Layout.fillWidth: true
                                            text: componentRow.modelData.description
                                            style: caption
                                            textFormat: Text.PlainText
                                            elide: Text.ElideRight
                                        }
                                    }
                                    LV.Label {
                                        visible: scroll.width >= 440 || !componentRow.modelData.optional
                                        text: !componentRow.modelData.included ? (componentRow.modelData.optional ? qsTr("Excluded") : qsTr("Missing"))
                                            : componentRow.modelData.size > 0 ? controller.formatSize(componentRow.modelData.size) : qsTr("Included")
                                        style: caption
                                        color: componentRow.modelData.included ? LV.Theme.descriptionColor : LV.Theme.accentGreen
                                    }
                                    LV.ToggleSwitch {
                                        objectName: "packagingToggle-" + componentRow.modelData.id
                                        visible: componentRow.modelData.optional
                                        enabled: root.editingEnabled
                                        checked: componentRow.modelData.included
                                        onColor: LV.Theme.accentGreen
                                        Accessible.name: qsTr("Include %1").arg(componentRow.modelData.label)
                                        onToggled: controller.setComponentIncluded(componentRow.modelData.id, checked)
                                    }
                                }
                            }
                        }
                        Caption {
                            visible: !!report.scanned_file_count
                            text: qsTr("%1 duplicate files skipped · %2 invalid downloads skipped")
                                .arg(report.duplicate_file_count || 0).arg(report.invalid_file_count || 0)
                        }
                        LV.LabelButton {
                            objectName: "packagingDetailsButton"
                            visible: !!report.scanned_file_count
                            text: qsTr("View all %1 files and skipped reasons ›").arg(report.scanned_file_count || 0)
                            tone: LV.AbstractButton.Borderless
                            onClicked: details.open()
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: root.wideLayout ? 348 : -1
                    Layout.maximumWidth: root.wideLayout ? 348 : Infinity
                    Layout.minimumWidth: 0
                    Layout.alignment: Qt.AlignTop
                    spacing: 20
                    Panel {
                        objectName: "packagingOutputPanel"
                        LV.Label { text: qsTr("Output"); style: header2 }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            LV.Label { text: qsTr("File name"); style: body }
                            LV.InputField {
                                objectName: "packagingOutputName"
                                Layout.fillWidth: true
                                enabled: root.editingEnabled
                                text: root.outputName
                                placeholderText: qsTr("Model package name")
                                clearButtonVisible: false
                                fieldMinHeight: root.touchNavigation ? 44 : 30
                                Accessible.name: qsTr("Model package file name")
                                onTextEdited: root.outputName = text
                            }
                            Caption { text: qsTr("One model package · .safetensors") }
                            LV.Label { text: qsTr("Save to"); style: body }
                            PathRow {
                                objectName: "packagingOutputFolder"
                                label: root.displayOutputFolder()
                                onClicked: outputPicker.open()
                            }
                            Caption {
                                text: root.outputPath ? root.outputPath.slice(root.outputPath.lastIndexOf("/") + 1) : qsTr("Enter a name and select an output folder")
                                wrapMode: Text.WrapAnywhere
                            }
                        }
                        Copy { text: qsTr("All selected components in one file. Source files are preserved.") }
                    }
                    Panel {
                        objectName: "packagingSummaryPanel"
                        LV.Label { text: report.ready ? qsTr("Ready to package") : qsTr("Package summary"); style: header2 }
                        LV.Label {
                            objectName: "packagingEstimatedSize"
                            text: report.estimated_size_bytes ? "≈ " + controller.formatSize(report.estimated_size_bytes) : "—"
                            style: title
                            font.pixelSize: 26
                        }
                        Caption { text: qsTr("Estimated size · Original precision") }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Copy { text: qsTr("%1 source files → 1 model file").arg(report.included_file_count || 0) }
                            Copy { text: qsTr("%1 components included").arg(report.component_count || 0) }
                            Copy { text: qsTr("%1 of duplicate data avoided").arg(controller.formatSize(report.duplicate_bytes || 0)) }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: LV.Theme.panelBackground10 }
                        Copy {
                            objectName: "packagingError"
                            text: !controller.supported ? qsTr("Install the Model Packaging SDK for this platform.")
                                : controller.errorString || (report.ready
                                    ? qsTr("%1 failed downloads excluded. The valid main model is ready to package.").arg(report.invalid_file_count || 0)
                                    : qsTr("Choose a folder with all required model components."))
                        }
                        LV.LabelButton {
                            id: submitButton
                            objectName: "packagingSubmit"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 34
                            tone: LV.AbstractButton.Primary
                            textColor: LV.Theme.panelBackground03
                            contentItem: LV.Label {
                                objectName: "packagingSubmitLabel"
                                text: submitButton.text
                                style: body
                                color: LV.Theme.panelBackground03
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            text: controller.busy ? controller.status : qsTr("Package model")
                            enabled: root.editingEnabled && !!report.ready && !!root.outputPath
                            onClicked: controller.createPackage(root.outputPath)
                        }
                        LV.LabelButton {
                            visible: !controller.busy && controller.phase === "cancelled"
                            text: qsTr("Inspect folder again")
                            tone: LV.AbstractButton.Borderless
                            onClicked: controller.scanFolder(controller.inputDirectory)
                        }
                    }
                }
            }
        }
    }
    Controls.Popup {
        id: details
        objectName: "packagingDetails"
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(860, parent ? parent.width - 32 : 860)
        height: Math.min(650, parent ? parent.height - 32 : 650)
        padding: 0
        modal: true
        focus: true
        background: Item {}
        contentItem: Panel {
            LV.Label { text: qsTr("Detected files"); style: header2 }
            Copy { text: qsTr("Every input is checked. Duplicate tensors are compared byte for byte; invalid files are excluded.") }
            Controls.ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 100
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 8
                    Repeater {
                        model: report.files || []
                        delegate: Panel {
                            required property var modelData
                            horizontalPadding: 12
                            verticalPadding: 10
                            RowLayout {
                                Layout.fillWidth: true
                                Copy { text: modelData.relative_path; wrapMode: Text.WrapAnywhere; color: LV.Theme.bodyColor }
                                LV.Label { text: modelData.status; style: caption; color: modelData.status === "included" ? LV.Theme.accentGreen : LV.Theme.descriptionColor }
                            }
                            Caption { text: controller.formatSize(modelData.size) + " · " + modelData.format }
                            Copy { text: modelData.reason || qsTr("Included in the model package") }
                        }
                    }
                }
            }
            LV.LabelButton { Layout.alignment: Qt.AlignRight; text: qsTr("Done"); onClicked: details.close() }
        }
    }
    Controls.Popup {
        id: progressDialog
        objectName: "packagingProgress"
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(620, parent ? parent.width - 32 : 620)
        padding: 0
        modal: true
        focus: true
        closePolicy: Controls.Popup.NoAutoClose
        visible: root.visible && controller.busy
        background: Item {}
        contentItem: Panel {
            LV.Label { text: controller.packaging ? qsTr("Packaging your model") : qsTr("Inspecting your model folder"); style: header2 }
            Copy { text: qsTr("Original files stay in place. Completion requires verification of the written file.") }
            LV.Label { text: controller.status; style: header2 }
            Copy { text: controller.detail; wrapMode: Text.WrapAnywhere }
            LV.ProgressBar {
                Layout.fillWidth: true
                currentValue: controller.progress * 100
                fillColor: LV.Theme.accentGreen
                trackColor: LV.Theme.panelBackground10
                visible: !controller.indeterminate
            }
            Controls.BusyIndicator { running: controller.indeterminate; visible: running; Layout.alignment: Qt.AlignHCenter; Layout.preferredHeight: 28 }
            Caption {
                text: (controller.indeterminate ? qsTr("Checking files") : Math.round(controller.progress * 100) + "%")
                    + " · " + qsTr("%1s elapsed").arg(controller.elapsedSeconds)
            }
            Repeater {
                model: controller.packaging ? [qsTr("Inspect files and remove verified duplicates"), qsTr("Write selected components"), qsTr("Verify package integrity"), qsTr("Save the verified file")] : []
                delegate: Copy {
                    required property string modelData
                    required property int index
                    text: (index < root.phaseNumber() ? "✓ " : index === root.phaseNumber() ? "› " : "  ") + modelData
                    color: index <= root.phaseNumber() ? LV.Theme.bodyColor : LV.Theme.descriptionColor
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Copy { text: qsTr("Cancelling removes the unfinished output.") }
                LV.LabelButton {
                    objectName: "packagingCancel"
                    text: controller.cancelling ? qsTr("Cancelling…") : qsTr("Cancel")
                    enabled: !controller.cancelling
                    onClicked: controller.cancel()
                }
            }
        }
    }
    Controls.Popup {
        id: completion
        objectName: "packagingCompletion"
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(620, parent ? parent.width - 32 : 620)
        padding: 0
        modal: true
        focus: true
        background: Item {}
        contentItem: Panel {
            LV.Label { text: qsTr("Model package ready"); style: header2; color: LV.Theme.accentGreen }
            Copy { text: controller.completedOutput; wrapMode: Text.WrapAnywhere; color: LV.Theme.bodyColor }
            Copy { text: qsTr("%1 · %2 components · Saved and verified").arg(controller.formatSize(report.size || 0)).arg(report.component_count || 0) }
            Caption { text: "SHA-256: " + (report.sha256 || ""); wrapMode: Text.WrapAnywhere }
            Copy { text: qsTr("The package retains each component and its original format. Use a compatible package reader to load all components.") }
            RowLayout {
                Layout.fillWidth: true
                LV.LabelButton { text: qsTr("Copy details"); onClicked: controller.copyDetails() }
                Item { Layout.fillWidth: true }
                LV.LabelButton { text: qsTr("Open folder"); onClicked: controller.openOutputFolder() }
                LV.LabelButton { text: qsTr("Done"); tone: LV.AbstractButton.Primary; onClicked: completion.close() }
            }
        }
    }
}
