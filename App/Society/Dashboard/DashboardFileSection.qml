pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV

LV.VStack {
    id: root
    required property string title
    required property string sectionKey
    required property string objectSuffix
    required property var rows
    property bool loading: false
    property string query: ""
    property bool menuVisible: false
    signal viewAllRequested(string key)
    signal fileRequested(string path)
    signal menuRequested(var file, var card)
    spacing: 12
    LV.HStack {
        Layout.fillWidth: true
        LV.Label {
            objectName: "dashboard" + root.objectSuffix + "Title"
            Layout.fillWidth: true; text: root.title; style: body; elide: Text.ElideRight
        }
        LV.LabelButton {
            objectName: "viewAll" + root.objectSuffix
            text: qsTr("View all files"); tone: LV.AbstractButton.Borderless
            onClicked: root.viewAllRequested(root.sectionKey)
        }
    }
    ListView {
        id: cards
        objectName: "dashboard" + root.objectSuffix + "Cards"
        Layout.fillWidth: true
        implicitHeight: LV.Theme.scaleMetric(160)
        visible: count > 0
        model: root.rows
        orientation: ListView.Horizontal
        spacing: LV.Theme.gap8
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: true
        keyNavigationEnabled: true
        Keys.onReturnPressed: openCurrentFile()
        Keys.onEnterPressed: openCurrentFile()
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Home) { currentIndex = 0; positionViewAtBeginning(); event.accepted = true }
            else if (event.key === Qt.Key_End) { currentIndex = count - 1; positionViewAtEnd(); event.accepted = true }
        }
        function openCurrentFile() {
            if (currentIndex >= 0 && currentIndex < count) root.fileRequested(root.rows[currentIndex].path)
        }
        Controls.ScrollBar.horizontal: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
        delegate: LV.Card {
            id: card
            required property int index
            required property var modelData
            objectName: "dashboard" + root.objectSuffix + "Card" + index
            type: LV.Card.File; size: LV.Card.Small; detail: LV.Card.Brief
            background: Rectangle { color: "transparent"; radius: card.resolvedCornerRadius }
            width: LV.Theme.scaleMetric(140); height: LV.Theme.scaleMetric(160)
            filename: modelData.name
            description: modelData.description || ""
            metadata: modelData.dateText
            previewSource: modelData.thumbnailSource || modelData.previewSource || ""
            selectable: false
            showMenu: hovered || visualFocus
            onClicked: { if (!root.menuVisible) { cards.currentIndex = index; root.fileRequested(modelData.path) } }
            onActiveFocusChanged: if (activeFocus) { cards.currentIndex = index; cards.positionViewAtIndex(index, ListView.Contain) }
            onMenuRequested: root.menuRequested(modelData, card)
            TapHandler { acceptedDevices: PointerDevice.TouchScreen; onLongPressed: root.menuRequested(card.modelData, card) }
            TapHandler { acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad; acceptedButtons: Qt.RightButton; onTapped: root.menuRequested(card.modelData, card) }
        }
    }
    LV.ListItem {
        objectName: "empty" + root.objectSuffix
        visible: root.rows.length === 0
        Layout.fillWidth: true
        type: LV.ListItem.Detail; showLeadingIcon: false
        label: root.loading ? qsTr("Reading Society…") : root.query.trim().length > 0
            ? qsTr("No matching files") : root.sectionKey === "files" ? qsTr("No recent files") : qsTr("No generated images yet")
        detail: root.query.trim().length > 0 ? qsTr("Try another file name.") : qsTr("Files saved in Society will appear here.")
        showBookmark: false; showDate: false; showFolders: false; showTags: false
    }
}
