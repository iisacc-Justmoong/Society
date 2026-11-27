#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <string>

namespace society::peer {

// The kernel supplies the other endpoint's process ID. Select the client or
// server query from the actual pipe end rather than trusting a protocol field.
[[nodiscard]] inline DWORD pipePeerProcessId(HANDLE pipe) {
    DWORD flags=0; ULONG pid=0;
    if (!pipe || pipe==INVALID_HANDLE_VALUE || !GetNamedPipeInfo(pipe,&flags,nullptr,nullptr,nullptr)) return 0;
    const auto ok=(flags&PIPE_SERVER_END) ? GetNamedPipeClientProcessId(pipe,&pid) : GetNamedPipeServerProcessId(pipe,&pid);
    return ok ? DWORD(pid) : 0;
}

// Query the executable path of the identified process. Failure remains an
// empty result, so callers cannot silently trust an inaccessible process.
[[nodiscard]] inline std::wstring processImagePath(DWORD pid) {
    const auto process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if (!process) return {};
    std::wstring path(32768,L'\0'); DWORD length=DWORD(path.size());
    const auto ok=QueryFullProcessImageNameW(process,0,path.data(),&length);
    CloseHandle(process);
    if (!ok || !length) return {};
    path.resize(length);
    return path;
}

}
#endif
