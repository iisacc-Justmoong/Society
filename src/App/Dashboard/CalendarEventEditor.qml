pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV
import Society

Controls.Popup {
    id: root
    objectName: "calendarEventEditor"
    required property CalendarWorkspace workspace
    property var draft: ({})
    property bool deleting: false
    onDraftChanged: {
        if (notes && notes.text !== (draft.description || ""))
            notes.text = draft.description || "";
    }
    readonly property bool editing: !!draft.id
    readonly property bool narrow: width < 480
    function change(key, value) {
        const copy = Object.assign({}, draft);
        copy[key] = value;
        draft = copy;
    }
    function begin(id, date, minute) {
        deleting = id.indexOf("delete:") === 0;
        const eventId = deleting ? id.slice(7) : id === "task:new" ? "" : id;
        draft = workspace.eventDraft(eventId);
        if (!draft.startLocal)
            return;
        if (id === "task:new")
            change("kind", "task");
        if (!eventId && date) {
            const hour = Math.min(23, Math.floor(minute / 60)), part = minute % 60;
            change("startLocal", date + " " + String(hour).padStart(2, "0") + ":" + String(part).padStart(2, "0"));
            change("endLocal", date + " " + String(Math.min(23, hour + 1)).padStart(2, "0") + ":" + (hour === 23 ? "59" : String(part).padStart(2, "0")));
        }
        quick.text = "";
        open();
        Qt.callLater(function () {
            titleField.forceActiveFocus();
        });
    }
    function save() {
        if (deleting) {
            if (workspace.removeEvent(draft.id, String(draft.revision)))
                close();
        } else if (workspace.saveEvent(draft))
            close();
    }
    parent: Controls.Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    width: Math.min(600, parent ? parent.width - 24 : 600)
    height: Math.min(610, parent ? parent.height - 32 : 610)
    padding: 24
    modal: true
    focus: true
    closePolicy: Controls.Popup.CloseOnEscape
    Controls.Overlay.modal: Rectangle {
        color: LV.Theme.overlayBackdrop
    }
    background: Rectangle {
        color: LV.Theme.surfacePanel
        radius: LV.Theme.radiusLg
        border.width: 0.5
        border.color: LV.Theme.panelBackground12
    }
    contentItem: LV.VStack {
        alignment: Qt.AlignLeft
        spacing: 12
        LV.HStack {
            Layout.fillWidth: true
            spacing: 8
            LV.Label {
                Layout.fillWidth: true
                text: root.deleting ? qsTr("Delete event?") : root.editing ? qsTr("Edit event") : root.draft.kind === "task" ? qsTr("New task") : qsTr("New event")
                style: title2
            }
            LV.LabelSegmentedControl {
                visible: !root.deleting
                implicitWidth: 136
                LV.LabelButton {
                    width: 63
                    text: qsTr("Event")
                    backgroundColor: root.draft.kind !== "task" ? LV.Theme.accentGreenMuted : "transparent"
                    textColor: root.draft.kind !== "task" ? LV.Theme.accentGreen : LV.Theme.descriptionColor
                    onClicked: root.change("kind", "event")
                }
                LV.LabelButton {
                    width: 63
                    text: qsTr("Task")
                    backgroundColor: root.draft.kind === "task" ? LV.Theme.accentGreenMuted : "transparent"
                    textColor: root.draft.kind === "task" ? LV.Theme.accentGreen : LV.Theme.descriptionColor
                    onClicked: root.change("kind", "task")
                }
            }
        }
        Controls.ScrollView {
            id: formScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
            LV.VStack {
                alignment: Qt.AlignLeft
                width: formScroll.availableWidth
                spacing: 10
                enabled: !root.deleting
                LV.Label {
                    text: qsTr("Describe your event")
                    style: description
                }
                LV.HStack {
                    visible: !root.editing
                    Layout.fillWidth: true
                    spacing: 6
                    LV.InputField {
                        id: quick
                        objectName: "calendarQuickDescription"
                        Layout.fillWidth: true
                        placeholderText: qsTr("Review Fri 2–3pm /Personal")
                        onAccepted: parseQuick.clicked()
                    }
                    LV.LabelButton {
                        id: parseQuick
                        text: qsTr("Apply")
                        tone: LV.AbstractButton.Default
                        onClicked: {
                            const parsed = root.workspace.quickDraft(quick.text);
                            if (parsed.startLocal)
                                root.draft = Object.assign({}, parsed, {
                                    kind: root.draft.kind
                                });
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: 8
                    Repeater {
                        model: [String(root.draft.startLocal || "").slice(0, 10), root.draft.allDay ? qsTr("All day") : String(root.draft.startLocal || "").slice(11) + "–" + String(root.draft.endLocal || "").slice(11), root.draft.calendarId || ""]
                        LV.LabelButton {
                            required property string modelData
                            text: modelData
                            enabled: false
                            textColorDisabled: LV.Theme.accentGreen
                            backgroundColorDisabled: LV.Theme.accentGreenMuted
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: LV.Theme.panelBackground08
                }
                LV.Label {
                    text: qsTr("Title")
                    style: description
                }
                LV.InputField {
                    id: titleField
                    objectName: "calendarEventTitleInput"
                    Layout.fillWidth: true
                    text: root.draft.title || ""
                    placeholderText: qsTr("Event title")
                    onTextEdited: function (value) {
                        root.change("title", value);
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.narrow ? 1 : 2
                    rowSpacing: 8
                    columnSpacing: 16
                    Repeater {
                        model: [
                            {
                                key: "startLocal",
                                label: qsTr("Starts")
                            },
                            {
                                key: "endLocal",
                                label: qsTr("Ends")
                            }
                        ]
                        LV.VStack {
                            id: dateField
                            alignment: Qt.AlignLeft
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredWidth: 240
                            spacing: 4
                            LV.Label {
                                text: dateField.modelData.label
                                style: description
                            }
                            LV.InputField {
                                objectName: "calendarEvent_" + dateField.modelData.key
                                Layout.fillWidth: true
                                text: root.draft[dateField.modelData.key] || ""
                                placeholderText: root.draft.allDay ? "yyyy-mm-dd" : "yyyy-mm-dd hh:mm"
                                onTextEdited: function (value) {
                                    root.change(dateField.modelData.key, value);
                                }
                            }
                        }
                    }
                }
                LV.HStack {
                    Layout.fillWidth: true
                    spacing: 12
                    LV.InputField {
                        objectName: "calendarEventTimeZone"
                        Layout.fillWidth: true
                        text: root.draft.timeZone || "UTC"
                        placeholderText: qsTr("IANA time zone")
                        onTextEdited: function (value) {
                            root.change("timeZone", value);
                        }
                    }
                    LV.CheckBox {
                        objectName: "calendarAllDay"
                        text: qsTr("All-day")
                        checked: root.draft.allDay === true
                        onClicked: root.change("allDay", checked)
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.narrow ? 1 : 2
                    rowSpacing: 8
                    columnSpacing: 16
                    LV.VStack {
                        alignment: Qt.AlignLeft
                        Layout.fillWidth: true
                        spacing: 4
                        LV.Label {
                            text: qsTr("Calendar")
                            style: description
                        }
                        CalendarChoice {
                            objectName: "calendarEventCollection"
                            Layout.fillWidth: true
                            options: root.workspace.calendars.map(c => ({
                                        value: c.id,
                                        label: c.name
                                    }))
                            value: root.draft.calendarId
                            onChosen: function (value) {
                                root.change("calendarId", value);
                            }
                        }
                    }
                    LV.VStack {
                        alignment: Qt.AlignLeft
                        Layout.fillWidth: true
                        spacing: 4
                        LV.Label {
                            text: qsTr("Availability")
                            style: description
                        }
                        CalendarChoice {
                            Layout.fillWidth: true
                            options: [
                                {
                                    value: true,
                                    label: qsTr("Busy")
                                },
                                {
                                    value: false,
                                    label: qsTr("Free")
                                }
                            ]
                            value: root.draft.busy
                            onChosen: function (value) {
                                root.change("busy", value);
                            }
                        }
                    }
                }
                LV.Label {
                    text: qsTr("Location")
                    style: description
                }
                LV.InputField {
                    Layout.fillWidth: true
                    text: root.draft.location || ""
                    placeholderText: qsTr("Place or room")
                    onTextEdited: function (value) {
                        root.change("location", value);
                    }
                }
                LV.InputField {
                    Layout.fillWidth: true
                    text: root.draft.meetingUrl || ""
                    placeholderText: qsTr("Video call URL (optional)")
                    onTextEdited: function (value) {
                        root.change("meetingUrl", value);
                    }
                }
                LV.Label {
                    text: qsTr("Invitees")
                    style: description
                }
                LV.InputField {
                    Layout.fillWidth: true
                    text: root.draft.invitees || ""
                    placeholderText: qsTr("Names or emails, separated by semicolons")
                    onTextEdited: function (value) {
                        root.change("invitees", value);
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.narrow ? 1 : 2
                    rowSpacing: 8
                    columnSpacing: 16
                    LV.VStack {
                        alignment: Qt.AlignLeft
                        Layout.fillWidth: true
                        spacing: 4
                        LV.Label {
                            text: qsTr("Alert")
                            style: description
                        }
                        CalendarChoice {
                            Layout.fillWidth: true
                            options: [
                                {
                                    value: -1,
                                    label: qsTr("None")
                                },
                                {
                                    value: 0,
                                    label: qsTr("At start")
                                },
                                {
                                    value: 5,
                                    label: qsTr("5 minutes before")
                                },
                                {
                                    value: 10,
                                    label: qsTr("10 minutes before")
                                },
                                {
                                    value: 15,
                                    label: qsTr("15 minutes before")
                                },
                                {
                                    value: 30,
                                    label: qsTr("30 minutes before")
                                },
                                {
                                    value: 60,
                                    label: qsTr("1 hour before")
                                },
                                {
                                    value: 1440,
                                    label: qsTr("1 day before")
                                }
                            ]
                            value: root.draft.alarmMinutes
                            onChosen: function (value) {
                                root.change("alarmMinutes", value);
                            }
                        }
                    }
                    LV.VStack {
                        alignment: Qt.AlignLeft
                        Layout.fillWidth: true
                        spacing: 4
                        LV.Label {
                            text: qsTr("Repeat")
                            style: description
                        }
                        CalendarChoice {
                            Layout.fillWidth: true
                            options: [
                                {
                                    value: "none",
                                    label: qsTr("Does not repeat")
                                },
                                {
                                    value: "daily",
                                    label: qsTr("Every day")
                                },
                                {
                                    value: "weekly",
                                    label: qsTr("Every week")
                                },
                                {
                                    value: "monthly",
                                    label: qsTr("Every month")
                                },
                                {
                                    value: "yearly",
                                    label: qsTr("Every year")
                                }
                            ]
                            value: root.draft.repeat
                            onChosen: function (value) {
                                root.change("repeat", value);
                            }
                        }
                    }
                }
                LV.Label {
                    text: qsTr("Notes")
                    style: description
                }
                LV.TextEditor {
                    id: notes
                    objectName: "calendarEventNotes"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    filePath: ""
                    fieldMinHeight: 64
                    editorHeight: 64
                    Component.onCompleted: editorItem.textFormat = TextEdit.PlainText
                    placeholderText: qsTr("Add notes for this event")
                    onTextEdited: function (value) {
                        if (value !== (root.draft.description || ""))
                            root.change("description", value);
                    }
                }
            }
        }
        LV.Label {
            Layout.fillWidth: true
            text: root.workspace.errorString || (root.deleting ? qsTr("This deletes the saved event%1.").arg(root.draft.repeat !== "none" ? qsTr(" and its entire repeating series") : "") : root.editing && root.draft.repeat !== "none" ? qsTr("Changes apply to the entire series.") : qsTr("Saved locally. Invitees are recorded; invitations are not sent."))
            textFormat: Text.PlainText
            style: caption
            color: root.workspace.errorString ? LV.Theme.accentOrange : LV.Theme.descriptionColor
            wrapMode: Text.WordWrap
            sizeToContentHeight: true
        }
        LV.HStack {
            Layout.fillWidth: true
            spacing: 8
            LV.LabelButton {
                visible: root.editing && !root.deleting
                text: qsTr("Delete…")
                tone: LV.AbstractButton.Destructive
                onClicked: root.deleting = true
            }
            Item {
                Layout.fillWidth: true
            }
            LV.LabelButton {
                objectName: "calendarCancelEvent"
                text: qsTr("Cancel")
                tone: LV.AbstractButton.Default
                onClicked: root.close()
            }
            LV.LabelButton {
                objectName: "calendarSaveEvent"
                text: root.deleting ? qsTr("Delete event") : root.editing ? qsTr("Save changes") : root.draft.kind === "task" ? qsTr("Add task") : qsTr("Add event")
                tone: root.deleting ? LV.AbstractButton.Destructive : LV.AbstractButton.Primary
                enabled: !!root.draft.title && root.draft.title.trim().length > 0
                onClicked: root.save()
            }
        }
    }
}
