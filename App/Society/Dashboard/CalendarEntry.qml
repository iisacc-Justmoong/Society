pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV

Item {
    id: root
    required property var entry
    required property string kind
    signal activated()
    signal completionRequested(bool completed)
    implicitHeight: kind === "tasks" ? 24 : 30

    LV.HStack {
        anchors.fill: parent
        spacing: 6
        LV.CheckBox {
            id: check
            objectName: "calendarTask_" + (root.entry.id || "")
            visible: root.kind === "tasks"
            enabled: !root.entry.recurring
            checked: root.entry.completed === true
            Accessible.name: qsTr("Complete %1").arg(root.entry.title)
            onClicked: {
                const requested = checked
                checked = Qt.binding(function() { return root.entry.completed === true })
                root.completionRequested(requested)
            }
        }
        LV.Label {
            visible: root.kind === "events" || root.kind === "reminders"
            Layout.preferredWidth: 40
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 2
            text: root.entry.time || ""
            style: caption
            lineHeight: 11
            color: LV.Theme.accentGreen
        }
        LV.AbstractButton {
            id: action
            objectName: "calendarEntry_" + (root.entry.id || "")
            Layout.fillWidth: true
            Layout.fillHeight: true
            horizontalPadding: 0
            verticalPadding: 0
            tone: LV.AbstractButton.Borderless
            Accessible.name: root.entry.title + ". " + (root.entry.subtitle || "")
            onClicked: root.activated()
            contentItem: LV.VStack {
                spacing: 3
                LV.Label {
                    Layout.fillWidth: true
                    text: root.entry.title || ""
                    textFormat: Text.PlainText
                    style: body
                    lineHeight: 13
                    color: root.entry.completed ? LV.Theme.descriptionColor : LV.Theme.bodyColor
                    font.strikeout: root.entry.completed === true
                    elide: Text.ElideRight
                }
                LV.Label {
                    visible: root.kind !== "tasks"
                    Layout.fillWidth: true
                    text: root.entry.subtitle || (root.kind === "activity" ? root.entry.time : "") || ""
                    textFormat: Text.PlainText
                    style: caption
                    lineHeight: 11
                    color: LV.Theme.descriptionColor
                    elide: Text.ElideRight
                }
            }
        }
        LV.Label {
            visible: root.kind === "tasks"
            text: root.entry.completed ? qsTr("Done") : root.entry.due || ""
            style: caption
            lineHeight: 11
            color: root.entry.completed ? LV.Theme.accentGreen : LV.Theme.descriptionColor
        }
    }
}
