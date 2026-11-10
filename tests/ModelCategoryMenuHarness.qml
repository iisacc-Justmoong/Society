import QtQuick
import QtQuick.Controls
import "../App/Society/Files"

ApplicationWindow {
    width: 700
    height: 360
    visible: true
    FileActionMenu {
        objectName: "menuUnderTest"
        filePath: "/example/model.safetensors"
        folderPath: "/example"
    }
}
