pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV
import Society

LV.ContextMenu {
    id: menu
    property string filePath: ""
    property string folderPath: ""
    property bool directory: false
    property bool backgroundOnly: false
    property var categoryOptions: []
    property bool categoryEnabled: true
    property string currentCategory: ""
    readonly property bool currentActionBusy: actions.activeCount > 0 && actions.busyForPath(filePath)
    readonly property alias categoryMenu: categoryMenu
    readonly property alias actions: actions
    signal openRequested(string path)
    signal revealRequested(string path)
    signal modified(string path)
    signal categoryRequested(string path, string category)
    showIconSlot: false
    itemWidth: 220
    property FileActions handler: FileActions {
        id: actions
        path: menu.filePath
        onOperationFinished: function(source, path, error) {
            if (renameSheet.pending && source === renameSheet.selectedPath) {
                renameSheet.pending = false
                renameSheet.operationError = error
                renameSheet.partial = error.length > 0 && path.length > 0
                if (!error.length) renameSheet.close()
                else if (!renameSheet.opened) renameSheet.open()
            }
            if (path.length) menu.modified(path)
        }
    }
    items: backgroundOnly ? [{ label: qsTr("Paste"), enabled: actions.canPaste }] : [
        { label: qsTr("Open"), enabled: !currentActionBusy },
        { label: qsTr("Show in folder"), enabled: !currentActionBusy },
        { label: qsTr("Copy"), enabled: actions.editable && !currentActionBusy },
        { label: qsTr("Copy path"), enabled: filePath.length > 0 },
        { label: qsTr("Paste"), enabled: actions.canPaste && !currentActionBusy },
        { label: qsTr("Duplicate"), enabled: actions.editable && !currentActionBusy },
        { label: qsTr("Rename…"), enabled: actions.editable && !currentActionBusy },
        { label: qsTr("Share…"), enabled: actions.canShare && actions.editable && !currentActionBusy },
        { label: actions.inDeleted ? qsTr("Remove from Deleted…") : qsTr("Delete"), enabled: actions.editable && !currentActionBusy }
    ].concat(categoryOptions.length ? [{ label: qsTr("Categories"), enabled: categoryEnabled && !currentActionBusy,
        hasChildItems: true, showChevron: true, keepOpen: true }] : [])
    onItemTriggered: function(index) {
        if (backgroundOnly) { actions.paste(folderPath); return }
        switch (index) {
        case 0: openRequested(filePath); break
        case 1: revealRequested(folderPath); break
        case 2: actions.copy(); break
        case 3: actions.copyPath(); break
        case 4: actions.paste(directory ? filePath : folderPath); break
        case 5: actions.duplicate(); break
        case 6:
            renameSheet.selectedPath = filePath
            renameSheet.attempted = false
            renameSheet.partial = false
            renameSheet.operationError = ""
            nameInput.text = filePath.substring(filePath.lastIndexOf("/") + 1)
            renameSheet.open()
            nameInput.forceActiveFocus()
            break
        case 7: actions.share(); break
        case 8:
            if (actions.inDeleted) { deleteSheet.selectedPath = filePath; deleteSheet.open() }
            else actions.trash()
            break
        case 9:
            categoryMenu.openAt(menu.x + menu.width, menu.y)
            break
        }
    }
    onClosed: categoryMenu.close()
    property LV.ContextMenu categoryPopup: LV.ContextMenu {
        id: categoryMenu
        objectName: "modelCategoryMenu"
        showIconSlot: false
        itemWidth: 220
        items: menu.categoryOptions.map(function(option) {
            return { label: option.title, category: option.key, selected: option.key === menu.currentCategory }
        })
        implicitHeight: categoryList.implicitHeight + topPadding + bottomPadding
        contentItem: ListView {
            id: categoryList
            objectName: "modelCategoryList"
            clip: true
            implicitWidth: categoryMenu.resolvedItemWidth
            implicitHeight: Math.min(contentHeight, Math.max(0, (categoryMenu.parent ? categoryMenu.parent.height : 600)
                - categoryMenu.topPadding - categoryMenu.bottomPadding - categoryMenu.edgeMargin * 2))
            spacing: categoryMenu.itemSpacing
            model: categoryMenu.entryDelegateItems
            boundsBehavior: Flickable.StopAtBounds
            Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
            delegate: LV.ContextMenuItem {
                required property var modelData
                objectName: "modelCategoryOption" + modelData.index
                width: categoryList.width
                label: modelData.label || ""
                state: modelData.state === undefined ? defaultState : modelData.state
                showIconSlot: false
                enabled: modelData.enabled !== false
                onClicked: categoryMenu.triggerEntry(modelData.index)
            }
        }
        onItemTriggered: function(index, entry) {
            menu.categoryRequested(menu.filePath, entry.category)
            menu.close()
        }
    }
    property LV.Sheet renameDialog: LV.Sheet {
        id: renameSheet
        objectName: "renameFileSheet"
        property string selectedPath: ""
        property bool pending: false
        property bool attempted: false
        property bool partial: false
        property string operationError: ""
        title: qsTr("Rename")
        preferredHeight: 270
        LV.VStack {
            width: parent.width
            spacing: 16
            LV.InputField {
                id: nameInput
                objectName: "fileNameInput"
                Layout.fillWidth: true
                placeholderText: qsTr("File name")
                enabled: !renameSheet.partial && !renameSheet.pending
            }
            LV.Label {
                objectName: "renameStatus"
                Layout.fillWidth: true
                visible: renameSheet.attempted && (renameSheet.pending || renameSheet.operationError.length > 0)
                text: renameSheet.operationError || qsTr("Renaming…")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                sizeToContentHeight: true
                style: description
            }
            LV.HStack {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                LV.LabelButton {
                    objectName: "dismissFileRename"
                    text: renameSheet.partial || renameSheet.pending ? qsTr("Close") : qsTr("Cancel")
                    onClicked: renameSheet.close()
                }
                LV.LabelButton {
                    objectName: "confirmFileRename"
                    text: qsTr("Rename")
                    enabled: nameInput.text.trim().length > 0 && !menu.currentActionBusy && !renameSheet.pending && !renameSheet.partial
                        && menu.filePath === renameSheet.selectedPath
                    onClicked: {
                        renameSheet.attempted = true
                        renameSheet.pending = true
                        renameSheet.operationError = ""
                        actions.rename(nameInput.text)
                    }
                }
            }
        }
    }
    property LV.Sheet deleteDialog: LV.Sheet {
        id: deleteSheet
        objectName: "deleteFileSheet"
        property string selectedPath: ""
        title: qsTr("Remove from Deleted?")
        preferredHeight: 260
        LV.VStack {
            width: parent.width
            spacing: 16
            LV.Label {
                Layout.fillWidth: true
                text: qsTr("“%1” will be removed from Deleted. Any managed originals and recorded history are retained.").arg(deleteSheet.selectedPath.substring(deleteSheet.selectedPath.lastIndexOf("/") + 1))
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                sizeToContentHeight: true
            }
            LV.HStack {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                LV.LabelButton { text: qsTr("Cancel"); onClicked: deleteSheet.close() }
                LV.LabelButton {
                    objectName: "confirmPermanentDelete"
                    text: qsTr("Remove from Deleted")
                    enabled: actions.activeCount === 0 || !actions.busyForPath(deleteSheet.selectedPath)
                    onClicked: { actions.removePath(deleteSheet.selectedPath); deleteSheet.close() }
                }
            }
        }
    }
}
