pragma ComponentBehavior: Bound

import QtQuick
import LVRS 1.0 as LV
import Society

LV.ApplicationWindow {
    id: preferences
    objectName: "preferencesWindow"
    required property NetworkDriveController network
    signal devicesRequested()

    title: qsTr("Preferences — Society")
    primaryColor: LV.Theme.accentGreen
    useInternalPageStack: false
    navigationEnabled: false
    width: 560
    height: 440
    desktopMinWidth: 360
    desktopMinHeight: 320
    visible: false
    modality: Qt.NonModal
    flags: Qt.Dialog
    solidChrome: false
    property bool initialPositionSet: false

    function open() {
        if (!network.hostModeAvailable)
            return
        if (!initialPositionSet && transientParent) {
            x = Math.round(transientParent.x + (transientParent.width - width) / 2)
            y = Math.round(transientParent.y + (transientParent.height - height) / 2)
            initialPositionSet = true
        }
        showNormal()
        raise()
        requestActivate()
    }

    Shortcut { sequence: "Escape"; enabled: preferences.visible; onActivated: preferences.close() }
    Shortcut { sequences: [StandardKey.Close]; enabled: preferences.visible; onActivated: preferences.close() }
    PreferencesContent {
        anchors.fill: parent
        anchors.margins: LV.Theme.gap20
        network: preferences.network
        onDevicesRequested: preferences.devicesRequested()
        onDoneRequested: preferences.close()
    }
}
