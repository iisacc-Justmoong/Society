pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV

LV.Card {
    id: root
    required property var tool
    readonly property color accent: tool.tint
    type: LV.Card.Model
    title: tool.title
    description: tool.description
    selectable: false
    showMenu: false
    showAction: false
    implicitHeight: 172
    objectName: "toolCard-" + tool.key
    Accessible.name: tool.title
    Accessible.description: tool.description
    contentItem: ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Rectangle {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                radius: 10
                color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.15)
                LV.Label {
                    anchors.centerIn: parent
                    text: root.tool.symbol
                    color: root.accent
                    style: header
                }
            }
            Item { Layout.fillWidth: true }
            LV.Label { text: root.tool.category; style: description; color: LV.Theme.descriptionColor }
            LV.Label { text: "↗"; style: header; color: LV.Theme.descriptionColor }
        }
        LV.Label {
            Layout.fillWidth: true
            text: root.tool.title
            style: header
            textFormat: Text.PlainText
            elide: Text.ElideRight
        }
        LV.Label {
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: root.tool.description
            style: description
            color: LV.Theme.descriptionColor
            wrapMode: Text.Wrap
            maximumLineCount: 2
            textFormat: Text.PlainText
        }
    }
}
