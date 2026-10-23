pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV
import Society

Controls.ScrollView {
    id: root
    required property CalendarWorkspace workspace
    required property DashboardCalendar calendar
    signal editRequested(string id)
    contentWidth: availableWidth
    clip: true
    Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
    LV.VStack {
        alignment: Qt.AlignLeft
        width: root.availableWidth
        spacing: 12
        CalendarMiniMonth {
            Layout.fillWidth: true
            Layout.preferredHeight: 232
            days: root.calendar.days
            title: root.calendar.monthTitle
            onDateRequested: function (date) {
                root.calendar.selectDate(date);
            }
            onMonthRequested: function (delta) {
                root.calendar.moveMonth(delta);
            }
        }
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: LV.Theme.panelBackground08
        }
        LV.HStack {
            Layout.fillWidth: true
            LV.Label {
                Layout.fillWidth: true
                text: qsTr("MY CALENDARS")
                style: caption
                color: LV.Theme.descriptionColor
            }
            LV.Label {
                text: qsTr("%1 selected").arg(root.workspace.calendars.filter(c => c.visible).length)
                style: caption
            }
        }
        LV.VStack {
            alignment: Qt.AlignLeft
            Layout.fillWidth: true
            spacing: 12
            Repeater {
                model: root.workspace.calendars
                LV.HStack {
                    id: collection
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 6
                    LV.CheckBox {
                        objectName: "workspaceCalendar_" + collection.modelData.id
                        checked: collection.modelData.visible
                        Accessible.name: collection.modelData.name
                        onClicked: {
                            const visible = checked;
                            checked = Qt.binding(function () {
                                return collection.modelData.visible;
                            });
                            root.workspace.setCalendarVisible(collection.modelData.id, visible);
                        }
                    }
                    Rectangle {
                        width: 6
                        height: 6
                        radius: 3
                        color: collection.modelData.colorKey === "purple" ? LV.Theme.accentPurple : collection.modelData.colorKey === "blue" ? LV.Theme.accentBlue : collection.modelData.colorKey === "orange" ? LV.Theme.accentOrange : LV.Theme.accentGreen
                    }
                    LV.Label {
                        Layout.fillWidth: true
                        text: collection.modelData.name
                        style: body
                        elide: Text.ElideRight
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: LV.Theme.panelBackground08
        }
        LV.HStack {
            Layout.fillWidth: true
            LV.Label {
                Layout.fillWidth: true
                text: qsTr("Upcoming")
                style: header
            }
            LV.Label {
                text: qsTr("Tasks %1").arg(root.calendar.tasks.length)
                style: body
            }
        }
        LV.Label {
            Layout.fillWidth: true
            text: root.calendar.selectedDate + qsTr(" · NEXT 7 DAYS")
            style: caption
            color: LV.Theme.descriptionColor
        }
        Repeater {
            model: root.workspace.upcoming.slice(0, 8)
            CalendarEventCard {
                required property var modelData
                Layout.fillWidth: true
                entry: modelData
                selected: root.workspace.selectedEvent.occurrenceKey === modelData.occurrenceKey
                onClicked: {
                    root.calendar.selectDate(modelData.date);
                    root.workspace.selectEvent(modelData.id, modelData.start);
                }
            }
        }
        LV.Label {
            visible: root.workspace.upcoming.length === 0
            Layout.fillWidth: true
            text: qsTr("No upcoming events")
            style: description
        }
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: LV.Theme.panelBackground08
        }
        LV.Label {
            text: qsTr("TASKS DUE THIS DAY")
            style: caption
            color: LV.Theme.descriptionColor
        }
        Repeater {
            model: root.calendar.tasks
            CalendarEntry {
                required property var modelData
                Layout.fillWidth: true
                entry: modelData
                kind: "tasks"
                onActivated: root.editRequested(modelData.id)
                onCompletionRequested: function (completed) {
                    root.calendar.setTaskCompleted(modelData.id, String(modelData.revision), completed);
                }
            }
        }
        LV.Label {
            visible: root.calendar.tasks.length === 0
            text: qsTr("No tasks due")
            style: description
        }
        LV.LabelButton {
            text: qsTr("+ New task")
            tone: LV.AbstractButton.Borderless
            onClicked: root.editRequested("task:new")
        }
    }
}
