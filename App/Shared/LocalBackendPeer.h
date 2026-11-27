#pragma once

#include <QtGlobal>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#endif
#include <QLocalSocket>
#include <QFile>
#include <QFileInfo>
#include <QString>
#ifdef Q_OS_WIN
#include "WindowsPipePeer.h"
// Shared with the Windows named-pipe peer verification below.
inline bool societyBackendSameWindowsUser(QLocalSocket *socket, bool serverSide);
#endif

#if defined(Q_OS_DARWIN)
#include <libproc.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#elif defined(Q_OS_LINUX)
#include <sys/socket.h>
#include <unistd.h>
#elif defined(Q_OS_FREEBSD)
#include <sys/socket.h>
#include <unistd.h>
#endif

// The OS supplies the peer PID; a PID claimed inside a protocol frame is never
// used as proof of UI identity. Unsupported platforms fail closed.
inline qint64 societyBackendPeerPid(QLocalSocket *socket)
{
    if (!socket || socket->socketDescriptor() < 0) return -1;
#if defined(Q_OS_DARWIN)
    pid_t pid = 0; socklen_t size = sizeof(pid);
    return ::getsockopt(int(socket->socketDescriptor()), SOL_LOCAL, LOCAL_PEERPID, &pid, &size) == 0
        && size == sizeof(pid) && pid > 0 ? qint64(pid) : -1;
#elif defined(Q_OS_LINUX)
    struct ucred peer{}; socklen_t size = sizeof(peer);
    return ::getsockopt(int(socket->socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0
        && size == sizeof(peer) && peer.pid > 0 ? qint64(peer.pid) : -1;
#elif defined(Q_OS_WIN)
    const auto pipe=reinterpret_cast<HANDLE>(socket->socketDescriptor());
    const auto pid=society::peer::pipePeerProcessId(pipe);
    return pid ? qint64(pid) : -1;
#else
    return -1;
#endif
}

inline bool societyBackendPeerSameUser(QLocalSocket *socket)
{
    if (!socket || socket->socketDescriptor() < 0) return false;
#if defined(Q_OS_DARWIN) || defined(Q_OS_FREEBSD)
    uid_t uid{}; gid_t gid{};
    return ::getpeereid(int(socket->socketDescriptor()), &uid, &gid) == 0 && uid == ::geteuid();
#elif defined(Q_OS_LINUX)
    struct ucred peer{}; socklen_t size = sizeof(peer);
    return ::getsockopt(int(socket->socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0
        && peer.uid == ::geteuid();
#elif defined(Q_OS_WIN)
    const auto pipe=reinterpret_cast<HANDLE>(socket->socketDescriptor());
    DWORD flags=0;
    return pipe && pipe!=INVALID_HANDLE_VALUE && GetNamedPipeInfo(pipe,&flags,nullptr,nullptr,nullptr)
        && societyBackendSameWindowsUser(socket,(flags&PIPE_SERVER_END)!=0);
#else
    return false;
#endif
}

inline bool societyBackendPeerMatchesExecutable(QLocalSocket *socket, const QString &expected)
{
    const auto pid = societyBackendPeerPid(socket);
    if (pid <= 0) return false;
    QString actual;
#if defined(Q_OS_DARWIN)
    QByteArray buffer(4096, '\0');
    const int count = ::proc_pidpath(int(pid), buffer.data(), uint32_t(buffer.size()));
    if (count <= 0) return false;
    actual = QFile::decodeName(buffer.constData());
#elif defined(Q_OS_LINUX)
    actual = QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(pid));
#elif defined(Q_OS_WIN)
    const auto path=society::peer::processImagePath(DWORD(pid));
    if (path.empty()) return false;
    actual=QString::fromStdWString(path);
#else
    Q_UNUSED(expected); return false;
#endif
    auto expectedPath=expected;
#ifdef Q_OS_WIN
    if (!QFileInfo::exists(expectedPath) && !expectedPath.endsWith(QStringLiteral(".exe"),Qt::CaseInsensitive)) expectedPath+=".exe";
#endif
    const auto target = QFileInfo(expectedPath).canonicalFilePath();
#ifdef Q_OS_WIN
    return !target.isEmpty() && QFileInfo(actual).canonicalFilePath().compare(target,Qt::CaseInsensitive)==0;
#else
    return !target.isEmpty() && QFileInfo(actual).canonicalFilePath() == target;
#endif
}

#ifdef Q_OS_WIN
#include <QByteArray>
#include <QString>

// Named pipes may be reached remotely and may have multiple listeners. Verify
// both the peer's machine and token instead of trusting the pipe name alone.
inline bool societyBackendSameWindowsUser(QLocalSocket *socket, bool serverSide)
{
    const auto pipe = reinterpret_cast<HANDLE>(socket->socketDescriptor());
    if (!pipe || pipe == INVALID_HANDLE_VALUE) return false;
    if (serverSide) {
        wchar_t peerName[256]{}, localName[MAX_COMPUTERNAME_LENGTH + 1]{};
        DWORD localLength = MAX_COMPUTERNAME_LENGTH + 1;
        if (!GetNamedPipeClientComputerNameW(pipe, peerName, sizeof(peerName))) {
            if (GetLastError() != ERROR_PIPE_LOCAL) return false;
        } else {
            if (!GetComputerNameW(localName, &localLength)) return false;
            auto peer = QString::fromWCharArray(peerName);
            while (peer.startsWith(QLatin1Char('\\'))) peer.remove(0, 1);
            if (peer.compare(QString::fromWCharArray(localName), Qt::CaseInsensitive) != 0) return false;
        }
    }
    ULONG pid = 0;
    if (!(serverSide ? GetNamedPipeClientProcessId(pipe, &pid) : GetNamedPipeServerProcessId(pipe, &pid)) || !pid) return false;
    struct ScopedHandle {
        HANDLE value = nullptr;
        ~ScopedHandle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    } process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)}, current, other;
    if (!process.value || !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &current.value)
        || !OpenProcessToken(process.value, TOKEN_QUERY, &other.value)) return false;
    const auto tokenUser = [](HANDLE token) {
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        QByteArray data(int(size), '\0');
        if (!size || !GetTokenInformation(token, TokenUser, data.data(), size, &size)) return QByteArray{};
        return data;
    };
    const auto own = tokenUser(current.value), theirs = tokenUser(other.value);
    if (own.isEmpty() || theirs.isEmpty()) return false;
    return EqualSid(reinterpret_cast<const TOKEN_USER *>(own.constData())->User.Sid,
                    reinterpret_cast<const TOKEN_USER *>(theirs.constData())->User.Sid);
}
#endif
