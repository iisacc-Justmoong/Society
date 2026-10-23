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
    readonly property var event: workspace.selectedEvent
    readonly property bool hasEvent: !!event.id
    signal editRequested(string id)
    signal fileRequested(string path)
    signal closeRequested
    property bool showClose: false
    contentWidth: availableWidth
    clip: true
    Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
    component Detail: LV.VStack {
        id: detail
        alignment: Qt.AlignLeft
        required property string label
        required property string value
        Layout.fillWidth: true
        spacing: 4
        LV.Label {
            text: detail.label
            style: caption
            color: LV.Theme.descriptionColor
        }
        LV.Label {
            Layout.fillWidth: true
            text: detail.value
            textFormat: Text.PlainText
            style: body
            wrapMode: Text.WordWrap
            sizeToContentHeight: true
        }
    }
    LV.VStack {
        alignment: Qt.AlignLeft
        width: root.availableWidth
        spacing: 8
        LV.HStack {
            Layout.fillWidth: true
            LV.Label {
                Layout.fillWidth: true
                text: qsTr("EVENT DETAILS")
                style: caption
            }
            LV.LabelButton {
                objectName: "calendarEventMore"
                visible: root.hasEvent
                text: "…"
                implicitWidth: 22
                tone: LV.AbstractButton.Default
                Accessible.name: qsTr("Event actions")
                onClicked: eventMenu.open()
                Controls.Popup {
                    id: eventMenu
                    x: parent.width - width
                    y: parent.height + 4
                    width: 180
                    padding: 4
                    background: Rectangle {
                        color: LV.Theme.surfaceRaised
                        radius: LV.Theme.radiusMd
                    }
                    contentItem: LV.VStack {
                        alignment: Qt.AlignLeft
                        spacing: 2
                        LV.LabelButton {
                            Layout.fillWidth: true
                            text: qsTr("Edit event")
                            tone: LV.AbstractButton.Default
                            onClicked: {
                                eventMenu.close();
                                root.editRequested(root.event.id);
                            }
                        }
                        LV.LabelButton {
                            Layout.fillWidth: true
                            text: qsTr("Delete event…")
                            tone: LV.AbstractButton.Destructive
                            onClicked: {
                                eventMenu.close();
                                root.editRequested("delete:" + root.event.id);
                            }
                        }
                    }
                }
            }
            LV.LabelButton {
                visible: root.showClose
                text: "×"
                implicitWidth: 22
                tone: LV.AbstractButton.Default
                Accessible.name: qsTr("Close details")
                onClicked: root.closeRequested()
            }
        }
        LV.Label {
            objectName: "calendarSelectedEventTitle"
            Layout.fillWidth: true
            text: root.hasEvent ? root.event.title : qsTr("Select an event")
            textFormat: Text.PlainText
            style: title2
            wrapMode: Text.WordWrap
            sizeToContentHeight: true
        }
        LV.Label {
            Layout.fillWidth: true
            text: root.hasEvent ? "•  " + root.event.calendarName : qsTr("Choose an event in the calendar or add a new one.")
            style: root.hasEvent ? body : description
            wrapMode: Text.WordWrap
            sizeToContentHeight: true
            color: root.hasEvent ? LV.Theme.accentGreen : LV.Theme.descriptionColor
        }
        LV.LabelButton {
            objectName: "calendarJoinCall"
            visible: root.hasEvent && /^https?:\/\//i.test(root.event.meetingUrl || "")
            Layout.fillWidth: true
            text: qsTr("Join call")
            tone: LV.AbstractButton.Primary
            onClicked: Qt.openUrlExternally(root.event.meetingUrl)
        }
        Detail {
            visible: root.hasEvent
            label: qsTr("When")
            value: (root.event.when || "") + "\n" + (root.event.time || "") + " · " + root.calendar.timeZone
        }
        Detail {
            visible: root.hasEvent
            label: qsTr("Location")
            value: root.event.location || qsTr("No location")
        }
        Detail {
            visible: root.hasEvent
            label: qsTr("Organizer")
            value: root.event.organizer || ""
        }
        Repeater {
            model: root.hasEvent ? [
                {
                    label: qsTr("Alert"),
                    value: root.event.alarmMinutes < 0 ? qsTr("None") : root.event.alarmMinutes === 0 ? qsTr("At start") : qsTr("%1 minutes before").arg(root.event.alarmMinutes)
                },
                {
                    label: qsTr("Repeat"),
                    value: root.event.repeat === "none" ? qsTr("Does not repeat") : root.event.repeat
                },
                {
                    label: qsTr("Availability"),
                    value: root.event.busy ? qsTr("Busy") : qsTr("Free")
                }
            ] : []
            LV.HStack {
                id: metadataRow
                required property var modelData
                Layout.fillWidth: true
                spacing: 8
                LV.Label {
                    Layout.preferredWidth: 76
                    text: metadataRow.modelData.label
                    style: caption
                }
                LV.Label {
                    Layout.fillWidth: true
                    text: metadataRow.modelData.value
                    style: body
                    elide: Text.ElideRight
                }
            }
        }
        Rectangle {
            visible: root.hasEvent
            Layout.fillWidth: true
            height: 1
            color: LV.Theme.panelBackground08
        }
        LV.Label {
            visible: root.hasEvent
            text: qsTr("GUESTS · %1").arg((root.event.guests || []).length)
            style: caption
        }
        Repeater {
            model: root.event.guests || []
            LV.HStack {
                id: guestRow
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: 26
                spacing: 8
                Rectangle {
                    width: 24
                    height: 24
                    radius: 12
                    color: LV.Theme.accentGreenMuted
                    LV.Label {
                        anchors.centerIn: parent
                        text: guestRow.modelData.name.split(/\s+/).map(n => n.charAt(0)).join("").slice(0, 2).toUpperCase()
                        style: caption
                        color: LV.Theme.accentGreen
                    }
                }
                LV.Label {
                    Layout.fillWidth: true
                    text: guestRow.modelData.name
                    textFormat: Text.PlainText
                    style: body
                    elide: Text.ElideRight
                }
                LV.Label {
                    text: guestRow.modelData.response
                    style: caption
                }
            }
        }
        Rectangle {
            visible: root.hasEvent
            Layout.fillWidth: true
            height: 1
            color: LV.Theme.panelBackground08
        }
        Detail {
            visible: root.hasEvent
            label: qsTr("Notes")
            value: root.event.description || qsTr("No notes")
        }
        LV.Label {
            visible: (root.event.attachments || []).length > 0
            text: qsTr("LINKED WORK")
            style: caption
        }
        Repeater {
            model: root.event.attachments || []
            LV.LabelButton {
                required property var modelData
                readonly property string path: root.calendar.attachmentPath(modelData.uri)
                Layout.fillWidth: true
                text: modelData.name || qsTr("Linked file")
                tone: LV.AbstractButton.Borderless
                textColor: LV.Theme.bodyColor
                enabled: path.length > 0
                onClicked: root.fileRequested(path)
            }
        }
        LV.LabelButton {
            objectName: "calendarEditEvent"
            visible: root.hasEvent
            Layout.fillWidth: true
            Layout.topMargin: 8
            text: qsTr("Edit event")
            tone: LV.AbstractButton.Default
            onClicked: root.editRequested(root.event.id)
        }
        LV.Label {
            visible: root.hasEvent
            Layout.fillWidth: true
            text: root.event.repeat !== "none" ? qsTr("Edits apply to the entire series") : qsTr("Stored in this container")
            style: caption
            wrapMode: Text.WordWrap
            sizeToContentHeight: true
        }
    }
}
