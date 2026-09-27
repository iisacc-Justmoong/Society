pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV

LV.Card {
    id: root
    required property var entry
    property bool deviceCard: false
    signal detailsRequested()
    signal primaryRequested()
    type: deviceCard ? LV.Card.Device : LV.Card.Model
    title: entry.name
    description: entry.description || ""
    selectable: false
    showMenu: false
    showAction: false
    implicitHeight: 286
    cornerRadius: 12
    background: Rectangle { color: LV.Theme.panelBackground04; border.color: root.visualFocus ? LV.Theme.accentGreen : LV.Theme.panelBackground10; radius: 12 }
    onClicked: detailsRequested()
    contentItem: Item {
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                EnvironmentBadge { symbol: root.entry.symbol || root.entry.name.slice(0, 2); paletteIndex: root.deviceCard ? -1 : (root.entry.palette || 0) }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 4
                    LV.Label { Layout.fillWidth: true; text: root.entry.name; style: header2; textFormat: Text.PlainText; elide: Text.ElideRight }
                    LV.Label { Layout.fillWidth: true; text: root.entry.publisher || "iisacc"; style: caption; textFormat: Text.PlainText; elide: Text.ElideRight }
                }
            }
            LV.Label { Layout.fillWidth: true; text: root.entry.description || ""; style: description; wrapMode: Text.Wrap; sizeToContentHeight: true; textFormat: Text.PlainText }
            Item { Layout.fillHeight: true }
            LV.Label { Layout.fillWidth: true; text: root.entry.status || ""; style: caption; textFormat: Text.PlainText; elide: Text.ElideRight }
            LV.Label { Layout.fillWidth: true; text: root.entry.license || ""; style: caption; textFormat: Text.PlainText; elide: Text.ElideRight }
            RowLayout {
                Layout.fillWidth: true
                EnvironmentButton { objectName: root.objectName + "Details"; implicitHeight: 28; text: qsTr("Details"); tone: LV.AbstractButton.Borderless; onClicked: root.detailsRequested() }
                Item { Layout.fillWidth: true }
                EnvironmentButton {
                    objectName: root.objectName + "Action"
                    implicitHeight: 32
                    text: root.entry.action || qsTr("Open")
                    tone: LV.AbstractButton.Primary
                    enabled: root.entry.actionEnabled !== false
                    onClicked: root.primaryRequested()
                }
            }
        }
    }
}
