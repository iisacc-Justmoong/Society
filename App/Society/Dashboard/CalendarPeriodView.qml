pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV
import Society

LV.VStack {
    id: root
    alignment: Qt.AlignLeft
    required property CalendarWorkspace workspace
    required property DashboardCalendar calendar
    signal createRequested(string date, int minute)
    signal inspectRequested
    spacing: 8
    function dayEvents(date) {
        return workspace.visibleEvents.filter(e => e.date === date);
    }
    function choose(entry) {
        workspace.selectEvent(entry.id, entry.start);
        inspectRequested();
    }
    // Only pixel packing lives here; the SDK supplies civil dates and occurrence minutes.
    function packedEvents() {
        const result = [];
        for (let day = 0; day < workspace.periodDays.length; ++day) {
            const date = workspace.periodDays[day].date;
            const entries = workspace.visibleEvents.filter(e => e.date === date && !e.allDay).slice().sort((a, b) => a.startMinute - b.startMinute || b.endMinute - a.endMinute);
            let group = [], ends = [], groupEnd = -1;
            function flush() {
                for (const item of group) {
                    item.lanes = Math.max(1, ends.length);
                    result.push(item);
                }
                group = [];
                ends = [];
                groupEnd = -1;
            }
            for (const entry of entries) {
                if (entry.startMinute >= groupEnd && group.length)
                    flush();
                let lane = ends.findIndex(end => end <= entry.startMinute);
                if (lane < 0)
                    lane = ends.length;
                ends[lane] = entry.endMinute;
                groupEnd = Math.max(groupEnd, entry.endMinute);
                group.push(Object.assign({}, entry, {
                    dayIndex: day,
                    lane: lane
                }));
            }
            flush();
        }
        return result;
    }
    LV.HStack {
        Layout.fillWidth: true
        Layout.preferredHeight: 40
        spacing: 8
        LV.Label {
            objectName: "calendarPeriodTitle"
            Layout.fillWidth: true
            text: root.workspace.periodTitle
            style: header
            elide: Text.ElideRight
        }
        LV.IconButton {
            objectName: "calendarPreviousPeriod"
            iconName: "generalchevronLeft"
            implicitWidth: 22
            tone: LV.AbstractButton.Default
            Accessible.name: qsTr("Previous period")
            onClicked: root.workspace.movePeriod(-1)
        }
        LV.LabelButton {
            objectName: "calendarToday"
            text: qsTr("Today")
            implicitWidth: 52
            horizontalPadding: 4
            tone: LV.AbstractButton.Default
            onClicked: root.calendar.goToday()
        }
        LV.IconButton {
            objectName: "calendarNextPeriod"
            iconName: "generalchevronRight"
            implicitWidth: 22
            tone: LV.AbstractButton.Default
            Accessible.name: qsTr("Next period")
            onClicked: root.workspace.movePeriod(1)
        }
    }
    Loader {
        Layout.fillWidth: true
        Layout.fillHeight: true
        sourceComponent: root.workspace.viewMode === "month" ? monthView : root.workspace.viewMode === "year" ? yearView : timeView
    }
    LV.HStack {
        Layout.fillWidth: true
        Layout.preferredHeight: 16
        spacing: 4
        Rectangle {
            width: 5
            height: 5
            radius: 3
            color: root.workspace.errorString ? LV.Theme.accentOrange : LV.Theme.accentGreen
        }
        LV.Label {
            Layout.fillWidth: true
            text: root.workspace.loading ? qsTr("Reading calendar…") : root.workspace.errorString || qsTr("Saved in this Society container")
            textFormat: Text.PlainText
            style: caption
            elide: Text.ElideRight
        }
        LV.Label {
            visible: root.width > 420
            text: root.calendar.timeZone
            style: caption
        }
    }

    Component {
        id: timeView
        LV.VStack {
            id: timeline
            alignment: Qt.AlignLeft
            readonly property real gutter: 48
            readonly property real hourHeight: 64
            readonly property int dayCount: root.workspace.periodDays.length
            readonly property real dayWidth: Math.max(1, (width - gutter) / Math.max(1, dayCount))
            readonly property var timed: root.packedEvents()
            spacing: 0
            Row {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                Item {
                    width: timeline.gutter
                    height: 56
                    LV.Label {
                        anchors.centerIn: parent
                        text: root.workspace.zoneOffset
                        style: caption
                        elide: Text.ElideRight
                        width: parent.width
                    }
                }
                Repeater {
                    model: root.workspace.periodDays
                    LV.AbstractButton {
                        id: weekdayButton
                        required property var modelData
                        width: timeline.dayWidth
                        height: 56
                        tone: LV.AbstractButton.Borderless
                        Accessible.name: modelData.date
                        onClicked: root.calendar.selectDate(modelData.date)
                        contentItem: LV.VStack {
                            alignment: Qt.AlignLeft
                            spacing: 8
                            LV.Label {
                                Layout.fillWidth: true
                                text: weekdayButton.modelData.weekdayName
                                style: caption
                                color: weekdayButton.modelData.selected ? LV.Theme.accentGreen : LV.Theme.descriptionColor
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                width: 28
                                height: 22
                                radius: LV.Theme.radiusMd
                                color: weekdayButton.modelData.selected ? LV.Theme.accentGreen : "transparent"
                                LV.Label {
                                    anchors.centerIn: parent
                                    text: weekdayButton.modelData.day
                                    style: body
                                    color: weekdayButton.modelData.selected ? LV.Theme.titleHeaderColor : LV.Theme.bodyColor
                                }
                            }
                        }
                    }
                }
            }
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                LV.Label {
                    width: timeline.gutter
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("All-day")
                    style: caption
                }
                Repeater {
                    model: root.workspace.periodDays
                    Item {
                        required property var modelData
                        required property int index
                        x: timeline.gutter + index * timeline.dayWidth
                        width: timeline.dayWidth
                        height: 28
                        readonly property var entries: root.dayEvents(modelData.date).filter(e => e.allDay)
                        CalendarEventCard {
                            visible: parent.entries.length > 0
                            anchors.fill: parent
                            anchors.margins: 3
                            brief: true
                            entry: parent.entries.length ? parent.entries[0] : ({})
                            selected: root.workspace.selectedEvent.occurrenceKey === entry.occurrenceKey
                            onClicked: root.choose(entry)
                        }
                        LV.LabelButton {
                            visible: parent.entries.length > 1
                            anchors.right: parent.right
                            text: "+" + (parent.entries.length - 1)
                            tone: LV.AbstractButton.Default
                            onClicked: {
                                root.calendar.selectDate(parent.modelData.date);
                                root.workspace.viewMode = "month";
                            }
                        }
                    }
                }
            }
            Controls.ScrollView {
                id: timeScroll
                objectName: "calendarTimeScroll"
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                contentHeight: 24 * timeline.hourHeight
                clip: true
                Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
                Component.onCompleted: Qt.callLater(function () {
                    timeScroll.contentItem.contentY = Math.min(8 * timeline.hourHeight, Math.max(0, timeScroll.contentHeight - timeScroll.height));
                })
                Item {
                    width: timeScroll.availableWidth
                    height: 24 * timeline.hourHeight
                    Repeater {
                        model: root.workspace.periodDays
                        Rectangle {
                            id: timeDay
                            required property var modelData
                            required property int index
                            x: timeline.gutter + index * timeline.dayWidth
                            width: timeline.dayWidth
                            height: parent.height
                            color: modelData.selected ? Qt.rgba(LV.Theme.accentGreen.r, LV.Theme.accentGreen.g, LV.Theme.accentGreen.b, 0.025) : "transparent"
                            border.width: 0.5
                            border.color: LV.Theme.panelBackground08
                            TapHandler {
                                onDoubleTapped: function (eventPoint) {
                                    root.createRequested(timeDay.modelData.date, Math.floor(eventPoint.position.y / timeline.hourHeight * 2) * 30);
                                }
                            }
                        }
                    }
                    Repeater {
                        model: 24
                        Item {
                            id: hourLine
                            required property int index
                            y: index * timeline.hourHeight
                            width: parent.width
                            height: 1
                            LV.Label {
                                width: timeline.gutter - 4
                                text: String(hourLine.index).padStart(2, "0") + ":00"
                                style: caption
                            }
                            Rectangle {
                                x: timeline.gutter
                                width: parent.width - x
                                height: 0.5
                                color: LV.Theme.panelBackground08
                            }
                        }
                    }
                    Repeater {
                        model: timeline.timed
                        CalendarEventCard {
                            id: timedCard
                            required property var modelData
                            objectName: "calendarTimedEvent_" + modelData.id
                            x: timeline.gutter + modelData.dayIndex * timeline.dayWidth + 4 + modelData.lane * (timeline.dayWidth - 8) / modelData.lanes
                            y: modelData.startMinute / 60 * timeline.hourHeight + 2
                            width: Math.max(12, (timeline.dayWidth - 8) / modelData.lanes - 3)
                            height: Math.max(20, (modelData.endMinute - modelData.startMinute) / 60 * timeline.hourHeight - 4)
                            entry: modelData
                            selected: root.workspace.selectedEvent.occurrenceKey === modelData.occurrenceKey
                            onClicked: root.choose(entry)
                            TapHandler {
                                onDoubleTapped: root.choose(timedCard.entry)
                            }
                        }
                    }
                    Rectangle {
                        visible: root.workspace.periodDays.some(d => d.date === root.workspace.todayDate)
                        x: timeline.gutter
                        y: root.workspace.currentMinute / 60 * timeline.hourHeight
                        width: parent.width - x
                        height: 1
                        color: LV.Theme.accentGreen
                        Rectangle {
                            x: -3
                            y: -2
                            width: 6
                            height: 6
                            radius: 3
                            color: LV.Theme.accentGreen
                        }
                    }
                }
            }
        }
    }
    Component {
        id: monthView
        LV.VStack {
            id: month
            alignment: Qt.AlignLeft
            spacing: 0
            Row {
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                Repeater {
                    model: ["MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"]
                    LV.Label {
                        required property string modelData
                        width: month.width / 7
                        text: modelData
                        style: caption
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
            GridLayout {
                id: monthGrid
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 7
                rowSpacing: 0
                columnSpacing: 0
                Repeater {
                    model: root.workspace.periodDays
                    Rectangle {
                        id: cell
                        required property var modelData
                        readonly property var entries: root.dayEvents(modelData.date)
                        readonly property int shown: Math.max(1, Math.min(3, Math.floor((height - 48) / 23)))
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: month.width / 7
                        Layout.preferredHeight: monthGrid.height / 6
                        color: modelData.selected ? Qt.rgba(LV.Theme.accentGreen.r, LV.Theme.accentGreen.g, LV.Theme.accentGreen.b, 0.04) : "transparent"
                        border.width: 0.5
                        border.color: LV.Theme.panelBackground08
                        clip: true
                        LV.VStack {
                            alignment: Qt.AlignLeft
                            anchors.fill: parent
                            anchors.margins: 4
                            spacing: 2
                            LV.LabelButton {
                                objectName: "calendarMonthDate_" + cell.modelData.date
                                implicitWidth: 26
                                implicitHeight: 22
                                horizontalPadding: 0
                                verticalPadding: 0
                                text: cell.modelData.day
                                tone: LV.AbstractButton.Borderless
                                textColor: cell.modelData.selected ? LV.Theme.titleHeaderColor : cell.modelData.inMonth ? LV.Theme.bodyColor : LV.Theme.descriptionColor
                                backgroundColor: cell.modelData.selected ? LV.Theme.accentGreen : "transparent"
                                Accessible.name: cell.modelData.date
                                onClicked: root.calendar.selectDate(cell.modelData.date)
                                TapHandler {
                                    onDoubleTapped: root.createRequested(cell.modelData.date, 540)
                                }
                            }
                            Repeater {
                                model: cell.entries.slice(0, cell.shown)
                                CalendarEventCard {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    brief: true
                                    entry: modelData
                                    selected: root.workspace.selectedEvent.occurrenceKey === modelData.occurrenceKey
                                    onClicked: root.choose(entry)
                                }
                            }
                            LV.LabelButton {
                                visible: cell.entries.length > cell.shown
                                Layout.fillWidth: true
                                text: qsTr("+%1 more").arg(cell.entries.length - cell.shown)
                                tone: LV.AbstractButton.Borderless
                                textColor: LV.Theme.descriptionColor
                                onClicked: {
                                    root.calendar.selectDate(cell.modelData.date);
                                    root.workspace.viewMode = "day";
                                }
                            }
                            Item {
                                Layout.fillHeight: true
                            }
                        }
                    }
                }
            }
        }
    }
    Component {
        id: yearView
        Controls.ScrollView {
            id: year
            contentWidth: availableWidth
            clip: true
            Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
            GridLayout {
                width: year.availableWidth
                columns: width < 480 ? 2 : 3
                columnSpacing: 24
                rowSpacing: 20
                Repeater {
                    model: root.workspace.yearMonths
                    CalendarMiniMonth {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredWidth: 200
                        Layout.preferredHeight: 169
                        compact: true
                        navigation: false
                        title: modelData.title
                        days: modelData.days
                        onDateRequested: function (date) {
                            root.calendar.selectDate(date);
                            root.workspace.viewMode = "day";
                        }
                    }
                }
            }
        }
    }
}
