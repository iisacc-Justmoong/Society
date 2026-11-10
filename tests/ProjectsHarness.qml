import QtQuick
import LVRS 1.0 as LV
import Society 1.0
import "../App/Society/Dashboard"

LV.ApplicationWindow {
    width: 1440; height: 900
    desktopMinWidth: 360; desktopMinHeight: 320
    visible: true
    useInternalPageStack: false
    primaryColor: LV.Theme.accentGreen
    content: ProjectsView {
        anchors.fill: parent
        anchors.topMargin: 56
        workspace: ProjectWorkspace { objectName: "testProjectWorkspace"; active: true }
    }
}
