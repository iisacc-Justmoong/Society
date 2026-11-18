#include "Society/Native/NativeBackendConnection.h"
#include <iiNetwork>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
using namespace std::chrono_literals;
using ii::core::Value;
using namespace society::native;
static void check(bool value,const char *message) {if(!value) throw std::runtime_error(message);}
static void until(ii::runtime::EventLoop &loop,const std::function<bool()> &ready) {
    const auto deadline=std::chrono::steady_clock::now()+3s;
    while (!ready() && std::chrono::steady_clock::now()<deadline) {loop.poll();std::this_thread::sleep_for(2ms);}
    check(ready(),"event loop timed out");
}
static void run() {
    auto dir=std::filesystem::path(SOCIETY_NATIVE_RESOURCE_ROOT)/(".native-ipc-"+std::to_string(getpid()));
    check(std::filesystem::create_directory(dir),"create owned fixture");
    struct Cleanup { std::filesystem::path path; ~Cleanup(){std::error_code e; std::filesystem::remove_all(path,e);} } cleanup{dir};
    const auto path=(dir/"backend.sock").string();
    ii::runtime::EventLoop loop; ii::network::LocalServer server(loop);
    std::unique_ptr<ii::network::LocalSocket> peer;
    BackendFrames incoming; std::vector<Value> requests;
    server.accepted=[&](auto stream) {
        peer=std::move(stream);
        peer->readable=[&] {auto bytes=peer->read();check(incoming.feed(bytes,requests),"request framing");};
    };
    check(bool(server.listen({path})),"listen fixture");
    NativeBackendConnection client(loop);
    bool connected=false; std::string error; std::vector<Value> messages;
    client.onConnected=[&]{connected=true;}; client.onError=[&](std::string s){error=std::move(s);};
    client.onMessage=[&](Value value){messages.push_back(std::move(value));};
    check(bool(client.connect(path)),"begin connection");
    until(loop,[&]{return connected && bool(peer);});
    check(client.connected(),"authenticated connection");
    const auto state=BackendFrames::encode(Value(Value::Object{{"type",Value("state")}}));check(bool(state),"fixture frame");
    check(peer->write(*state),"send state");
    until(loop,[&]{return messages.size()==1;});
    check(client.send(Value(Value::Object{{"id",Value("request")},{"op",Value("snapshot")}})),"send request");
    until(loop,[&]{return requests.size()==1;});
    client.close(); check(!client.connected(),"close");
    connected=false;
    check(bool(client.connect(path)),"reconnect after close");
    until(loop,[&]{return connected;}); check(client.connected(),"reconnected peer authenticated");
    client.close();
    NativeBackendConnection rejected(loop,[](const auto &){return false;});
    bool denied=false; rejected.onError=[&](std::string){denied=true;};
    check(bool(rejected.connect(path)),"begin rejected connection");
    until(loop,[&]{return denied;}); check(!rejected.connected(),"untrusted peer refused");
    check(error.empty(),"unexpected transport failure");
}
int main() {
    try { run(); return 0; }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
