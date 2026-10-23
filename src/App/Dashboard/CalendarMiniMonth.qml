pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV

LV.VStack {
    id: root
    alignment: Qt.AlignLeft
    required property var days
    required property string title
    property bool compact: false
    property bool navigation: true
    signal dateRequested(string date)
    signal monthRequested(int delta)
    spacing: compact ? 2 : 4
    implicitHeight: compact ? 169 : 232
    LV.HStack {
        Layout.fillWidth: true
        Layout.preferredHeight: root.compact ? 22 : 28
        spacing: 4
        LV.Label {
            Layout.fillWidth: true
            text: root.title
            style: root.compact ? body : header
            elide: Text.ElideRight
        }
        LV.IconButton {
            visible: root.navigation
            iconName: "generalchevronLeft"
            tone: LV.AbstractButton.Default
            implicitWidth: 22
            Accessible.name: qsTr("Previous month")
            onClicked: root.monthRequested(-1)
        }
        LV.IconButton {
            visible: root.navigation
            iconName: "generalchevronRight"
            tone: LV.AbstractButton.Default
            implicitWidth: 22
            Accessible.name: qsTr("Next month")
            onClicked: root.monthRequested(1)
        }
    }
    GridLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        columns: 7
        rowSpacing: root.compact ? 0 : 2
        columnSpacing: 0
        Repeater {
            model: ["M", "T", "W", "T", "F", "S", "S"]
            LV.Label {
                required property string modelData
                Layout.fillWidth: true
                Layout.preferredHeight: root.compact ? 16 : 22
                text: modelData
                style: caption
                color: LV.Theme.descriptionColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        Repeater {
            model: root.days
            LV.LabelButton {
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: root.compact ? 18 : 24
                implicitWidth: 18
                horizontalPadding: 0
                verticalPadding: 0
                text: modelData.day
                tone: LV.AbstractButton.Borderless
                backgroundColor: modelData.selected ? LV.Theme.accentGreen : "transparent"
                textColor: modelData.selected ? LV.Theme.titleHeaderColor : modelData.today ? LV.Theme.accentGreen : modelData.inMonth ? LV.Theme.bodyColor : LV.Theme.descriptionColor
                Accessible.name: modelData.date
                onClicked: root.dateRequested(modelData.date)
            }
        }
    }
}
