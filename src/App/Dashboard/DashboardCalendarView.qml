pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Society

Item {
    id: root
    objectName: "dashboardCalendarView"
    required property DashboardCalendar model
    signal fileRequested(string path)
    readonly property bool stacked: width < 700
    property string expandedSection: ""
    property int detailPage: 0
    property bool focusSelectedDay: false
    property var inspectedEntry: null
    property string actionMessage: ""
    readonly property var sections: [
        { key: "events", title: qsTr("Events · %1").arg(model.events.length), rows: model.events, limit: 2 },
        { key: "tasks", title: qsTr("Tasks · %1/%2 done").arg(model.completedTasks).arg(model.tasks.length), rows: model.tasks, limit: 2 },
        { key: "activity", title: qsTr("Activity · %1").arg(model.activity.length), rows: model.activity, limit: 1 },
        { key: "files", title: qsTr("Files · %1").arg(model.files.length), rows: model.files, limit: 1 },
        { key: "notes", title: qsTr("Notes · %1").arg(model.notes.length), rows: model.notes, limit: 1 },
        { key: "reminders", title: qsTr("Reminders · %1").arg(model.reminders.length), rows: model.reminders, limit: 1 }
    ]
    readonly property var expandedRows: expandedSection.length > 0 ? model[expandedSection] : []
    implicitHeight: stacked ? 750 : 370

    function inspect(entry, kind) {
        actionMessage = ""
        if (kind === "files") {
            const path = model.attachmentPath(entry.uri)
            if (path.length > 0) fileRequested(path)
            else actionMessage = qsTr("This attachment is not available in the current Society container.")
        } else inspectedEntry = entry
    }
    function resetDetails() { expandedSection = ""; detailPage = 0; inspectedEntry = null; actionMessage = "" }
    Connections {
        target: root.model
        function onContainerPathChanged() { root.resetDetails() }
        function onChanged() {
            if (root.focusSelectedDay) Qt.callLater(function() {
                for (let i = 0; i < dayRepeater.count; ++i) {
                    const cell = dayRepeater.itemAt(i)
                    if (cell && cell.modelData.selected) cell.focusDate()
                }
            })
            if (!root.model.loading) root.focusSelectedDay = false
        }
    }
    GridLayout {
        anchors.fill: parent
        columns: root.stacked ? 1 : 2
        rowSpacing: 10
        columnSpacing: 10
        Rectangle {
            id: monthPanel
            objectName: "calendarMonthPanel"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredHeight: 370
            radius: LV.Theme.radiusLg
            color: LV.Theme.panelBackground04
            border.color: LV.Theme.panelBackground10
            border.width: 1
            clip: true
            LV.HStack {
                id: monthToolbar
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                anchors.leftMargin: 16; anchors.rightMargin: 16
                height: 56
                spacing: 8
                LV.Label {
                    objectName: "calendarMonthTitle"
                    Layout.fillWidth: true
                    text: root.model.monthTitle
                    style: header
                    lineHeight: 17
                    elide: Text.ElideRight
                }
                LV.IconButton {
                    objectName: "calendarPreviousMonth"
                    Layout.preferredWidth: 28; Layout.preferredHeight: 28
                    iconName: "generalchevronLeft"
                    tone: LV.AbstractButton.Default
                    Accessible.name: qsTr("Previous month")
                    onClicked: { root.resetDetails(); root.model.moveMonth(-1) }
                }
                LV.LabelButton {
                    objectName: "calendarToday"
                    Layout.preferredWidth: 64; Layout.preferredHeight: 28
                    text: qsTr("Today")
                    tone: LV.AbstractButton.Default
                    onClicked: { root.resetDetails(); root.model.goToday() }
                }
                LV.IconButton {
                    objectName: "calendarNextMonth"
                    Layout.preferredWidth: 28; Layout.preferredHeight: 28
                    iconName: "generalchevronRight"
                    tone: LV.AbstractButton.Default
                    Accessible.name: qsTr("Next month")
                    onClicked: { root.resetDetails(); root.model.moveMonth(1) }
                }
            }
            Row {
                id: weekdays
                anchors.top: monthToolbar.bottom; width: parent.width
                height: 26
                Repeater {
                    model: [qsTr("Mon"), qsTr("Tue"), qsTr("Wed"), qsTr("Thu"), qsTr("Fri"), qsTr("Sat"), qsTr("Sun")]
                    LV.Label {
                        required property string modelData
                        required property int index
                        width: weekdays.width / 7; height: 26
                        text: modelData; style: caption
                        color: index >= 5 ? LV.Theme.descriptionColor : LV.Theme.bodyColor
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    }
                }
            }
            Grid {
                id: grid
                objectName: "calendarGrid"
                anchors.top: weekdays.bottom; anchors.bottom: parent.bottom
                width: parent.width; columns: 7
                Repeater {
                    id: dayRepeater
                    model: root.model.days
                    Item {
                        id: cell
                        required property var modelData
                        required property int index
                        function focusDate() { dateButton.forceActiveFocus(Qt.OtherFocusReason) }
                        width: grid.width / 7; height: grid.height / 6
                        Rectangle { width: parent.width; height: 1; color: LV.Theme.panelBackground10 }
                        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: LV.Theme.panelBackground10; visible: cell.index % 7 !== 6 }
                        LV.AbstractButton {
                            id: dateButton
                            objectName: "calendarDay_" + cell.modelData.date
                            anchors.fill: parent
                            tone: LV.AbstractButton.Borderless
                            horizontalPadding: 0; verticalPadding: 0
                            Accessible.name: cell.modelData.date + qsTr(", %1 items").arg(cell.modelData.count)
                            Accessible.selected: cell.modelData.selected
                            onClicked: { root.focusSelectedDay = true; root.resetDetails(); root.model.selectDate(cell.modelData.date) }
                            Keys.onLeftPressed: { root.focusSelectedDay = true; root.resetDetails(); root.model.moveDay(-1) }
                            Keys.onRightPressed: { root.focusSelectedDay = true; root.resetDetails(); root.model.moveDay(1) }
                            Keys.onUpPressed: { root.focusSelectedDay = true; root.resetDetails(); root.model.moveDay(-7) }
                            Keys.onDownPressed: { root.focusSelectedDay = true; root.resetDetails(); root.model.moveDay(7) }
                            contentItem: Item {
                                Rectangle {
                                    anchors.top: parent.top; anchors.topMargin: 4
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: Math.max(26, dayLabel.implicitWidth + 8); height: 22; radius: 8
                                    color: cell.modelData.selected ? LV.Theme.accentGreen : "transparent"
                                    border.width: cell.modelData.today && !cell.modelData.selected ? 1 : 0
                                    border.color: LV.Theme.accentGreen
                                    LV.Label {
                                        id: dayLabel
                                        anchors.fill: parent
                                        text: String(cell.modelData.day); style: body
                                        opacity: cell.modelData.inMonth ? 1 : 0.4
                                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                        color: cell.modelData.selected ? LV.Theme.panelBackground01 : LV.Theme.bodyColor
                                    }
                                }
                                Rectangle {
                                    objectName: "calendarDot_" + cell.modelData.date
                                    visible: cell.modelData.count > 0
                                    anchors.top: parent.top; anchors.topMargin: 28
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: 4; height: 4; radius: 2; color: LV.Theme.accentGreen
                                }
                            }
                        }
                    }
                }
            }
        }
        Rectangle {
            id: detailPanel
            objectName: "calendarDetailPanel"
            Layout.fillWidth: root.stacked
            Layout.preferredWidth: root.stacked ? -1 : 355
            Layout.minimumWidth: 0
            Layout.preferredHeight: 370
            radius: LV.Theme.radiusLg
            color: LV.Theme.panelBackground04
            border.color: LV.Theme.panelBackground10; border.width: 1
            clip: true
            LV.HStack {
                id: detailToolbar
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                anchors.leftMargin: 16; anchors.rightMargin: 16
                height: 56; spacing: 8
                LV.IconButton {
                    objectName: "calendarDetailBack"
                    visible: root.expandedSection.length > 0 || root.inspectedEntry !== null
                    Layout.preferredWidth: 28; Layout.preferredHeight: 28
                    iconName: "generalchevronLeft"; tone: LV.AbstractButton.Borderless
                    Accessible.name: qsTr("Back to day summary")
                    onClicked: { if (root.inspectedEntry !== null) root.inspectedEntry = null; else root.expandedSection = "" }
                }
                LV.VStack {
                    Layout.fillWidth: true
                    spacing: 4
                    LV.Label {
                        objectName: "calendarDayTitle"
                        Layout.fillWidth: true; style: header
                        lineHeight: 17
                        text: root.model.dayTitle
                        elide: Text.ElideRight
                    }
                    LV.Label {
                        objectName: "calendarSummary"
                        Layout.fillWidth: true; style: caption
                        lineHeight: 11
                        text: root.model.loading ? qsTr("Loading calendar…") : root.model.summary
                        color: LV.Theme.descriptionColor; elide: Text.ElideRight
                    }
                }
                LV.IconButton {
                    objectName: "calendarPreviousDay"
                    Layout.preferredWidth: 28; Layout.preferredHeight: 28
                    iconName: "generalchevronLeft"; tone: LV.AbstractButton.Borderless
                    Accessible.name: qsTr("Previous day")
                    onClicked: { root.resetDetails(); root.model.moveDay(-1) }
                }
                LV.IconButton {
                    objectName: "calendarNextDay"
                    Layout.preferredWidth: 28; Layout.preferredHeight: 28
                    iconName: "generalchevronRight"; tone: LV.AbstractButton.Borderless
                    Accessible.name: qsTr("Next day")
                    onClicked: { root.resetDetails(); root.model.moveDay(1) }
                }
            }
            Controls.ScrollView {
                id: detailsScroll
                objectName: "calendarDetailsScroll"
                anchors.top: detailToolbar.bottom; anchors.bottom: parent.bottom
                anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: 16; anchors.rightMargin: 16; anchors.bottomMargin: 14; anchors.topMargin: 8
                contentWidth: availableWidth; clip: true
                Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
                LV.VStack {
                    width: detailsScroll.availableWidth
                    spacing: 8
                    LV.Label {
                        visible: root.model.errorString.length > 0 || root.actionMessage.length > 0
                        Layout.fillWidth: true; style: caption; wrapMode: Text.WordWrap; sizeToContentHeight: true
                        text: root.model.errorString || root.actionMessage
                        textFormat: Text.PlainText
                    }
                    Repeater {
                        model: root.expandedSection.length === 0 && root.inspectedEntry === null ? root.sections : []
                        CalendarDetailSection {
                            required property var modelData
                            Layout.fillWidth: true
                            visible: modelData.key !== "notes" && modelData.key !== "reminders" || modelData.rows.length > 0
                            kind: modelData.key; title: modelData.title; entries: modelData.rows; previewLimit: modelData.limit
                            onViewAllRequested: { root.focusSelectedDay = false; root.detailPage = 0; root.expandedSection = modelData.key; detailsScroll.contentItem.contentY = 0 }
                            onEntryRequested: function(entry) { root.inspect(entry, modelData.key) }
                            onCompletionRequested: function(entry, completed) { root.model.setTaskCompleted(entry.id, entry.revision, completed) }
                        }
                    }
                    LV.Label {
                        visible: root.expandedSection.length > 0 && root.inspectedEntry === null
                        Layout.fillWidth: true; style: body
                        text: qsTr("All %1 · %2").arg(root.expandedSection).arg(root.expandedRows.length)
                    }
                    Repeater {
                        model: root.inspectedEntry === null ? root.expandedRows.slice(root.detailPage * 100, (root.detailPage + 1) * 100) : []
                        CalendarEntry {
                            required property var modelData
                            Layout.fillWidth: true
                            kind: root.expandedSection; entry: modelData
                            onActivated: root.inspect(modelData, root.expandedSection)
                            onCompletionRequested: function(completed) { root.model.setTaskCompleted(modelData.id, modelData.revision, completed) }
                        }
                    }
                    LV.HStack {
                        visible: root.expandedRows.length > 100 && root.inspectedEntry === null
                        Layout.fillWidth: true
                        LV.LabelButton {
                            text: qsTr("Previous")
                            enabled: root.detailPage > 0
                            onClicked: { --root.detailPage; detailsScroll.contentItem.contentY = 0 }
                        }
                        LV.Label { Layout.fillWidth: true; text: String(root.detailPage + 1); style: caption; horizontalAlignment: Text.AlignHCenter }
                        LV.LabelButton {
                            text: qsTr("Next")
                            enabled: (root.detailPage + 1) * 100 < root.expandedRows.length
                            onClicked: { ++root.detailPage; detailsScroll.contentItem.contentY = 0 }
                        }
                    }
                    LV.VStack {
                        visible: root.inspectedEntry !== null
                        Layout.fillWidth: true; spacing: 12
                        Repeater {
                            model: root.inspectedEntry === null ? [] : [root.inspectedEntry.title,
                                root.inspectedEntry.description, root.inspectedEntry.start + " → " + root.inspectedEntry.end,
                                root.inspectedEntry.location, qsTr("%1 participants · %2 attachments").arg(root.inspectedEntry.participants).arg(root.inspectedEntry.attachmentCount)]
                            LV.Label {
                                required property string modelData
                                Layout.fillWidth: true; visible: modelData.length > 0
                                text: modelData; textFormat: Text.PlainText; style: body
                                wrapMode: Text.WrapAnywhere; sizeToContentHeight: true
                            }
                        }
                    }
                }
            }
        }
    }
}
