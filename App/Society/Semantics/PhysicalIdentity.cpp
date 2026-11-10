#include "PhysicalIdentity.h"

#include <QFile>
#include <QFileInfo>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#else
#include <sys/stat.h>
#if defined(Q_OS_LINUX)
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#endif

namespace society::semantics {
std::optional<QString> physicalIdentity(const QString &path) {
    const QFileInfo info(path);
    if (path.isEmpty() || !info.exists() || info.isSymLink() || info.isJunction()) return {};
#ifdef Q_OS_DARWIN
    if (info.canonicalFilePath().normalized(QString::NormalizationForm_C) != path.normalized(QString::NormalizationForm_C)) return {};
#else
    if (info.canonicalFilePath() != path) return {};
#endif
#ifdef Q_OS_WIN
    const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    BY_HANDLE_FILE_INFORMATION value{};
    const auto ok = GetFileInformationByHandle(handle, &value); CloseHandle(handle);
    if (!ok || (value.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return {};
    return QStringLiteral("win:%1:%2:%3:%4:%5").arg(value.dwVolumeSerialNumber).arg(value.nFileIndexHigh)
        .arg(value.nFileIndexLow).arg(value.ftCreationTime.dwHighDateTime).arg(value.ftCreationTime.dwLowDateTime);
#else
    const auto native = QFile::encodeName(path);
    struct stat value{};
    if (::lstat(native.constData(), &value) != 0 || (!S_ISREG(value.st_mode) && !S_ISDIR(value.st_mode))) return {};
#ifdef Q_OS_DARWIN
    return QStringLiteral("posix:%1:%2:%3:%4").arg(qulonglong(value.st_dev)).arg(qulonglong(value.st_ino))
        .arg(qlonglong(value.st_birthtimespec.tv_sec)).arg(qlonglong(value.st_birthtimespec.tv_nsec));
#elif defined(Q_OS_LINUX) && defined(STATX_BTIME) && defined(SYS_statx)
    struct statx extended{};
    if (::syscall(SYS_statx, AT_FDCWD, native.constData(), AT_SYMLINK_NOFOLLOW, STATX_BTIME, &extended) != 0
        || !(extended.stx_mask & STATX_BTIME)) return {};
    return QStringLiteral("posix:%1:%2:%3:%4").arg(qulonglong(value.st_dev)).arg(qulonglong(value.st_ino))
        .arg(qlonglong(extended.stx_btime.tv_sec)).arg(qlonglong(extended.stx_btime.tv_nsec));
#else
    return {};
#endif
#endif
}
}
