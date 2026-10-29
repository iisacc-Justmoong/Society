import QtQuick
import LVRS 1.0 as LV

Rectangle {
    id: root
    property string symbol: ""
    property int paletteIndex: 0
    readonly property var backgrounds: ["#10382c", "#292050", "#123550", "#4c2514", "#40203b", "#163a40"]
    readonly property var foregrounds: ["#9ce1bd", "#c7b1ff", "#84d3ff", "#ffb378", "#eda8d9", "#8bd9de"]
    implicitWidth: 64
    implicitHeight: 64
    radius: 14.08
    color: paletteIndex < 0 ? LV.Theme.panelBackground10 : backgrounds[paletteIndex % 6]
    LV.Label { anchors.centerIn: parent; text: root.symbol; style: title; color: root.paletteIndex < 0 ? LV.Theme.titleHeaderColor : root.foregrounds[root.paletteIndex % 6] }
}
