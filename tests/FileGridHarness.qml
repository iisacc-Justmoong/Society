import QtQuick
import LVRS 1.0 as LV
import "../App" as App

LV.ApplicationWindow {
    title: "Society"
    width: 960
    height: 640
    desktopMinWidth: 360
    desktopMinHeight: 320
    visible: true
    useInternalPageStack: false
    App.FileGridView { objectName: "fileGridView"; anchors.fill: parent; path: "" }
}
