#include "Society/Native/NativeFilesViewModel.h"
#include <iiJson>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

using ii::core::Value;
static void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
static Value json(const char *input) {
    auto parsed = ii::json::parse(input);
    if (!parsed) throw std::runtime_error("bad fixture");
    return std::move(*parsed);
}
static std::string text(const Value &value, const char *key) {
    auto found = value.find(key); check(bool(found), "missing field");
    auto string = found->get().get<std::string>(); check(bool(string), "not text");
    return string->get();
}
static Value response(std::string id, Value::Object fields) {
    fields.emplace("type", Value("result")); fields.emplace("id", Value(std::move(id)));
    fields.emplace("accepted", Value(true)); return Value(std::move(fields));
}
static void run() {
    NativeFilesViewModel vm; std::vector<Value> sent;
    vm.onRequest = [&](Value value) { sent.push_back(std::move(value)); return true; };
    vm.receive(json(R"({"type":"state","state":{"selectedContainer":"/fixture/Society"}})"));
    check(sent.size()==1 && text(sent.back(),"op")=="directoryOpen" && text(sent.back(),"path")=="Files", "open initial root");
    const auto openId=text(sent.back(),"id");
    vm.receive(response(openId, {{"directory",json(R"({"path":"Files","generation":"g1","revision":"r1","status":"ready","count":2})")}}));
    check(sent.size()==2 && text(sent.back(),"op")=="directoryPage", "request page");
    const auto pageId=text(sent.back(),"id");
    vm.receive(response(pageId, {{"generation",Value("g1")},{"revision",Value("r1")},{"offset",Value(0)},{"count",Value(2)},
        {"entries",json(R"([{"name":"Folder","path":"Files/Folder","directory":true,"size":"0","modified":""},{"name":"image.png","path":"Files/image.png","directory":false,"size":"12","modified":"2026-01-01T00:00:00Z"}])")}}));
    auto snapshot=vm.source->snapshot(); check(snapshot && (*snapshot)->rows.size()==2,"populate LVUI table");
    vm.receive(json(R"({"type":"directoryChanged","directory":{"path":"Files","generation":"g1","revision":"r1","status":"loading","count":2}})"));
    vm.receive(json(R"({"type":"directoryChanged","directory":{"path":"Files","generation":"g1","revision":"r1","status":"ready","count":2}})"));
    check(vm.status.get()=="2 items", "restore status after unchanged refresh");
    vm.selected(lvui::TableSelection{{"Files/Folder","name"},{"Files/Folder","name"}});
    check(vm.canOpen.get(),"selected folder is openable"); vm.openSelected();
    check(sent.size()==3 && text(sent.back(),"path")=="Files/Folder", "navigate folder");
    const auto folderId=text(sent.back(),"id");
    vm.receive(response(pageId, {{"generation",Value("g1")},{"revision",Value("r1")},{"offset",Value(0)},{"count",Value(0)},{"entries",Value(Value::Array{})}}));
    snapshot=vm.source->snapshot(); check(snapshot && (*snapshot)->rows.empty(),"ignore late old page");
    vm.receive(response(folderId, {{"directory",json(R"({"path":"Files/Folder","generation":"g2","revision":"r2","status":"ready","count":0})")}}));
    check(vm.path.get()=="Files/Folder" && vm.canUp.get(),"folder state");
    const auto emptyId=text(sent.back(),"id");
    vm.receive(response(emptyId, {{"generation",Value("g2")},{"revision",Value("r2")},{"offset",Value(0)},{"count",Value(0)},{"entries",Value(Value::Array{})}}));
    check(vm.status.get()=="No items", "empty state");
    auto changed=json(R"({"type":"directoryChanged","directory":{"path":"Files/Folder","generation":"g2","revision":"r3","status":"ready","count":1}})");
    vm.receive(changed); const auto count=sent.size(); vm.receive(changed);
    check(sent.size()==count && text(sent.back(),"op")=="directoryPage", "coalesce duplicate revision");
    vm.disconnected(); check(!vm.canOpen.get() && vm.status.get()=="Society backend disconnected", "disconnection state");
}
static void fileJobs() {
    NativeFilesViewModel vm, second; std::vector<Value> sent, other;
    vm.onRequest=[&](Value value){sent.push_back(std::move(value));return true;};
    second.onRequest=[&](Value value){other.push_back(std::move(value));return true;};
    auto state=json(R"({"type":"state","state":{"selectedContainer":"/fixture/Society"}})");
    vm.receive(state); second.receive(state);
    check(text(sent[0],"id")!=text(other[0],"id"),"independent request IDs");
    vm.receive(response(text(sent[0],"id"),{{"directory",json(R"({"path":"Files","generation":"g","revision":"r","status":"ready","count":1})")}}));
    auto page=text(sent.back(),"id");
    vm.receive(response(page,{{"generation",Value("g")},{"revision",Value("r")},{"offset",Value(0)},{"count",Value(1)},
        {"entries",json(R"([{"name":"one.png","path":"Files/one.png","directory":false,"size":"7"}])")}}));
    vm.selected(lvui::TableSelection{{"Files/one.png","name"},{"Files/one.png","name"}});
    check(vm.canRename.get() && vm.canTrash.get(),"file actions available");
    vm.renameEdited("../unsafe"); const auto before=sent.size(); vm.renameSelected();
    check(sent.size()==before,"reject invalid file name");
    vm.renameEdited("new.png"); vm.renameSelected();
    check(text(sent.back(),"op")=="fileOperation" && text(sent.back(),"action")=="rename"
          && text(sent.back(),"path")=="Files/one.png" && text(sent.back(),"argument")=="new.png","rename request");
    const auto requestId=text(sent.back(),"id");
    const std::string jobId="9e0da5c9-0903-4996-82e4-c474d795d313";
    vm.receive(response(requestId,{{"jobId",Value(jobId)},{"state",Value("queued")}}));
    check(vm.activity.get().find("queued")!=std::string::npos,"queued is not yet durable");
    vm.receive(json(R"({"type":"fileJobChanged","jobId":"9e0da5c9-0903-4996-82e4-c474d795d313","container":"/fixture/Society","state":"recorded"})"));
    check(vm.activity.get().find("recorded")!=std::string::npos,"durable job acknowledged");
    bool reconnectRequested=false; vm.onReconnect=[&]{reconnectRequested=true;};
    vm.disconnected(); check(vm.canReconnect.get(),"offer reconnect"); vm.reconnect();
    check(reconnectRequested && !vm.canReconnect.get(),"explicit reconnect"); vm.receive(state);
    check(text(sent.back(),"op")=="fileJobStatus" && text(sent.back(),"jobId")==jobId,"recover active job on reconnect");
    auto query=text(sent.back(),"id");
    vm.receive(Value(Value::Object{{"type",Value("result")},{"id",Value(query)},{"accepted",Value(false)},{"error",Value("not recorded yet")}}));
    const auto beforeRetry=sent.size(); vm.refresh();
    check(sent.size()==beforeRetry+1 && text(sent.back(),"op")=="fileJobStatus" && text(sent.back(),"id")!=query,
          "retry status for queued job"); query=text(sent.back(),"id");
    vm.receive(response(query,{{"job",json(R"({"state":"attention","path":"","error":""})")}}));
    check(vm.activity.get().find(jobId)!=std::string::npos,"uncertain job remains identifiable");
    const auto beforeRecheck=sent.size(); vm.refresh();
    check(sent.size()==beforeRecheck+1 && text(sent.back(),"op")=="fileJobStatus","uncertain job remains queryable");
    query=text(sent.back(),"id");
    vm.receive(json(R"({"type":"fileJobChanged","jobId":"9e0da5c9-0903-4996-82e4-c474d795d313","container":"/fixture/Society","state":"finished","path":"Files/new.png","error":"Index update failed"})"));
    check(vm.activity.get().find("Files/new.png")!=std::string::npos && vm.activity.get().find("Index update failed")!=std::string::npos,
          "partial outcome retains path and failure");
    const auto finalActivity=vm.activity.get();
    vm.receive(response(query,{{"job",json(R"({"state":"running","path":"","error":""})")}}));
    check(vm.activity.get()==finalActivity,"late job status cannot replace a completed result");
    const auto beforeFailureCheck=sent.size(); vm.refresh();
    check(sent.size()==beforeFailureCheck+1 && text(sent.back(),"op")=="fileJobStatus"
          && text(sent.back(),"jobId")==jobId,"failed operation remains queryable after its completion event");
    vm.receive(response(text(sent.back(),"id"),{{"job",json(R"({"state":"attention","path":"Files/new.png","error":"Index update failed"})")}}));
    check(vm.activity.get().find(jobId)!=std::string::npos,"ambiguous operation keeps its identity");
    const auto beforeResolved=sent.size(); vm.refresh();
    check(sent.size()==beforeResolved+1 && text(sent.back(),"op")=="fileJobStatus","ambiguous operation can be checked again");
    vm.receive(response(text(sent.back(),"id"),{{"job",json(R"({"state":"partial","path":"Files/new.png","error":"Index update failed"})")}}));
    const auto afterResolved=sent.size(); vm.refresh();
    check(sent.size()==afterResolved,"resolved operation is retired");
}
static void sections() {
    NativeFilesViewModel vm; std::vector<Value> sent;
    vm.onRequest=[&](Value value){sent.push_back(std::move(value));return true;};
    vm.receive(json(R"({"type":"state","state":{"selectedContainer":"/fixture/Society"}})"));
    vm.showModels(); check(text(sent.back(),"path")=="Models" && vm.modelsActive.get(),"models root");
    auto id=text(sent.back(),"id");
    vm.receive(response(id,{{"directory",json(R"({"path":"Models","generation":"gm","revision":"rm","status":"ready","count":1})")}}));
    vm.receive(response(text(sent.back(),"id"),{{"generation",Value("gm")},{"revision",Value("rm")},{"offset",Value(0)},{"count",Value(1)},
        {"entries",json(R"([{"name":"LLM","path":"Models/LLM","directory":true,"size":"0"}])")}}));
    vm.selected(lvui::TableSelection{{"Models/LLM","name"},{"Models/LLM","name"}}); vm.openSelected();
    check(text(sent.back(),"path")=="Models/LLM","model category navigation");
    vm.showDeleted(); check(text(sent.back(),"path")=="Deleted" && vm.deletedActive.get(),"deleted root");
    vm.showFiles(); check(text(sent.back(),"path")=="Files" && vm.filesActive.get(),"files root");
}
static void additionalFileActions() {
    NativeFilesViewModel vm; std::vector<Value> sent;
    vm.onRequest=[&](Value value){sent.push_back(std::move(value));return true;};
    vm.receive(json(R"({"type":"state","state":{"selectedContainer":"/fixture/Society"}})"));
    vm.receive(response(text(sent.back(),"id"),{{"directory",json(R"({"path":"Files","generation":"g","revision":"r","status":"ready","count":1})")}}));
    vm.receive(response(text(sent.back(),"id"),{{"generation",Value("g")},{"revision",Value("r")},{"offset",Value(0)},{"count",Value(1)},
        {"entries",json(R"([{"name":"one.txt","path":"Files/one.txt","directory":false,"size":"7"}])")}}));
    check(vm.canCreateFolder.get(),"folder creation available in files");
    vm.showCreateFolder(); vm.folderEdited("../bad"); const auto beforeInvalid=sent.size(); vm.createFolder();
    check(sent.size()==beforeInvalid,"invalid folder name rejected");
    vm.folderEdited("Artwork"); vm.createFolder();
    check(text(sent.back(),"action")=="create-directory" && text(sent.back(),"path")=="Files"
          && text(sent.back(),"argument")=="Artwork","create folder in current directory");
    vm.selected(lvui::TableSelection{{"Files/one.txt","name"},{"Files/one.txt","name"}});
    check(vm.canDuplicate.get(),"duplicate available"); vm.duplicateSelected();
    check(text(sent.back(),"action")=="duplicate" && text(sent.back(),"path")=="Files/one.txt","duplicate submitted");
    vm.showDeleted();
    vm.receive(response(text(sent.back(),"id"),{{"directory",json(R"({"path":"Deleted","generation":"d","revision":"r","status":"ready","count":1})")}}));
    vm.receive(response(text(sent.back(),"id"),{{"generation",Value("d")},{"revision",Value("r")},{"offset",Value(0)},{"count",Value(1)},
        {"entries",json(R"([{"name":"old.txt","path":"Deleted/old.txt","directory":false,"size":"7"}])")}}));
    vm.selected(lvui::TableSelection{{"Deleted/old.txt","name"},{"Deleted/old.txt","name"}});
    check(!vm.canCreateFolder.get() && !vm.canDuplicate.get() && vm.canRemove.get(),"deleted actions differ");
    const auto beforeConfirm=sent.size(); vm.removeSelected(); check(sent.size()==beforeConfirm,"permanent removal needs confirmation");
    vm.showRemove(); check(vm.removeOpen.get(),"permanent removal confirmation opens"); vm.removeSelected();
    check(text(sent.back(),"action")=="remove" && text(sent.back(),"path")=="Deleted/old.txt","confirmed permanent removal submitted");
}
static void queryAndSort() {
    NativeFilesViewModel vm; std::vector<Value> sent;
    vm.onRequest=[&](Value value){sent.push_back(std::move(value));return true;};
    vm.receive(json(R"({"type":"state","state":{"selectedContainer":"/fixture/Society"}})"));
    vm.searchEdited("Alpha"); vm.search();
    check(text(sent.back(),"path")=="Files" && text(sent.back(),"query")=="Alpha", "search current folder");
    vm.sortKind(); check(text(sent.back(),"sort")=="kind","sort by kind");
    vm.sortKind();
    const auto descending=sent.back().find("descending");
    check(descending && descending->get().get<bool>() && descending->get().get<bool>()->get(),"toggle sort direction");
    vm.showModels();
    check(text(sent.back(),"path")=="Models" && text(sent.back(),"query").empty(),"navigation clears search");
}
static void fileActivation() {
    NativeFilesViewModel vm; std::vector<Value> sent; std::string opened;
    vm.onRequest=[&](Value value){sent.push_back(std::move(value));return true;};
    vm.onOpenFile=[&](const std::string &path)->std::optional<std::string>{opened=path;return {};};
    vm.receive(json(R"({"type":"state","state":{"selectedContainer":"/fixture/Society"}})"));
    vm.receive(response(text(sent.back(),"id"),{{"directory",json(R"({"path":"Files","generation":"g","revision":"r","status":"ready","count":1})")}}));
    vm.receive(response(text(sent.back(),"id"),{{"generation",Value("g")},{"revision",Value("r")},{"offset",Value(0)},{"count",Value(1)},
        {"entries",json(R"([{"name":"one.png","path":"Files/one.png","directory":false,"size":"7"}])")}}));
    vm.selected(lvui::TableSelection{{"Files/one.png","name"},{"Files/one.png","name"}});
    check(vm.canOpen.get(),"a file can be opened"); vm.openSelected();
    const auto id=text(sent.back(),"id"); check(text(sent.back(),"op")=="directoryActivate","ask container to prepare the file");
    vm.receive(response(id,{}));
    vm.receive(Value(Value::Object{{"type",Value("directoryActivated")},{"id",Value(id)},{"generation",Value("g")},
        {"relative",Value("Files/one.png")},{"path",Value("/fixture/Society/Files/one.png")},{"error",Value("")}}));
    check(opened=="/fixture/Society/Files/one.png","open the prepared file");
    vm.openSelected(); const auto late=text(sent.back(),"id"); vm.showModels();
    opened.clear(); vm.receive(Value(Value::Object{{"type",Value("directoryActivated")},{"id",Value(late)},{"generation",Value("g")},
        {"relative",Value("Files/one.png")},{"path",Value("/fixture/Society/Files/one.png")},{"error",Value("")}}));
    check(opened.empty(),"ignore a result from a previous directory");
}
static void rangeActions() {
    NativeFilesViewModel vm; std::vector<Value> sent;
    vm.onRequest=[&](Value value){sent.push_back(std::move(value));return true;};
    vm.receive(json(R"({"type":"state","state":{"selectedContainer":"/fixture/Society"}})"));
    vm.receive(response(text(sent.back(),"id"),{{"directory",json(R"({"path":"Files","generation":"g","revision":"r","status":"ready","count":3})")}}));
    vm.receive(response(text(sent.back(),"id"),{{"generation",Value("g")},{"revision",Value("r")},{"offset",Value(0)},{"count",Value(3)},
        {"entries",json(R"([{"name":"a","path":"Files/a","directory":false},{"name":"b","path":"Files/b","directory":false},{"name":"c","path":"Files/c","directory":false}])")}}));
    check(vm.controller.get()->mode().value()==lvui::TableSelectionMode::range,"table must allow range selection");
    vm.selected(lvui::TableSelection{{"Files/c","name"},{"Files/a","name"}});
    check(vm.canTrash.get() && vm.canDuplicate.get() && !vm.canOpen.get() && !vm.canRename.get(),"range capabilities");
    const auto before=sent.size(); vm.trashSelected();
    check(sent.size()==before+3,"range submits one job per file");
    check(text(sent[before],"path")=="Files/a" && text(sent[before+1],"path")=="Files/b"
          && text(sent[before+2],"path")=="Files/c","range ordering and paths");
    vm.showMove(); check(vm.moveOpen.get(),"move dialog opens");
    vm.moveEdited("Files/../escape"); const auto beforeInvalidMove=sent.size(); vm.moveSelected();
    check(sent.size()==beforeInvalidMove,"reject an escaping destination");
    vm.moveEdited("Files/destination"); vm.moveSelected();
    check(sent.size()==beforeInvalidMove+3 && text(sent[beforeInvalidMove],"action")=="move"
          && text(sent[beforeInvalidMove],"argument")=="Files/destination"
          && text(sent[beforeInvalidMove+2],"path")=="Files/c","move every selected file to requested directory");
    vm.showMove(); vm.selected(lvui::TableSelection{{"Files/a","name"},{"Files/a","name"}});
    check(!vm.moveOpen.get(),"selection change cancels move dialog");
    const auto rejected=text(sent[before+1],"id"), accepted=text(sent[before+2],"id");
    vm.receive(Value(Value::Object{{"type",Value("result")},{"id",Value(rejected)},{"accepted",Value(false)},{"error",Value("locked")}}));
    vm.receive(response(accepted,{{"jobId",Value("job-c")},{"state",Value("queued")}}));
    check(vm.errors.get().find("Files/b")!=std::string::npos && vm.errors.get().find("locked")!=std::string::npos,
          "individual failure remains visible after another item queues");
    vm.clearErrors(); check(vm.errors.get().empty(),"individual failures can be dismissed");
    for (int i=0;i<40;++i) {
        vm.trashSelected(); const auto id=text(sent.back(),"id");
        vm.receive(Value(Value::Object{{"type",Value("result")},{"id",Value(id)},{"accepted",Value(false)},
            {"error",Value("failure "+std::to_string(i))}}));
    }
    check(vm.errors.get().find("failure 0")!=std::string::npos && vm.errors.get().find("failure 39")!=std::string::npos,
          "all failures from a selected page remain available");
    check(vm.errorCountLabel.get()=="40 / 40" && vm.currentError.get().find("failure 39")!=std::string::npos,
          "latest failure and count are visible");
    for (int i=0;i<39;++i) vm.previousError();
    check(vm.errorCountLabel.get()=="1 / 40" && vm.currentError.get().find("failure 0")!=std::string::npos,
          "previous error navigation reaches first failure");
    vm.showDeleted();
    vm.receive(response(text(sent.back(),"id"),{{"directory",json(R"({"path":"Deleted","generation":"gd","revision":"rd","status":"ready","count":2})")}}));
    vm.receive(response(text(sent.back(),"id"),{{"generation",Value("gd")},{"revision",Value("rd")},{"offset",Value(0)},{"count",Value(2)},
        {"entries",json(R"([{"name":"a","path":"Deleted/a","directory":false},{"name":"b","path":"Deleted/b","directory":false}])")}}));
    const lvui::TableSelection both{{"Deleted/a","name"},{"Deleted/b","name"}};
    vm.selected(both); vm.showRemove(); check(vm.removeOpen.get(),"range removal confirmation opens");
    vm.selected(lvui::TableSelection{{"Deleted/a","name"},{"Deleted/a","name"}});
    check(!vm.removeOpen.get(),"changing selection closes removal confirmation");
    const auto beforeRemove=sent.size(); vm.removeSelected(); check(sent.size()==beforeRemove,"changed selection is not deleted");
    vm.selected(both); vm.showRemove(); vm.removeSelected();
    check(sent.size()==beforeRemove+2 && text(sent[beforeRemove],"path")=="Deleted/a"
          && text(sent[beforeRemove+1],"path")=="Deleted/b","confirmed range removal submits selected files");
}
static void gridActions() {
    NativeFilesViewModel vm; std::vector<Value> sent;
    vm.onRequest=[&](Value value){sent.push_back(std::move(value));return true;};
    vm.receive(json(R"({"type":"state","state":{"selectedContainer":"/fixture/Society"}})"));
    vm.receive(response(text(sent.back(),"id"),{{"directory",json(R"({"path":"Files","generation":"g","revision":"r","status":"ready","count":3})")}}));
    vm.receive(response(text(sent.back(),"id"),{{"generation",Value("g")},{"revision",Value("r")},{"offset",Value(0)},{"count",Value(3)},
        {"entries",json(R"([{"name":"a","path":"Files/a","directory":false},{"name":"b","path":"Files/b","directory":false},{"name":"c","path":"Files/c","directory":false}])")}}));
    const auto rows=vm.gridRows.get(); check(rows.size()==3,"grid uses current directory page");
    vm.showGrid(); check(vm.gridActive.get(),"show grid");
    vm.gridSelected({rows[0].id,rows[2].id});
    const auto before=sent.size(); vm.trashSelected();
    check(sent.size()==before+2 && text(sent[before],"path")=="Files/a" && text(sent[before+1],"path")=="Files/c",
          "grid submits only individually selected items");
    vm.showList(); check(!vm.canTrash.get() && !vm.gridActive.get(),"mode switch clears selection");
    vm.showGrid(); vm.gridSelected({"stale-row-id"}); check(!vm.canTrash.get(),"stale grid key is ignored");
    vm.receive(json(R"({"type":"directoryChanged","directory":{"path":"Files","generation":"g","revision":"r2","status":"ready","count":1}})"));
    vm.receive(response(text(sent.back(),"id"),{{"generation",Value("g")},{"revision",Value("r2")},{"offset",Value(0)},{"count",Value(1)},
        {"entries",json(R"([{"name":"a","path":"Files/a","directory":false}])")}}));
    vm.gridSelected({rows[0].id}); check(!vm.canTrash.get(),"keys from an older grid revision are ignored");
}
static void modelImport() {
    NativeFilesViewModel vm; std::vector<Value> sent; bool chooser=false;
    vm.onRequest=[&](Value value){sent.push_back(std::move(value));return true;};
    vm.onChooseModels=[&]{chooser=true;};
    vm.receive(json(R"({"type":"state","state":{"selectedContainer":"/fixture/Society"}})"));
    vm.showModels(); vm.chooseModels(); check(chooser,"open model selector");
    vm.modelsChosen({"/source/first.safetensors","/source/second.safetensors"});
    check(text(sent.back(),"op")=="import","send model import to daemon");
    const auto sources=sent.back().find("sources");
    check(sources && sources->get().get<Value::Array>() && sources->get().get<Value::Array>()->get().size()==2,
          "send both selected model paths");
    const auto requestId=text(sent.back(),"id");
    vm.receive(response(requestId,{{"jobId",Value("import-job")},{"state",json(R"({"selectedContainer":"/fixture/Society","currentJobId":"import-job","busy":true,"status":"Importing"})")}}));
    check(vm.modelImportStatus.get().find("Importing")!=std::string::npos,"show active import");
    vm.disconnected(); check(vm.modelImportStatus.get().find("import-job")!=std::string::npos,"retain import identity on disconnect");
    vm.receive(json(R"({"type":"state","state":{"selectedContainer":"/fixture/Society","currentJobId":"import-job","busy":true,"status":"Importing"}})"));
    check(vm.modelImportStatus.get().find("Importing")!=std::string::npos,"restore import progress after reconnect");
    vm.receive(json(R"({"type":"state","state":{"selectedContainer":"/fixture/Society","currentJobId":"import-job","busy":false,"status":"2 models imported","errorString":""}})"));
    check(vm.activity.get().find("2 models imported")!=std::string::npos,"show completed import");
    chooser=false; vm.chooseModels(); check(chooser,"select another model");
    vm.receive(json(R"({"type":"state","state":{"selectedContainer":"/fixture/other"}})"));
    const auto before=sent.size(); vm.modelsChosen({"/source/other.safetensors"});
    check(sent.size()==before,"do not import into a changed container");
    vm.chooseModels(); vm.modelsChosen({"/source/other.safetensors"}); const auto delayed=text(sent.back(),"id");
    vm.receive(json(R"({"type":"state","state":{"selectedContainer":"/fixture/third"}})"));
    vm.receive(response(delayed,{{"jobId",Value("old-drive-job")},{"state",json(R"({"selectedContainer":"/fixture/other","currentJobId":"old-drive-job","busy":true,"status":"Old drive importing"})")}}));
    check(vm.modelImportStatus.get().find("another Society drive")!=std::string::npos,
          "late acknowledgement remains attached to its original drive");
}
static void fileImport() {
    NativeFilesViewModel vm; std::vector<Value> sent; bool chooser=false;
    vm.onRequest=[&](Value value){sent.push_back(std::move(value));return true;};
    vm.onChooseFiles=[&]{chooser=true;};
    vm.receive(json(R"({"type":"state","state":{"selectedContainer":"/fixture/Society"}})"));
    vm.receive(response(text(sent.back(),"id"),{{"directory",json(R"({"path":"Files","generation":"g","revision":"r","status":"ready","count":0})")}}));
    vm.receive(response(text(sent.back(),"id"),{{"generation",Value("g")},{"revision",Value("r")},{"offset",Value(0)},{"count",Value(0)},{"entries",Value(Value::Array{})}}));
    check(vm.canImportFiles.get(),"file import available in files");
    vm.chooseFiles(); check(chooser && !vm.canImportFiles.get(),"open file selector once");
    const auto before=sent.size(); vm.filesChosen({"/source/a.png","/source/b.png"});
    check(sent.size()==before+2 && text(sent[before],"action")=="copy" && text(sent[before],"path")=="/source/a.png"
          && text(sent[before],"argument")=="Files" && text(sent[before+1],"path")=="/source/b.png","submit each file to captured folder");
    chooser=false; vm.chooseFiles(); check(chooser,"select more files");
    vm.showModels(); const auto afterNavigation=sent.size(); vm.filesChosen({"/source/stale.png"});
    check(sent.size()==afterNavigation,"file selection cannot target a different folder");
}
static void errorNotifications() {
    NativeFilesViewModel vm;int changes=0;vm.onNotificationChanged=[&]{++changes;};
    vm.backendError("First failure");vm.backendError("First failure");vm.backendError("Second failure");
    check(vm.notificationMessage.get()=="First failure" && changes==3,"first notification or repeated errors were lost");
    vm.dismissNotification();check(vm.notificationMessage.get()=="First failure","repeated failure was not retained");
    vm.dismissNotification();check(vm.notificationMessage.get()=="Second failure","notifications were not ordered");
    vm.dismissNotification();check(vm.notificationMessage.get().empty() && vm.hasErrors.get(),"dismiss removed source error history");
}
int main() { try { run(); fileJobs(); sections(); additionalFileActions(); queryAndSort(); fileActivation(); rangeActions(); gridActions(); modelImport(); fileImport(); errorNotifications(); return 0; } catch (const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; } }
