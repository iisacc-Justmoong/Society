import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV

Rectangle {
    id: root
    property string title: ""
    default property alias contents: content.data
    color: LV.Theme.panelBackground04
    border.color: LV.Theme.panelBackground10
    radius: 10
    implicitHeight: content.implicitHeight + 24
    Layout.fillWidth: true
    ColumnLayout {
        id: content
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
        spacing: 4
        LV.Label { Layout.fillWidth: true; visible: root.title.length > 0; text: root.title; style: header2 }
    }
}
