#include "Society/Native/NativeBackendWire.h"
#include <iiJson>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
using ii::core::Value;
using society::native::BackendFrames;
static void check(bool value,const char *message) { if(!value){std::cerr<<message<<'\n';std::exit(1);} }
int main() {
    auto first=BackendFrames::encode(Value(Value::Object{{"type",Value("state")},{"n",Value(2)}}));
    auto second=BackendFrames::encode(Value(Value::Object{{"type",Value("result")}}));
    check(bool(first)&&bool(second),"encode objects");
    BackendFrames frames; std::vector<Value> output;
    for(char c:*first) check(frames.feed(std::string_view(&c,1),output),"fragmented frame");
    check(output.size()==1,"one frame after fragments");
    const auto combined=*second+*first;
    check(frames.feed(combined,output)&&output.size()==3,"multiple frames in one read");
    std::string oversized("\x00\x10\x00\x01",4);
    check(!frames.feed(oversized,output),"oversized frame rejected");
    check(!frames.feed(*first,output),"failed stream stays failed");
    frames.reset();
    check(frames.feed(*second,output),"reset allows a new connection");
    frames.reset(); std::string invalid("\0\0\0\x03",4); invalid += "bad";
    check(!frames.feed(invalid,output),"invalid JSON rejected");
    check(!BackendFrames::encode(Value("non-object")),"non-object rejected");
}
