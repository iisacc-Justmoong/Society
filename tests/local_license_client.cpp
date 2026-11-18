#include <iiAccountManager>
#include "LicenseTokenFixture.h"
#include <chrono>
#include <iostream>
#include <thread>
using namespace iisacc::accounts::native;
int main(int argc,char** argv){
    if(argc!=3)return 3;
    ii::runtime::EventLoop loop;
    auto device=detected_device("com.iisacc.congregation.painter","1",std::string(64,'a'));
    if(device.id!=argv[2]){std::cerr<<"product and Society device identities differ\n";return 4;}
    SocietySessionOptions options{.product_id="congregation",.device=std::move(device),
        .socket_path=argv[1],.timeout=std::chrono::seconds(3),.keys={{"fixture-owned-license",license_fixture::public_key()}}};
    SocietySession session(loop,std::move(options));bool done=false,licensed=false;
    session.changed=[&](const auto& value){if(!value.checking){done=true;licensed=value.licensed;std::cout<<value.code<<'\n';}};
    const auto started=std::chrono::steady_clock::now();
    session.refresh();const auto end=ii::runtime::Clock::now()+std::chrono::seconds(12);
    while(!done&&ii::runtime::Clock::now()<end){loop.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started);
    std::cout<<"LOCAL_LICENSE_CLIENT_US="<<elapsed.count()<<'\n';
    if(done && elapsed>=std::chrono::seconds(1))return 5;
    return !done?3:licensed?0:2;
}
