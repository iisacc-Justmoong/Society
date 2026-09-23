pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV

Item {
    id: root
    required property var appWindow
    property bool backEnabled: false
    signal backRequested()

    Component.onCompleted: {
        LV.RuntimeEvents.start()
        LV.RuntimeEvents.attachWindow(appWindow)
        LV.GestureEvents.attachRuntime(LV.RuntimeEvents)
    }
    function dismissKeyboard() {
        // Qt 6.8 exposes QInputMethod as QObject to qmllint.
        // qmllint disable missing-property
        Qt.inputMethod.hide()
        // qmllint enable missing-property
    }
    Connections {
        target: LV.GestureEvents
        function onDragEnded(eventData) {
            if (!root.enabled || !root.appWindow.active || !root.backEnabled || eventData.maximumFingerCount !== 1) return
            const content = root.appWindow.contentItem
            const start = content.mapFromGlobal(eventData.startGlobalX, eventData.startGlobalY)
            if (start.x >= 0 && start.x <= 28 && start.y >= 0 && start.y < content.height
                && eventData.totalDeltaX >= 72 && Math.abs(eventData.totalDeltaY) < eventData.totalDeltaX * 0.6)
                root.backRequested()
        }
        function onScrollStarted(eventData) {
            if (root.enabled && root.appWindow.active) root.dismissKeyboard()
        }
        function onTouchStarted(eventData) {
            if (!root.enabled || !root.appWindow.active || !root.appWindow.activeFocusItem) return
            const field = root.appWindow.activeFocusItem
            const point = field.mapFromGlobal(eventData.globalX, eventData.globalY)
            if (point.x < 0 || point.y < 0 || point.x > field.width || point.y > field.height)
                root.dismissKeyboard()
        }
    }
}
