pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV

LV.VStack {
    id: root
    required property string kind
    required property string title
    required property var entries
    property int previewLimit: 1
    signal viewAllRequested()
    signal entryRequested(var entry)
    signal completionRequested(var entry, bool completed)
    spacing: 2
    LV.HStack {
        Layout.fillWidth: true
        Layout.preferredHeight: 22
        spacing: 4
        LV.Label {
            Layout.fillWidth: true
            text: root.title
            style: caption
            lineHeight: 11
            color: LV.Theme.descriptionColor
            elide: Text.ElideRight
        }
        LV.AbstractButton {
            objectName: "calendarViewAll_" + root.kind
            visible: root.entries.length > root.previewLimit
            implicitWidth: contentItem.implicitWidth + 4
            implicitHeight: 22
            horizontalPadding: 2
            verticalPadding: 0
            tone: LV.AbstractButton.Borderless
            Accessible.name: qsTr("View all %1").arg(root.kind)
            onClicked: root.viewAllRequested()
            contentItem: LV.Label {
                text: qsTr("View all %1").arg(root.kind)
                style: caption
                lineHeight: 11
                color: LV.Theme.bodyColor
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
    Repeater {
        model: root.entries.slice(0, root.previewLimit)
        CalendarEntry {
            required property var modelData
            Layout.fillWidth: true
            kind: root.kind
            entry: modelData
            onActivated: root.entryRequested(modelData)
            onCompletionRequested: function(completed) { root.completionRequested(modelData, completed) }
        }
    }
    LV.Label {
        visible: root.entries.length === 0
        Layout.fillWidth: true
        Layout.preferredHeight: 20
        text: qsTr("No %1 for this day").arg(root.kind)
        style: caption
        color: LV.Theme.descriptionColor
    }
}
