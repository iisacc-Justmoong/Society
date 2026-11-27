#include "../App/Shared/WindowsPipePeer.h"

#ifndef _WIN32
#error This test must be compiled with a Windows target.
#endif

// Both ends of a real local pipe must identify the other process. The same
// source can also be cross-compiled without claiming a Windows runtime test.
int main() {
    if (society::peer::pipePeerProcessId(nullptr)!=0 || !society::peer::processImagePath(0).empty()) return 1;
    const auto name=std::wstring(L"\\\\.\\pipe\\society-peer-test-")+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    const auto server=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,
                                       1,4096,4096,0,nullptr);
    if (server==INVALID_HANDLE_VALUE) return 2;
    const auto client=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
    if (client==INVALID_HANDLE_VALUE) { CloseHandle(server); return 3; }
    const auto connected=ConnectNamedPipe(server,nullptr) || GetLastError()==ERROR_PIPE_CONNECTED;
    const auto expected=GetCurrentProcessId();
    const auto fromServer=society::peer::pipePeerProcessId(server);
    const auto fromClient=society::peer::pipePeerProcessId(client);
    const auto image=society::peer::processImagePath(expected);
    CloseHandle(client); DisconnectNamedPipe(server); CloseHandle(server);
    return connected && fromServer==expected && fromClient==expected && !image.empty() ? 0 : 4;
}
