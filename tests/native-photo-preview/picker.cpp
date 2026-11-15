#include "Society/Native/PhotoPicker.h"
#include <stdexcept>
#include <thread>

using namespace ii::gui;
using ii::core::ErrorCode;
using ii::core::Result;
using society::native::PhotoPicker;

void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
    PhotoPicker picker;
    FileDialogCompletion first,second;
    int opened=0,delivered=0,errors=0;
    auto transport=[&](FileDialogOptions,FileDialogCompletion completion)->Result<void>{
        ++opened;first=std::move(completion);return {};
    };
    auto consume=[&](Result<FileDialogSelection> result){
        if(!result){++errors;return;}
        if(!result->canceled&&result->paths==std::vector<std::string>{"/chosen"})++delivered;
    };
    check(bool(picker.open(transport,{},consume))&&opened==1,"A requested picker was not admitted");
    auto busy=picker.open(transport,{},consume);
    check(!busy&&busy.error().code==ErrorCode::busy&&opened==1,"A second picker was admitted while one was open");
    auto duplicate=first;
    first(FileDialogSelection{.paths={"/chosen"}});
    duplicate(FileDialogSelection{.paths={"/chosen"}});
    check(delivered==1,"A picker result was lost or delivered twice");

    check(bool(picker.open(transport,{},consume)),"A completed picker could not be reopened");
    auto stale=first;check(bool(picker.cancel()),"An open picker was not cancelled");
    auto replacement=[&](FileDialogOptions,FileDialogCompletion completion)->Result<void>{
        second=std::move(completion);return {};
    };
    check(bool(picker.open(replacement,{},consume)),"A cancelled picker blocked its replacement");
    stale(FileDialogSelection{.paths={"/chosen"}});
    check(delivered==1,"A retired picker delivered into its replacement");
    second(ii::core::fail(ErrorCode::failed,"picker failed"));
    check(errors==1,"An asynchronous picker error was not delivered");

    auto refused=picker.open([](FileDialogOptions,FileDialogCompletion)->Result<void>{
        return ii::core::fail(ErrorCode::unsupported,"picker unavailable");
    },{},consume);
    check(!refused&&refused.error().code==ErrorCode::unsupported,"A synchronous admission error was hidden");
    auto immediate=picker.open([&](FileDialogOptions,FileDialogCompletion completion)->Result<void>{
        completion(FileDialogSelection{.paths={"/chosen"}});return {};
    },{},consume);
    check(bool(immediate)&&delivered==2,"A synchronous picker completion was lost");
    FileDialogCompletion reentered;
    auto thrown=picker.open([&](FileDialogOptions,FileDialogCompletion completion)->Result<void>{
        completion(FileDialogSelection{.canceled=true});
        throw std::runtime_error("transport failed after completion");
    },{},[&](Result<FileDialogSelection>){
        auto reopened=picker.open([&](FileDialogOptions,FileDialogCompletion completion)->Result<void>{
            reentered=std::move(completion);return {};
        },{},consume);
        check(bool(reopened),"A completion could not start a new request");
    });
    check(!thrown&&bool(thrown.error().cause),"A transport exception was hidden");
    reentered(FileDialogSelection{.paths={"/chosen"}});
    check(delivered==3,"An older failure cancelled the reentrant request");
    std::vector<std::string> manyPaths;
    for(int i=0;i<300;++i)manyPaths.push_back("/selected/photo-"+std::to_string(i));
    std::vector<std::string> receivedPaths;
    check(bool(picker.open(transport,{},[&](Result<FileDialogSelection> selected){
        if(selected&&!selected->canceled)receivedPaths=std::move(selected->paths);
    })),"A large selection was not admitted");
    first(FileDialogSelection{.paths=manyPaths});
    check(receivedPaths==manyPaths,"A large selection was truncated or reordered");
    bool wrongThread=false;
    std::thread worker([&]{auto result=picker.cancel();wrongThread=!result&&result.error().code==ErrorCode::wrong_executor;});
    worker.join();check(wrongThread,"A foreign thread changed picker lifetime");

    FileDialogCompletion afterDestruction;
    {
        PhotoPicker temporary;
        check(bool(temporary.open([&](FileDialogOptions,FileDialogCompletion completion)->Result<void>{
            afterDestruction=std::move(completion);return {};
        },{},consume)),"The temporary picker did not open");
    }
    afterDestruction(FileDialogSelection{.paths={"/chosen"}});
    check(delivered==3,"A destroyed picker delivered a late completion");
}
