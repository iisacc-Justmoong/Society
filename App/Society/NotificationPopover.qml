pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV

LV.Popover {
    id: root
    objectName: "notificationPopover"
    required property var notifications
    property Item anchorItem: null
    property bool touchNavigation: false

    primaryColor: LV.Theme.accentGreen
    width: Math.min(LV.Theme.scaleMetric(420), Math.max(0, parent.width - 2 * margins))
    height: Math.min(LV.Theme.scaleMetric(480), Math.max(0, parent.height - 2 * margins))

    function updatePosition() {
        if (!anchorItem || !parent) return
        const point = anchorItem.mapToItem(parent, 0, anchorItem.height)
        x = Math.max(margins, Math.min(parent.width - width - margins,
                                      point.x + anchorItem.width - width))
        y = Math.max(margins, Math.min(parent.height - height - margins,
                                      point.y + LV.Theme.gap8))
    }
    function openFor(item) {
        anchorItem = item
        updatePosition()
        open()
    }
    onWidthChanged: updatePosition()
    onHeightChanged: updatePosition()

    contentItem: LV.VStack {
        spacing: LV.Theme.gap12
        LV.HStack {
            Layout.fillWidth: true
            spacing: LV.Theme.gap8
            LV.Label {
                Layout.fillWidth: true
                style: header
                text: qsTr("Notifications")
            }
            LV.LabelButton {
                objectName: "clearNotifications"
                text: qsTr("Clear all")
                visible: root.notifications && root.notifications.count > 0
                onClicked: root.notifications.clear()
            }
        }
        LV.Label {
            objectName: "notificationEmptyState"
            Layout.fillWidth: true
            visible: !root.notifications || root.notifications.count === 0
            text: qsTr("No new notifications.")
            style: description
        }
        ListView {
            objectName: "notificationList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.notifications && root.notifications.count > 0
            model: root.notifications ? root.notifications.entries : null
            clip: true
            spacing: LV.Theme.gap12
            boundsBehavior: Flickable.StopAtBounds
            Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
            delegate: LV.VStack {
                id: notificationRow
                required property int index
                required property string source
                required property string message
                objectName: "notificationRow" + index
                width: ListView.view.width
                spacing: LV.Theme.gap4
                LV.HStack {
                    Layout.fillWidth: true
                    spacing: LV.Theme.gap8
                    LV.Label {
                        objectName: "notificationSource" + notificationRow.index
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        style: caption
                        text: notificationRow.source
                        textFormat: Text.PlainText
                    }
                    LV.IconButton {
                        objectName: "notificationDismiss" + notificationRow.index
                        iconName: "generalclose"
                        tone: LV.AbstractButton.Borderless
                        Layout.minimumWidth: root.touchNavigation ? LV.Theme.mobileNavigationTouchSize : 0
                        Layout.minimumHeight: root.touchNavigation ? LV.Theme.mobileNavigationTouchSize : 0
                        Accessible.name: qsTr("Dismiss notification: %1").arg(notificationRow.message)
                        onClicked: root.notifications.dismiss(notificationRow.index)
                    }
                }
                LV.Label {
                    objectName: "notificationMessage" + notificationRow.index
                    Layout.fillWidth: true
                    style: description
                    text: notificationRow.message
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    sizeToContentHeight: true
                    Accessible.name: text
                }
            }
        }
    }
}
