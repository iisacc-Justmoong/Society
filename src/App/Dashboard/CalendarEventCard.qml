pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV

LV.AbstractButton {
    id: root
    required property var entry
    property bool selected: false
    property bool brief: false
    readonly property color eventColor: entry.colorKey === "purple" ? LV.Theme.accentPurple : entry.colorKey === "blue" ? LV.Theme.accentBlue : entry.colorKey === "orange" ? LV.Theme.accentOrange : LV.Theme.accentGreen
    implicitHeight: brief ? 22 : 44
    implicitWidth: 80
    horizontalPadding: 6
    verticalPadding: 4
    tone: LV.AbstractButton.Borderless
    Accessible.name: (entry.title || "") + ", " + (entry.time || "")
    background: Rectangle {
        radius: LV.Theme.radiusSm
        color: Qt.rgba(root.eventColor.r, root.eventColor.g, root.eventColor.b, root.down ? 0.3 : 0.2)
        border.width: root.selected || root.visualFocus ? 1 : 0
        border.color: root.eventColor
    }
    contentItem: Item {
        clip: true
        LV.VStack {
            alignment: Qt.AlignLeft
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: implicitHeight
            spacing: 3
            LV.Label {
                Layout.fillWidth: true
                text: (root.brief && !root.entry.allDay ? String(root.entry.time || "").split("–")[0] + " " : "") + (root.entry.title || "")
                textFormat: Text.PlainText
                style: root.brief ? caption : body
                color: root.eventColor
                elide: Text.ElideRight
            }
            LV.Label {
                visible: !root.brief && root.height >= 40
                Layout.fillWidth: true
                text: root.entry.time || ""
                style: caption
                color: LV.Theme.descriptionColor
                elide: Text.ElideRight
            }
        }
    }
}
