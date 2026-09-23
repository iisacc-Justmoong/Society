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
    readonly property alias actions: actions
    signal openRequested(string path)
    signal revealRequested(string path)
    signal modified(string path)
    showIconSlot: false
    itemWidth: 220
    property FileActions handler: FileActions {
        id: actions
        path: menu.filePath
        onCompleted: function(path) { menu.modified(path) }
    }
    items: backgroundOnly ? [{ label: qsTr("Paste"), enabled: actions.canPaste && !actions.busy }] : [
        { label: qsTr("Open"), enabled: !actions.busy },
        { label: qsTr("Show in folder"), enabled: !actions.busy },
        { label: qsTr("Copy"), enabled: actions.editable && !actions.busy },
        { label: qsTr("Copy path"), enabled: filePath.length > 0 },
        { label: qsTr("Paste"), enabled: actions.canPaste && !actions.busy },
        { label: qsTr("Duplicate"), enabled: actions.editable && !actions.busy },
        { label: qsTr("Rename…"), enabled: actions.editable && !actions.busy },
        { label: qsTr("Share…"), enabled: actions.canShare && actions.editable && !actions.busy },
        { label: actions.inDeleted ? qsTr("Delete permanently…") : qsTr("Delete"), enabled: actions.editable && !actions.busy }
    ]
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
            nameInput.text = filePath.substring(filePath.lastIndexOf("/") + 1)
            renameSheet.open()
            nameInput.forceActiveFocus()
            break
        case 7: actions.share(); break
        case 8:
            if (actions.inDeleted) { deleteSheet.selectedPath = filePath; deleteSheet.open() }
            else actions.trash()
            break
        }
    }
    property LV.Sheet renameDialog: LV.Sheet {
        id: renameSheet
        objectName: "renameFileSheet"
        property string selectedPath: ""
        title: qsTr("Rename")
        preferredHeight: 230
        LV.VStack {
            width: parent.width
            spacing: 16
            LV.InputField { id: nameInput; objectName: "fileNameInput"; Layout.fillWidth: true; placeholderText: qsTr("File name") }
            LV.HStack {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                LV.LabelButton { text: qsTr("Cancel"); onClicked: renameSheet.close() }
                LV.LabelButton {
                    objectName: "confirmFileRename"
                    text: qsTr("Rename")
                    enabled: nameInput.text.trim().length > 0 && !actions.busy
                    onClicked: { actions.rename(nameInput.text); renameSheet.close() }
                }
            }
        }
    }
    property LV.Sheet deleteDialog: LV.Sheet {
        id: deleteSheet
        objectName: "deleteFileSheet"
        property string selectedPath: ""
        title: qsTr("Delete permanently?")
        preferredHeight: 260
        LV.VStack {
            width: parent.width
            spacing: 16
            LV.Label {
                Layout.fillWidth: true
                text: qsTr("“%1” will be permanently deleted. This cannot be undone.").arg(deleteSheet.selectedPath.substring(deleteSheet.selectedPath.lastIndexOf("/") + 1))
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
                    text: qsTr("Delete permanently")
                    enabled: !actions.busy
                    onClicked: { actions.remove(); deleteSheet.close() }
                }
            }
        }
    }
}
