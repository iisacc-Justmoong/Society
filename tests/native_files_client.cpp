#include "Society/Native/NativeFilesClient.h"
#include <iiNetwork>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <unistd.h>
using namespace std::chrono_literals;
using ii::core::Value;
using namespace society::native;
static void check(bool ok,const char *error){if(!ok)throw std::runtime_error(error);}
static void until(ii::runtime::EventLoop &loop,const std::function<bool()> &ready){
    const auto deadline=std::chrono::steady_clock::now()+3s;
    while(!ready() && std::chrono::steady_clock::now()<deadline){loop.poll();std::this_thread::sleep_for(2ms);}
    check(ready(),"event loop timed out");
}
static std::string text(const Value &value,const char *key){
    auto v=value.find(key);check(bool(v),"missing field");auto s=v->get().get<std::string>();check(bool(s),"bad field");return s->get();
}
static void run(){
    auto dir=std::filesystem::path(SOCIETY_NATIVE_RESOURCE_ROOT)/(".native-client-"+std::to_string(getpid()));
    check(std::filesystem::create_directory(dir),"create fixture");
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove_all(p,e);}} cleanup{dir};
    ii::runtime::EventLoop loop;ii::network::LocalServer server(loop);std::unique_ptr<ii::network::LocalSocket> peer;
    BackendFrames decoder;std::vector<Value> requests;
    server.accepted=[&](auto stream){peer=std::move(stream);peer->readable=[&]{check(decoder.feed(peer->read(),requests),"decode request");};};
    const auto address=(dir/"backend.sock").string();check(bool(server.listen({address})),"listen");
    NativeBackendConnection connection(loop); NativeFilesViewModel model; NativeFilesClient binding(connection,model);
    check(bool(connection.connect(address)),"connect");until(loop,[&]{return connection.connected() && bool(peer);});
    auto send=[&](Value value){auto frame=BackendFrames::encode(value);check(frame && peer->write(*frame),"send fixture");};
    send(Value(Value::Object{{"type",Value("state")},{"state",Value(Value::Object{{"selectedContainer",Value("/fixture/Society")}})}}));
    until(loop,[&]{return requests.size()==1;});check(text(requests[0],"op")=="directoryOpen","open request");
    send(Value(Value::Object{{"type",Value("result")},{"id",Value(text(requests[0],"id"))},{"accepted",Value(true)},
        {"directory",Value(Value::Object{{"path",Value("Files")},{"generation",Value("generation")},{"revision",Value("revision")},{"status",Value("ready")},{"count",Value(1)}})}}));
    until(loop,[&]{return requests.size()==2;});check(text(requests[1],"op")=="directoryPage","page request");
    Value::Array entries{Value(Value::Object{{"name",Value("one.png")},{"path",Value("Files/one.png")},{"directory",Value(false)},{"size",Value("7")}})};
    send(Value(Value::Object{{"type",Value("result")},{"id",Value(text(requests[1],"id"))},{"accepted",Value(true)},
        {"generation",Value("generation")},{"revision",Value("revision")},{"offset",Value(0)},{"count",Value(1)},{"entries",Value(std::move(entries))}}));
    until(loop,[&]{auto s=model.source->snapshot();return s && (*s)->rows.size()==1;});
    peer->close();until(loop,[&]{return model.status.get()=="Society backend disconnected";});
}
int main(){try{run();return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
