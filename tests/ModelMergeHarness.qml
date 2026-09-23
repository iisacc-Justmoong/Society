import QtQuick
import LVRS 1.0 as LV
import "../src/App/Tools"

LV.ApplicationWindow {
    width: 1440
    height: 1000
    desktopMinWidth: 360
    desktopMinHeight: 320
    visible: true
    useInternalPageStack: false
    primaryColor: LV.Theme.accentGreen
    color: LV.Theme.panelBackground03
    content: ModelMergeTool {
        anchors.fill: parent
        anchors.topMargin: 32
    }
}
