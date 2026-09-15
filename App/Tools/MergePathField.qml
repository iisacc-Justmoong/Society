pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import LVRS 1.0 as LV
import Society

LV.VStack {
    id: root

    alignment: Qt.AlignLeft
    required property ModelMergeController merger
    property bool touchNavigation: false
    property string label: ""
    property string path: ""
    property string placeholder: ""
    property bool allowFile: true
    property bool allowFolder: true
    property bool saveFile: false
    property bool folderIsParent: false
    property var nameFilters: [qsTr("Model files (*.safetensors *.safetensor *.ckpt *.pt *.pth *.bin)"), qsTr("All files (*)")]
    signal edited(string value)
    signal parentChosen(string directory)
    spacing: 6

    LV.Label {
        Layout.fillWidth: true
        visible: root.label.length > 0
        text: root.label
        style: body
        textFormat: Text.PlainText
    }
    LV.InputField {
        Layout.minimumHeight: root.touchNavigation ? 44 : 0
        objectName: root.objectName + "Input"
        Layout.fillWidth: true
        text: root.path
        placeholderText: root.placeholder
        Accessible.name: root.label
        onTextEdited: function (value) {
            root.edited(value);
        }
    }
    LV.HStack {
        Layout.alignment: Qt.AlignLeft
        spacing: 8
        LV.LabelButton {
            Layout.minimumHeight: root.touchNavigation ? 44 : 0
            objectName: root.objectName + "FileButton"
            visible: root.allowFile
            text: root.saveFile ? qsTr("Save as…") : qsTr("Choose file…")
            Accessible.name: qsTr("%1: choose file").arg(root.label)
            tone: LV.AbstractButton.Default
            onClicked: fileDialog.open()
        }
        LV.LabelButton {
            Layout.minimumHeight: root.touchNavigation ? 44 : 0
            objectName: root.objectName + "FolderButton"
            visible: root.allowFolder
            text: root.folderIsParent ? qsTr("Choose parent folder…") : qsTr("Choose folder…")
            Accessible.name: qsTr("%1: choose folder").arg(root.label)
            tone: LV.AbstractButton.Default
            onClicked: folderDialog.open()
        }
    }
    FileDialog {
        id: fileDialog
        title: root.label
        fileMode: root.saveFile ? FileDialog.SaveFile : FileDialog.OpenFile
        nameFilters: root.nameFilters
        onAccepted: root.edited(root.merger.localPath(selectedFile))
    }
    FolderDialog {
        id: folderDialog
        title: root.label
        onAccepted: {
            const path = root.merger.localPath(selectedFolder);
            if (root.folderIsParent)
                root.parentChosen(path);
            else
                root.edited(path);
        }
    }
}
