import QtQuick
import LVRS 1.0 as LV

LV.ApplicationWindow {
    id: root
    objectName: "societyWindow"

    title: "Society"
    width: 640
    height: 400
    desktopMinWidth: 320
    desktopMinHeight: 240
    visible: true
    solidChrome: false
    useInternalPageStack: false

    LV.Label {
        objectName: "greeting"
        anchors.centerIn: parent
        style: title
        text: qsTr("Hello world!")
    }
}
