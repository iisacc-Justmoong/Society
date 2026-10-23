pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV

LV.DropdownButton {
    id: root
    required property var options
    property var value
    signal chosen(var value)
    tone: LV.AbstractButton.Default
    text: {
        for (const option of options)
            if (String(option.value) === String(value))
                return option.label;
        return String(value || qsTr("Choose"));
    }
    onClicked: menu.open()
    Controls.Popup {
        id: menu
        y: root.height + 4
        width: Math.max(root.width, 180)
        padding: 4
        closePolicy: Controls.Popup.CloseOnEscape | Controls.Popup.CloseOnPressOutside
        background: Rectangle {
            color: LV.Theme.surfaceRaised
            radius: LV.Theme.radiusMd
        }
        contentItem: LV.VStack {
            alignment: Qt.AlignLeft
            spacing: 2
            Repeater {
                model: root.options
                LV.LabelButton {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.label
                    tone: LV.AbstractButton.Borderless
                    textColor: String(modelData.value) === String(root.value) ? LV.Theme.accentGreen : LV.Theme.bodyColor
                    onClicked: {
                        root.chosen(modelData.value);
                        menu.close();
                    }
                }
            }
        }
    }
}
