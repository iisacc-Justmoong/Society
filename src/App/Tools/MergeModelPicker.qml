pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society

LV.VStack {
    id: root
    required property MergeModelCatalog catalog
    property bool touchNavigation: false
    property string label: ""
    property string path: ""
    property string detailText: ""
    property bool baseOnly: false
    readonly property var choices: catalog.models.filter(model => !baseOnly || model.baseEligible)
    readonly property int selectedIndex: choices.findIndex(model => model.path === path)
    readonly property var selectedModel: selectedIndex >= 0 ? choices[selectedIndex] : null
    signal edited(string value)
    alignment: Qt.AlignLeft
    spacing: 6
    onEnabledChanged: if (!enabled && menu) menu.close()

    function formatLabel(model) {
        const format = model.kind === "diffusers" ? qsTr("Diffusers folder")
            : model.kind === "adapter" ? qsTr("Adapter folder") : model.format;
        return model.modelType && model.modelType !== "Other" ? model.modelType + " · " + format : format;
    }
    function openMenu() {
        if (!root.enabled || catalog.directory.length === 0)
            return;
        catalog.refresh();
        menu.openFor(button, 0, button.height + 2);
    }

    LV.ListItem {
        id: button
        objectName: root.objectName + "Button"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        type: LV.ListItem.Navigation
        standardItemHeight: LV.Theme.scaleMetric(56)
        implicitHeight: standardItemHeight
        label: root.selectedModel ? root.selectedModel.name : qsTr("Select a model…")
        description: root.label + (root.selectedModel ? " · " + root.formatLabel(root.selectedModel) : "")
        iconName: root.baseOnly ? "warehouse" : "toolWindowModelChecker"
        value: root.selectedModel ? qsTr("Change") : qsTr("Choose")
        showDescription: true
        showValue: true
        showTrailingIcon: true
        cornerRadius: LV.Theme.radiusMd
        backgroundColor: LV.Theme.panelBackground04
        backgroundColorHover: LV.Theme.panelBackground07
        backgroundColorPressed: LV.Theme.accentMuted
        enabled: root.catalog.directory.length > 0
        Accessible.name: root.label + ": " + (root.selectedModel ? root.selectedModel.relativePath : label)
        Accessible.description: root.selectedModel
            ? qsTr("Models / %1 · %2. %3").arg(root.selectedModel.relativePath).arg(root.formatLabel(root.selectedModel)).arg(root.detailText)
            : root.label
        onClicked: root.openMenu()
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Down || event.key === Qt.Key_Menu
                    || (event.key === Qt.Key_F10 && event.modifiers & Qt.ShiftModifier)) {
                root.openMenu();
                event.accepted = true;
            }
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: root.openMenu()
        }
    }
    LV.Label {
        Layout.fillWidth: true
        visible: root.selectedModel !== null && root.selectedModel.relativePath !== root.selectedModel.name
        text: root.selectedModel ? qsTr("Models / %1 · %2").arg(root.selectedModel.relativePath).arg(root.formatLabel(root.selectedModel)) : ""
        textFormat: Text.PlainText
        wrapMode: Text.WrapAnywhere
        sizeToContentHeight: true
        style: caption
    }
    LV.Label {
        objectName: root.objectName + "Compatibility"
        Layout.fillWidth: true
        visible: root.selectedModel !== null && root.detailText.length > 0
        text: root.detailText
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        sizeToContentHeight: true
        style: caption
        color: LV.Theme.textSecondary
    }

    LV.ContextMenu {
        id: menu
        objectName: root.objectName + "Menu"
        showIconSlot: false
        selectedIndex: root.selectedIndex
        items: root.choices.length > 0 ? root.choices.map(model => ({
            label: model.relativePath + " · " + root.formatLabel(model)
                + (model.ecosystemLabel ? " · " + model.ecosystemLabel : ""), path: model.path
        })) : [{label: root.catalog.loading ? qsTr("Loading models…") : qsTr("No models found in Models"), enabled: false}]
        implicitWidth: Math.max(0, Math.min(root.width, parent ? parent.width - edgeMargin * 2 : root.width))
        itemWidth: Math.max(0, implicitWidth - leftPadding - rightPadding)
        implicitHeight: Math.min(modelList.contentHeight + topPadding + bottomPadding, LV.Theme.scaleMetric(320),
            parent ? Math.max(0, parent.height - edgeMargin * 2) : LV.Theme.scaleMetric(320))
        onItemTriggered: function(index, entry) {
            if (root.enabled && entry.path && root.catalog.contains(entry.path, root.baseOnly))
                root.edited(entry.path);
        }
        onOpened: {
            modelList.currentIndex = Math.max(0, selectedIndex);
            modelList.positionViewAtIndex(modelList.currentIndex, ListView.Contain);
            modelList.forceActiveFocus();
        }
        onClosed: button.forceActiveFocus()
        contentItem: ListView {
            id: modelList
            objectName: root.objectName + "List"
            implicitHeight: contentHeight
            model: menu.items
            spacing: menu.itemSpacing
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: true
            Controls.ScrollBar.vertical: Controls.ScrollBar {}
            Keys.onReturnPressed: menu.triggerEntry(currentIndex)
            Keys.onEnterPressed: menu.triggerEntry(currentIndex)
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Home) {
                    currentIndex = 0;
                    positionViewAtBeginning();
                    event.accepted = true;
                } else if (event.key === Qt.Key_End) {
                    currentIndex = count - 1;
                    positionViewAtEnd();
                    event.accepted = true;
                }
            }
            delegate: LV.MenuItem {
                required property int index
                required property var modelData
                objectName: root.objectName + "Option" + index
                width: modelList.width
                itemWidth: menu.itemWidth
                label: modelData.label
                keyVisible: false
                showIconSlot: false
                hasChildItems: false
                enabled: modelData.enabled !== false
                state: index === modelList.currentIndex ? selectedState : defaultState
                Accessible.name: modelData.label
                onClicked: menu.triggerEntry(index)
            }
        }
    }
}
