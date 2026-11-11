#include "Society/Native/NativeFilesClient.h"
#include "Daemon/ModelBackendServer.h"
#include "Shared/LocalBackendAddress.h"
#include <SocietyDrive.h>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace std::chrono_literals;
static void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static void until(ii::runtime::EventLoop &loop,const std::function<bool()> &ready){
    const auto deadline=std::chrono::steady_clock::now()+30s;
    while(!ready() && std::chrono::steady_clock::now()<deadline){
        loop.poll();QCoreApplication::processEvents();std::this_thread::sleep_for(2ms);
    }
    check(ready(),"integration event loop timed out");
}
static void run(){
    QTemporaryDir fixture(QStringLiteral(SOCIETY_NATIVE_RESOURCE_ROOT)+"/.native-integration-XXXXXX");
    check(fixture.isValid(),"create fixture"); qputenv("SOCIETY_JOB_STATE_ROOT",fixture.filePath("jobs").toUtf8());
    const auto root=fixture.filePath("drive.societycontainer");check(QDir().mkdir(root),"create drive directory");
    check(bool(iiSocietyContainer::SocietyDrive::create(root)),"create drive");
    QFile source(root+"/Files/original.txt");check(source.open(QIODevice::WriteOnly),"create file");source.write("fixture");source.close();
    const auto modelSource=fixture.filePath("model.safetensors");
    const QByteArray payload(4096,'m');
    const auto header=QJsonDocument(QJsonObject{{"tensor",QJsonObject{{"dtype","U8"},{"shape",QJsonArray{payload.size()}},
        {"data_offsets",QJsonArray{0,payload.size()}}}}}).toJson(QJsonDocument::Compact);
    QByteArray prefix(8,'\0'); qToLittleEndian(quint64(header.size()),prefix.data());
    QFile input(modelSource); check(input.open(QIODevice::WriteOnly),"create model source");
    check(input.write(prefix+header+payload)==prefix.size()+header.size()+payload.size(),"write model source"); input.close();
    const auto broker=fixture.filePath("broker");check(QDir().mkdir(broker),"create broker directory");
    check(QFile::setPermissions(broker,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner),"secure broker");
    ModelBackendServer server;server.selectContainer(root);check(server.listen(broker),"listen");
    ii::runtime::EventLoop loop;society::native::NativeBackendConnection connection(loop);
    NativeFilesViewModel model;society::native::NativeFilesClient binding(connection,model);
    std::string opened;
    model.onOpenFile=[&](const std::string &path)->std::optional<std::string>{opened=path;return {};};
    check(bool(connection.connect(societyBackendAddress(broker).toStdString())),"connect");
    until(loop,[&]{auto snapshot=model.source->snapshot();return snapshot && (*snapshot)->rows.size()==1;});
    model.selected(lvui::TableSelection{{"Files/original.txt","name"},{"Files/original.txt","name"}});
    check(model.canRename.get() && model.canOpen.get(),"select file");
    model.openSelected(); until(loop,[&]{return opened==(root+"/Files/original.txt").toStdString();});
    model.renameEdited("renamed.txt");model.renameSelected();
    until(loop,[&]{return QFileInfo::exists(root+"/Files/renamed.txt") && model.activity.get().find("completed")!=std::string::npos;});
    check(!QFileInfo::exists(source.fileName()),"old file was renamed");
    until(loop,[&]{return model.canCreateFolder.get();});
    model.showCreateFolder(); model.folderEdited("Artwork"); model.createFolder();
    until(loop,[&]{return QFileInfo(root+"/Files/Artwork").isDir() && model.activity.get().find("completed")!=std::string::npos;});
    const auto hasRow=[&](const std::string &key) {
        auto snapshot=model.source->snapshot();
        return snapshot && std::any_of((*snapshot)->rows.begin(),(*snapshot)->rows.end(),[&](const auto &row){return row.key==key;});
    };
    until(loop,[&]{return hasRow("Files/renamed.txt");});
    model.selected(lvui::TableSelection{{"Files/renamed.txt","name"},{"Files/renamed.txt","name"}});
    model.duplicateSelected();
    until(loop,[&]{return QFileInfo::exists(root+"/Files/renamed copy.txt") && model.activity.get().find("completed")!=std::string::npos;});
    model.searchEdited("COPY"); model.search();
    until(loop,[&]{return hasRow("Files/renamed copy.txt") && !hasRow("Files/renamed.txt");});
    until(loop,[&]{return hasRow("Files/renamed copy.txt");});
    model.selected(lvui::TableSelection{{"Files/renamed copy.txt","name"},{"Files/renamed copy.txt","name"}});
    model.trashSelected();
    until(loop,[&]{return !QFileInfo::exists(root+"/Files/renamed copy.txt") && model.activity.get().find("completed")!=std::string::npos;});
    model.showDeleted(); until(loop,[&]{return hasRow("Deleted/renamed copy.txt");});
    model.selected(lvui::TableSelection{{"Deleted/renamed copy.txt","name"},{"Deleted/renamed copy.txt","name"}});
    model.showRemove(); check(model.removeOpen.get(),"confirm removal"); model.removeSelected();
    until(loop,[&]{return !QFileInfo::exists(root+"/Deleted/renamed copy.txt") && model.activity.get().find("completed")!=std::string::npos;});
    model.showFiles(); until(loop,[&]{return hasRow("Files/renamed.txt") && hasRow("Files/Artwork");});
    QFile external(fixture.filePath("external.txt")); check(external.open(QIODevice::WriteOnly),"create external file");
    external.write("external"); external.close(); model.onChooseFiles=[]{};
    until(loop,[&]{return model.canImportFiles.get();}); model.chooseFiles();
    model.filesChosen({QFileInfo(external).canonicalFilePath().toStdString()});
    until(loop,[&]{return QFileInfo::exists(root+"/Files/external.txt") && model.activity.get().find("completed")!=std::string::npos;});
    check(QFileInfo::exists(external.fileName()),"file import preserves source");
    model.selected(lvui::TableSelection{{"Files/renamed.txt","name"},{"Files/renamed.txt","name"}});
    model.showMove(); model.moveEdited("Files/Artwork"); model.moveSelected();
    until(loop,[&]{return QFileInfo::exists(root+"/Files/Artwork/renamed.txt")
                           && !QFileInfo::exists(root+"/Files/renamed.txt")
                           && model.activity.get().find("completed")!=std::string::npos;});
    model.showModels(); model.onChooseModels=[]{}; until(loop,[&]{return model.canImportModels.get();});
    model.chooseModels(); model.modelsChosen({modelSource.toStdString()});
    QString imported;
    until(loop,[&]{QDirIterator it(root+"/Models",{"model.safetensors"},QDir::Files,QDirIterator::Subdirectories);
        if (it.hasNext()) imported=it.next();
        return !imported.isEmpty() && QFileInfo::exists(root+"/.society-runtime/classifications/models/"
            +QDir(root+"/Models").relativeFilePath(imported)+".iisacc.json")
            && model.activity.get().find("Imported")!=std::string::npos;});
    check(!QFileInfo::exists(imported+".iisacc.json"),"model metadata remains outside the visible tree");
    check(QFileInfo::exists(modelSource),"model import preserves source");
    connection.close();qunsetenv("SOCIETY_JOB_STATE_ROOT");
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{run();return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
