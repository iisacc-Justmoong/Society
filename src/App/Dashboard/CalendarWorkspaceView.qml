pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV
import Society

LV.VStack {
    id: root
    alignment: Qt.AlignLeft
    objectName: "calendarWorkspaceView"
    required property CalendarWorkspace workspace
    required property DashboardCalendar calendar
    readonly property bool fullContext: width >= 1000
    readonly property bool fullInspector: width >= 1200
    signal fileRequested(string path)
    spacing: 0
    function edit(id, date, minute) {
        editor.begin(id || "", date || "", minute === undefined ? 540 : minute);
    }
    LV.HStack {
        Layout.fillWidth: true
        Layout.preferredHeight: root.width < 600 ? 96 : 64
        Layout.leftMargin: 16
        Layout.rightMargin: 16
        spacing: 12
        LV.VStack {
            alignment: Qt.AlignLeft
            visible: root.width >= 600
            spacing: 3
            LV.Label {
                text: qsTr("Calendar")
                style: title2
            }
        }
        LV.Label {
            visible: root.width > 850
            Layout.fillWidth: true
            text: qsTr("%1 calendars · %2 tasks due this day").arg(root.workspace.calendars.length).arg(root.calendar.tasks.length)
            style: caption
            elide: Text.ElideRight
        }
        Item {
            visible: root.width <= 850
            Layout.fillWidth: true
        }
        LV.LabelSegmentedControl {
            id: modes
            objectName: "calendarViewSelector"
            Layout.preferredWidth: root.width < 600 ? 200 : 260
            Repeater {
                model: [
                    {
                        id: "day",
                        label: qsTr("Day")
                    },
                    {
                        id: "week",
                        label: qsTr("Week")
                    },
                    {
                        id: "month",
                        label: qsTr("Month")
                    },
                    {
                        id: "year",
                        label: qsTr("Year")
                    }
                ]
                LV.LabelButton {
                    required property var modelData
                    objectName: "calendarMode_" + modelData.id
                    width: (modes.width - 14) / 4
                    text: modelData.label
                    horizontalPadding: root.width < 600 ? 4 : 8
                    textColor: root.workspace.viewMode === modelData.id ? LV.Theme.accentGreen : LV.Theme.descriptionColor
                    backgroundColor: root.workspace.viewMode === modelData.id ? LV.Theme.accentGreenMuted : "transparent"
                    onClicked: root.workspace.viewMode = modelData.id
                }
            }
        }
        LV.LabelButton {
            objectName: "calendarNewEvent"
            implicitWidth: root.width < 600 ? 72 : 100
            text: root.width < 600 ? qsTr("+ New") : qsTr("+ New event")
            tone: LV.AbstractButton.Primary
            enabled: root.calendar.databasePath.length > 0
            onClicked: root.edit("")
        }
    }
    LV.HStack {
        visible: !root.fullContext || !root.fullInspector
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.rightMargin: 16
        Layout.bottomMargin: 8
        spacing: 8
        LV.LabelButton {
            visible: !root.fullContext
            text: qsTr("Calendars & tasks")
            tone: LV.AbstractButton.Default
            onClicked: contextDrawer.open()
        }
        Item {
            Layout.fillWidth: true
        }
        LV.LabelButton {
            visible: !root.fullInspector
            text: qsTr("Event details")
            tone: LV.AbstractButton.Default
            onClicked: inspectorDrawer.open()
        }
    }
    LV.HStack {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 0
        CalendarContext {
            visible: root.fullContext
            Layout.preferredWidth: 216
            Layout.fillHeight: true
            Layout.margins: 16
            workspace: root.workspace
            calendar: root.calendar
            onEditRequested: function (id) {
                root.edit(id);
            }
        }
        Rectangle {
            visible: root.fullContext
            width: 1
            Layout.fillHeight: true
            color: LV.Theme.panelBackground08
        }
        CalendarPeriodView {
            objectName: "calendarPeriodView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            workspace: root.workspace
            calendar: root.calendar
            onCreateRequested: function (date, minute) {
                root.edit("", date, minute);
            }
            onInspectRequested: if (!root.fullInspector)
                inspectorDrawer.open()
        }
        Rectangle {
            visible: root.fullInspector
            width: 1
            Layout.fillHeight: true
            color: LV.Theme.panelBackground08
        }
        CalendarInspector {
            visible: root.fullInspector
            Layout.preferredWidth: 256
            Layout.fillHeight: true
            Layout.margins: 16
            workspace: root.workspace
            calendar: root.calendar
            onEditRequested: function (id) {
                root.edit(id);
            }
            onFileRequested: function (path) {
                root.fileRequested(path);
            }
        }
    }
    Controls.Drawer {
        id: contextDrawer
        objectName: "calendarContextDrawer"
        width: Math.min(280, root.width - 24)
        height: root.height
        padding: 16
        edge: Qt.LeftEdge
        background: Rectangle {
            color: LV.Theme.surfacePanel
        }
        contentItem: CalendarContext {
            workspace: root.workspace
            calendar: root.calendar
            onEditRequested: function (id) {
                contextDrawer.close();
                root.edit(id);
            }
        }
    }
    Controls.Drawer {
        id: inspectorDrawer
        objectName: "calendarInspectorDrawer"
        width: Math.min(320, root.width - 24)
        height: root.height
        padding: 16
        edge: Qt.RightEdge
        background: Rectangle {
            color: LV.Theme.surfacePanel
        }
        contentItem: CalendarInspector {
            workspace: root.workspace
            calendar: root.calendar
            showClose: true
            onCloseRequested: inspectorDrawer.close()
            onEditRequested: function (id) {
                inspectorDrawer.close();
                root.edit(id);
            }
            onFileRequested: function (path) {
                root.fileRequested(path);
            }
        }
    }
    CalendarEventEditor {
        id: editor
        workspace: root.workspace
    }
}
