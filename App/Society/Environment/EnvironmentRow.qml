pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV

LV.ListItem {
    id: root
    type: LV.ListItem.Navigation
    Layout.fillWidth: true
    standardItemHeight: 52
    description: ""
    detail: ""
    value: ""
    showDescription: description.length > 0
    showValue: value.length > 0
    showTrailingIcon: false
    showLeadingIcon: iconSource.toString().length > 0
    iconName: ""
    leadingComponent: Component {
        Item {
            property var listItem: null
            implicitWidth: 18
            implicitHeight: 18
            Image {
                width: root.iconSource.toString().endsWith("device.svg") ? 16 : 18
                height: width
                source: root.iconSource
                sourceSize.width: width * Screen.devicePixelRatio
                sourceSize.height: height * Screen.devicePixelRatio
            }
        }
    }
    iconSize: 18
    activeFocusOnTab: true
    Accessible.name: label
}
