import QtQuick
import LVRS 1.0 as LV
import "../App/Tools"

LV.ApplicationWindow {
    width: 1440
    height: 1000
    desktopMinWidth: 360
    desktopMinHeight: 320
    visible: true
    useInternalPageStack: false
    primaryColor: LV.Theme.accentGreen
    content: ModelMergeTool {
        anchors.fill: parent
        anchors.topMargin: 32
    }
}
