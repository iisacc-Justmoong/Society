pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import LVRS 1.0 as LV
import Society

Controls.ScrollView {
    id: root
    objectName: "projectInspector"
    required property ProjectWorkspace workspace
    property var draft: ({})
    property var drafts: ({})
    property string draftKey: ""
    property bool saving: false
    readonly property bool dirty: !!draft._dirty
    readonly property var types: (workspace.schema.entities || []).map(function(t) { return {value: t.id, label: t.label || t.id}; })
    readonly property var relationTypes: (workspace.schema.relations || []).map(function(t) { return {value: t.id, label: t.label || t.id}; })
    readonly property var nodes: [{value: workspace.project.goalId || "", label: qsTr("Project goal")}]
        .concat(workspace.entities.map(function(e) { return {value: e.id, label: e.name}; }))
        .concat(workspace.resources.map(function(e) { return {value: e.id, label: e.name}; }))
    readonly property var covers: [{value: "", label: qsTr("No cover")}].concat(workspace.resources.filter(function(r) {
        return String(r.mediaType).indexOf("image/") === 0;
    }).map(function(r) { return {value: r.id, label: r.name}; }))
    function syncDraft() {
        const p = workspace.project;
        const key = p.key || "";
        if (saving && !workspace.busy) {
            saving = false;
            if (!workspace.errorString.length) { const cache = Object.assign({}, drafts); delete cache[draftKey]; drafts = cache; draftKey = ""; }
        }
        if (key !== draftKey) {
            draftKey = key;
            draft = drafts[key] || (key ? draftFromProject(p) : {});
        } else if (key && !dirty && draft._expectedRevision !== p.revision) {
            draft = draftFromProject(p);
        }
    }
    function draftFromProject(p) {
        return Object.assign({}, p, {_expectedRevision: p.revision, _dirty: false,
            requirementsText: listText(p.requirements), constraintsText: listText(p.constraints),
            completionText: listText(p.completionConditions)});
    }
    function change(key, value) {
        const copy = Object.assign({}, draft); copy[key] = value; copy._dirty = true; draft = copy;
        const cache = Object.assign({}, drafts); cache[draftKey] = copy; drafts = cache;
    }
    function discard() {
        const cache = Object.assign({}, drafts); delete cache[draftKey]; drafts = cache;
        draftKey = ""; syncDraft();
    }
    function listText(value) { return (value || []).join("; "); }
    function listValue(value) { return value.split(";").map(function(s) { return s.trim(); }).filter(function(s) { return s.length > 0; }); }
    function save() {
        const value = Object.assign({}, draft, {requirements: listValue(draft.requirementsText || ""),
            constraints: listValue(draft.constraintsText || ""), completionConditions: listValue(draft.completionText || "")});
        saving = true; workspace.saveProject(value);
    }
    function showAddResource() {
        addResourceToggle.checked = true;
        Qt.callLater(function() {
            const y = addResourceToggle.mapToItem(root.contentItem.contentItem, 0, 0).y;
            root.contentItem.contentY = Math.max(0, Math.min(y, root.contentItem.contentHeight - root.contentItem.height));
        });
    }
    Connections { target: root.workspace; function onChanged() { root.syncDraft(); } }
    Component.onCompleted: syncDraft()
    contentWidth: availableWidth
    clip: true
    Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
    FileDialog {
        id: sourceFile
        title: qsTr("Choose a project resource")
        onAccepted: resourceLocator.text = String(selectedFile)
    }
    component FieldLabel: LV.Label { style: caption; color: LV.Theme.descriptionColor }
    LV.VStack {
        width: root.availableWidth
        spacing: 8
        LV.HStack {
            Layout.fillWidth: true
            LV.Label { Layout.fillWidth: true; text: qsTr("PROJECT DEFINITION"); style: caption }
            LV.Label { visible: !!root.draft.projectId; text: qsTr("r%1").arg(root.draft.revision); style: caption }
        }
        LV.Label { visible: !root.draft.projectId; Layout.fillWidth: true; text: qsTr("Select a project to view or edit its definition."); style: description; wrapMode: Text.WordWrap; sizeToContentHeight: true }
        LV.VStack {
            visible: !!root.draft.projectId
            enabled: !root.workspace.busy
            Layout.fillWidth: true
            spacing: 6
            FieldLabel { text: qsTr("Name") }
            LV.InputField { objectName: "projectName"; Layout.fillWidth: true; text: root.draft.name || ""; onTextEdited: function(value) { root.change("name", value); } }
            FieldLabel { text: qsTr("Goal") }
            LV.TextEditor { objectName: "projectGoal"; Layout.fillWidth: true; filePath: ""; fieldMinHeight: 72; editorHeight: 72; Component.onCompleted: editorItem.textFormat = TextEdit.PlainText; text: root.draft.goal || ""; onTextEdited: function(value) { if (value !== (root.draft.goal || "")) root.change("goal", value); } }
            FieldLabel { text: qsTr("Requirements · separate items with semicolons") }
            LV.InputField { Layout.fillWidth: true; text: root.draft.requirementsText || ""; onTextEdited: function(value) { root.change("requirementsText", value); } }
            FieldLabel { text: qsTr("Constraints · separate items with semicolons") }
            LV.InputField { Layout.fillWidth: true; text: root.draft.constraintsText || ""; onTextEdited: function(value) { root.change("constraintsText", value); } }
            FieldLabel { text: qsTr("Completion conditions · separate items with semicolons") }
            LV.InputField { Layout.fillWidth: true; text: root.draft.completionText || ""; onTextEdited: function(value) { root.change("completionText", value); } }
            FieldLabel { text: qsTr("Status") }
            CalendarChoice {
                Layout.fillWidth: true
                value: root.draft.status || "draft"
                options: [{value:"draft",label:qsTr("Draft")},{value:"in_progress",label:qsTr("In progress")},{value:"completed",label:qsTr("Completed")},{value:"on_hold",label:qsTr("On hold")}]
                onChosen: function(value) { root.change("status", value); }
            }
            FieldLabel { text: qsTr("Gallery cover") }
            CalendarChoice { Layout.fillWidth: true; value: root.draft.coverResource || ""; options: root.covers; onChosen: function(value) { root.change("coverResource", value); } }
            LV.Label { visible: root.dirty && root.draft._expectedRevision !== root.workspace.project.revision; Layout.fillWidth: true; text: qsTr("This project changed. Discard or copy your draft before editing the latest revision."); style: description; wrapMode: Text.WordWrap; sizeToContentHeight: true }
            LV.HStack {
                Layout.fillWidth: true; spacing: 8
                LV.LabelButton { text: qsTr("Discard"); tone: LV.AbstractButton.Default; enabled: root.dirty; onClicked: root.discard() }
                Item { Layout.fillWidth: true }
                LV.LabelButton { objectName: "saveProject"; text: qsTr("Save changes"); tone: LV.AbstractButton.Primary; enabled: root.dirty && root.draft._expectedRevision === root.workspace.project.revision; onClicked: root.save() }
            }
            LV.Label { Layout.fillWidth: true; text: qsTr("Project ID: %1\nGoal ID: %2\nCreated: %3\nOwner: %4\nSpecified by: %5").arg(root.draft.projectId).arg(root.draft.goalId).arg(root.draft.createdAt).arg(root.draft.owner).arg(root.draft.specifiedBy); style: caption; wrapMode: Text.WrapAnywhere; sizeToContentHeight: true }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: LV.Theme.panelBackground12 }
            LV.Label { text: qsTr("RESOURCES · %1").arg(root.workspace.resources.length); style: caption }
            Repeater {
                model: root.workspace.resources
                LV.VStack {
                    id: resourceRow
                    required property var modelData
                    property bool editing: false
                    property string editRevision: ""
                    Layout.fillWidth: true; spacing: 3
                    LV.HStack {
                        Layout.fillWidth: true; spacing: 4
                        LV.Label { Layout.fillWidth: true; text: resourceRow.modelData.name; textFormat: Text.PlainText; style: body; elide: Text.ElideRight }
                        LV.LabelButton { text: resourceRow.editing ? qsTr("Cancel") : qsTr("Edit"); tone: LV.AbstractButton.Borderless; onClicked: { resourceRow.editing = !resourceRow.editing; resourceRow.editRevision = root.workspace.project.revision; } }
                        LV.LabelButton { text: qsTr("Open"); tone: LV.AbstractButton.Borderless; onClicked: root.workspace.openResource(resourceRow.modelData.id) }
                        LV.LabelButton { text: qsTr("Remove"); tone: LV.AbstractButton.Borderless; onClicked: root.workspace.removeResource(resourceRow.modelData.id) }
                    }
                    LV.Label { Layout.fillWidth: true; text: resourceRow.modelData.purpose + " · " + resourceRow.modelData.location + (resourceRow.modelData.derived ? " · " + qsTr("Derived") : ""); textFormat: Text.PlainText; style: caption; wrapMode: Text.WordWrap; sizeToContentHeight: true }
                    LV.Label { Layout.fillWidth: true; text: resourceRow.modelData.locator; textFormat: Text.PlainText; style: caption; elide: Text.ElideMiddle }
                    LV.Label { visible: !!resourceRow.modelData.assetId; Layout.fillWidth: true; text: qsTr("Asset ID: %1").arg(resourceRow.modelData.assetId); textFormat: Text.PlainText; style: caption; wrapMode: Text.WrapAnywhere; sizeToContentHeight: true }
                    LV.Label { Layout.fillWidth: true; text: qsTr("MIME: %1 · Size: %2").arg(resourceRow.modelData.mediaType || qsTr("Not specified")).arg(resourceRow.modelData.size || qsTr("Not measured")); style: caption; wrapMode: Text.WordWrap; sizeToContentHeight: true }
                    LV.Label { Layout.fillWidth: true; text: qsTr("Selected by: %1 · SHA-256: %2").arg(resourceRow.modelData.selectedBy).arg(resourceRow.modelData.sha256 || qsTr("Not specified")); style: caption; wrapMode: Text.WrapAnywhere; sizeToContentHeight: true }
                    LV.VStack {
                        visible: resourceRow.editing; Layout.fillWidth: true; spacing: 5
                        LV.InputField { id: editResourceName; Layout.fillWidth: true; text: resourceRow.modelData.name; placeholderText: qsTr("Resource name") }
                        LV.InputField { id: editResourcePurpose; Layout.fillWidth: true; text: resourceRow.modelData.purpose; placeholderText: qsTr("Purpose") }
                        LV.InputField { id: editResourceLocator; Layout.fillWidth: true; text: resourceRow.modelData.locator; enabled: resourceRow.modelData.location === "reference"; placeholderText: qsTr("Reference path or URI") }
                        LV.InputField { id: editResourceMime; Layout.fillWidth: true; text: resourceRow.modelData.mediaType; placeholderText: qsTr("MIME type") }
                        LV.InputField { id: editResourceHash; Layout.fillWidth: true; text: resourceRow.modelData.sha256; placeholderText: qsTr("Declared SHA-256") }
                        LV.CheckBox { id: editDerived; text: qsTr("Derived resource"); checked: resourceRow.modelData.derived }
                        LV.LabelButton {
                            text: qsTr("Save resource"); tone: LV.AbstractButton.Default
                            enabled: !!editResourceName.text && !!editResourcePurpose.text && !!editResourceLocator.text && resourceRow.editRevision === root.workspace.project.revision
                            onClicked: root.workspace.updateResource(resourceRow.modelData.id, {_expectedRevision:resourceRow.editRevision,name:editResourceName.text,purpose:editResourcePurpose.text,locator:editResourceLocator.text,mediaType:editResourceMime.text,sha256:editResourceHash.text,derived:editDerived.checked})
                        }
                    }
                }
            }
            LV.CheckBox { id: addResourceToggle; objectName: "projectAddResourceForm"; text: qsTr("Add a resource") }
            LV.VStack {
                visible: addResourceToggle.checked
                Layout.fillWidth: true; spacing: 5
                LV.InputField { id: resourceName; Layout.fillWidth: true; placeholderText: qsTr("Resource name") }
                LV.InputField { id: resourcePurpose; Layout.fillWidth: true; placeholderText: qsTr("Purpose in this project") }
                LV.HStack {
                    Layout.fillWidth: true
                    LV.InputField { id: resourceLocator; Layout.fillWidth: true; placeholderText: qsTr("File path or reference URI") }
                    LV.LabelButton { text: qsTr("Choose"); tone: LV.AbstractButton.Default; onClicked: sourceFile.open() }
                }
                LV.InputField { id: resourceMime; Layout.fillWidth: true; placeholderText: qsTr("MIME type, optional") }
                LV.InputField { id: resourceHash; Layout.fillWidth: true; placeholderText: qsTr("Declared SHA-256, optional") }
                CalendarChoice { id: resourceLocation; Layout.fillWidth: true; value: "reference"; options: [{value:"reference",label:qsTr("Reference existing resource")},{value:"embedded",label:qsTr("Copy file into package")}]; onChosen: function(v) { value = v; } }
                LV.CheckBox { id: derived; text: qsTr("Derived resource") }
                LV.LabelButton { text: qsTr("Add resource"); tone: LV.AbstractButton.Default; enabled: resourceName.text.length > 0 && resourcePurpose.text.length > 0 && resourceLocator.text.length > 0; onClicked: root.workspace.addResource({name:resourceName.text,purpose:resourcePurpose.text,locator:resourceLocator.text,mediaType:resourceMime.text,sha256:resourceHash.text,location:resourceLocation.value,derived:derived.checked}) }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: LV.Theme.panelBackground12 }
            LV.Label { text: qsTr("ENTITIES & RELATIONSHIPS"); style: caption }
            Repeater {
                model: root.workspace.entities
                LV.Label { required property var modelData; Layout.fillWidth: true; text: modelData.name + " · " + modelData.type + (modelData.description ? "\n" + modelData.description : "") + "\n" + qsTr("Declared by: %1").arg(modelData.declaredBy); textFormat: Text.PlainText; style: caption; wrapMode: Text.WrapAnywhere; sizeToContentHeight: true }
            }
            Repeater {
                model: root.workspace.relations
                LV.VStack {
                    id: relationRow
                    required property var modelData
                    Layout.fillWidth: true; spacing: 3
                    LV.Label { Layout.fillWidth: true; text: relationRow.modelData.subject + " → " + relationRow.modelData.type + " → " + relationRow.modelData.object; textFormat: Text.PlainText; style: caption; wrapMode: Text.WrapAnywhere; sizeToContentHeight: true }
                    LV.Label { Layout.fillWidth: true; text: qsTr("Actor: %1").arg(relationRow.modelData.actor) + (relationRow.modelData.note ? "\n" + relationRow.modelData.note : "") + (relationRow.modelData.confirmedBy ? "\n" + qsTr("Confirmed by %1 at %2").arg(relationRow.modelData.confirmedBy).arg(relationRow.modelData.confirmedAt) : ""); textFormat: Text.PlainText; style: caption; wrapMode: Text.WrapAnywhere; sizeToContentHeight: true }
                    LV.HStack {
                        Layout.fillWidth: true
                        LV.Label { Layout.fillWidth: true; text: relationRow.modelData.origin + (relationRow.modelData.confirmedBy ? " · " + qsTr("confirmed") : ""); style: caption }
                        LV.LabelButton { visible: relationRow.modelData.origin === "inferred" && !relationRow.modelData.confirmedBy; text: qsTr("Confirm"); tone: LV.AbstractButton.Borderless; onClicked: root.workspace.confirmRelation(relationRow.modelData.id) }
                        LV.LabelButton { text: qsTr("Remove"); tone: LV.AbstractButton.Borderless; onClicked: root.workspace.removeRelation(relationRow.modelData.id) }
                    }
                }
            }
            LV.CheckBox { id: entityToggle; text: qsTr("Add an entity") }
            LV.VStack {
                visible: entityToggle.checked; Layout.fillWidth: true; spacing: 5
                LV.InputField { id: entityName; Layout.fillWidth: true; placeholderText: qsTr("Entity name") }
                CalendarChoice { id: entityType; Layout.fillWidth: true; options: root.types; value: options.length ? options[0].value : ""; onChosen: function(v) { value = v; } }
                LV.InputField { id: entityDescription; Layout.fillWidth: true; placeholderText: qsTr("Description, optional") }
                LV.LabelButton { text: qsTr("Add entity"); tone: LV.AbstractButton.Default; enabled: !!entityName.text && !!entityType.value; onClicked: root.workspace.addEntity({name:entityName.text,type:entityType.value,description:entityDescription.text}) }
            }
            LV.CheckBox { id: relationToggle; text: qsTr("Add a relationship") }
            LV.VStack {
                visible: relationToggle.checked; Layout.fillWidth: true; spacing: 5
                CalendarChoice { id: subject; Layout.fillWidth: true; options: root.nodes; value: options.length ? options[0].value : ""; onChosen: function(v) { value = v; } }
                CalendarChoice { id: relationType; Layout.fillWidth: true; options: root.relationTypes; value: options.length ? options[0].value : ""; onChosen: function(v) { value = v; } }
                CalendarChoice { id: objectNode; Layout.fillWidth: true; options: root.nodes; value: options.length ? options[0].value : ""; onChosen: function(v) { value = v; } }
                LV.InputField { id: relationNote; Layout.fillWidth: true; placeholderText: qsTr("Relationship note, optional") }
                LV.LabelButton { text: qsTr("Add relationship"); tone: LV.AbstractButton.Default; enabled: !!subject.value && !!objectNode.value && !!relationType.value; onClicked: root.workspace.addRelation({subject:subject.value,type:relationType.value,object:objectNode.value,note:relationNote.text}) }
            }
            LV.CheckBox { id: schemaToggle; text: qsTr("Extend project schema") }
            LV.VStack {
                visible: schemaToggle.checked; Layout.fillWidth: true; spacing: 5
                LV.Label { Layout.fillWidth: true; text: qsTr("Use a stable identifier for a custom entity or relation type."); style: description; wrapMode: Text.WordWrap; sizeToContentHeight: true }
                LV.InputField { id: typeId; Layout.fillWidth: true; placeholderText: qsTr("Type identifier, e.g. ex:Material") }
                LV.InputField { id: typeLabel; Layout.fillWidth: true; placeholderText: qsTr("Type label") }
                LV.InputField { id: typeDescription; Layout.fillWidth: true; placeholderText: qsTr("Type description") }
                LV.HStack {
                    Layout.fillWidth: true; spacing: 6
                    LV.LabelButton { text: qsTr("Add entity type"); tone: LV.AbstractButton.Default; enabled: !!typeId.text && !!typeLabel.text && !!typeDescription.text; onClicked: root.workspace.addEntityType({id:typeId.text,label:typeLabel.text,description:typeDescription.text,parents:["iip:Entity"]}) }
                    LV.LabelButton { text: qsTr("Add relation type"); tone: LV.AbstractButton.Default; enabled: !!typeId.text && !!typeLabel.text && !!typeDescription.text; onClicked: root.workspace.addRelationType({id:typeId.text,label:typeLabel.text,description:typeDescription.text}) }
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: LV.Theme.panelBackground12 }
            LV.HStack {
                Layout.fillWidth: true
                LV.Label { Layout.fillWidth: true; text: qsTr("HISTORY & VALIDATION"); style: caption }
                LV.LabelButton { text: qsTr("Validate"); tone: LV.AbstractButton.Borderless; onClicked: root.workspace.validateProject(true) }
                LV.LabelButton { text: qsTr("Rebuild index"); tone: LV.AbstractButton.Borderless; onClicked: root.workspace.rebuildIndex() }
            }
            LV.Label { visible: root.workspace.validation.ok !== undefined; Layout.fillWidth: true; text: root.workspace.validation.ok ? qsTr("Project structure is valid.") : (root.workspace.validation.errors || []).join("\n"); style: description; wrapMode: Text.WordWrap; sizeToContentHeight: true }
            LV.Label { objectName: "projectValidationWarnings"; visible: (root.workspace.validation.warnings || []).length > 0; Layout.fillWidth: true; text: (root.workspace.validation.warnings || []).join("\n"); textFormat: Text.PlainText; style: description; wrapMode: Text.WrapAnywhere; sizeToContentHeight: true }
            Repeater {
                model: root.workspace.history.slice(0, 8)
                LV.Label { required property var modelData; Layout.fillWidth: true; text: qsTr("r%1 · %2\n%3 · %4").arg(modelData.revision).arg(modelData.timestamp).arg(modelData.actor).arg(modelData.changes.join(", ")); style: caption; wrapMode: Text.WrapAnywhere; sizeToContentHeight: true }
            }
        }
    }
}
