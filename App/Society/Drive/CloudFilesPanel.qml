pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society

LV.Sheet {
    id: root
    property CloudFilesController controller: null
    parent: Controls.Overlay.overlay
    title: qsTr("Cloud Files")
    preferredHeight: 650
    contentPadding: 16
    LV.VStack {
        width: parent.width
        spacing: 12
        LV.Label {
            Layout.fillWidth: true
            text: root.controller && root.controller.quotaBytes > 0
                ? qsTr("%1 of 5 GB used · %2 GB uploading").arg((root.controller.usedBytes / 1000000000).toFixed(3)).arg((root.controller.reservedBytes / 1000000000).toFixed(3))
                : qsTr("5 GB free with your iisacc account")
            textFormat: Text.PlainText
        }
        LV.Label {
            Layout.fillWidth: true
            text: root.controller ? root.controller.status : ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            sizeToContentHeight: true
        }
        LV.HStack {
            Layout.fillWidth: true
            LV.InputField {
                id: localPath
                objectName: "cloudLocalPath"
                Layout.fillWidth: true
                placeholderText: qsTr("Path inside Files, e.g. Documents/report.pdf")
            }
            LV.LabelButton {
                objectName: "uploadCloudFile"
                text: qsTr("Upload")
                enabled: root.controller && !root.controller.busy && localPath.text.length > 0
                onClicked: root.controller.uploadFile(localPath.text)
            }
            LV.LabelButton {
                text: qsTr("Refresh")
                enabled: root.controller && !root.controller.busy
                onClicked: root.controller.refresh()
            }
        }
        Controls.ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 350
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 12
                Repeater {
                    model: root.controller ? root.controller.uploads : []
                    LV.VStack {
                        required property var modelData
                        Layout.fillWidth: true
                        LV.Label { text: qsTr("Pending: %1 (%2)").arg(modelData.path).arg(modelData.state); textFormat: Text.PlainText }
                        LV.HStack {
                            LV.LabelButton {
                                text: qsTr("Finish upload")
                                enabled: root.controller && !root.controller.busy && ["uploading", "completing"].includes(modelData.state)
                                onClicked: root.controller.finishUpload(modelData.id)
                            }
                            LV.LabelButton {
                                text: qsTr("Cancel upload")
                                enabled: root.controller && !root.controller.busy && ["creating", "uploading", "aborting"].includes(modelData.state)
                                onClicked: root.controller.cancelUpload(modelData.id)
                            }
                        }
                    }
                }
                Repeater {
                    model: root.controller ? root.controller.files : []
                    LV.VStack {
                        required property var modelData
                        Layout.fillWidth: true
                        LV.Label {
                            Layout.fillWidth: true
                            text: modelData.path + " · " + modelData.size + qsTr(" bytes")
                            textFormat: Text.PlainText
                            wrapMode: Text.WrapAnywhere
                            sizeToContentHeight: true
                        }
                        LV.HStack {
                            LV.LabelButton {
                                text: qsTr("Download")
                                enabled: root.controller && !root.controller.busy
                                onClicked: root.controller.downloadFile(modelData.path)
                            }
                            LV.LabelButton {
                                text: qsTr("Replace with local file")
                                enabled: root.controller && !root.controller.busy
                                onClicked: root.controller.uploadFile(modelData.path, modelData.revision)
                            }
                            LV.LabelButton {
                                text: qsTr("Delete from cloud")
                                enabled: root.controller && !root.controller.busy
                                onClicked: root.controller.removeFile(modelData.path, modelData.revision)
                            }
                        }
                    }
                }
                LV.LabelButton {
                    text: qsTr("Load more")
                    visible: root.controller && root.controller.nextCursor.length > 0
                    enabled: root.controller && !root.controller.busy
                    onClicked: root.controller.loadMore()
                }
            }
        }
        LV.Label {
            Layout.fillWidth: true
            text: qsTr("Uploads are explicit. Downloads keep existing local files. Create the matching local folder in Files first.")
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            sizeToContentHeight: true
            style: caption
        }
        LV.LabelButton {
            text: qsTr("Retry cloud cleanup")
            enabled: root.controller && !root.controller.busy
            onClicked: root.controller.retryCleanup()
        }
    }
}
