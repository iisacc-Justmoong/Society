#pragma once

#include <QCryptographicHash>
#include <QDir>
#include <QString>

inline bool societySameBackendPath(const QString &left, const QString &right)
{
    if (left.isEmpty() || right.isEmpty()) return false;
    const auto a = QDir::cleanPath(QDir::fromNativeSeparators(left));
    const auto b = QDir::cleanPath(QDir::fromNativeSeparators(right));
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

// Unix uses a private filesystem socket. Windows named pipes accept a name,
// not a filesystem path; derive a stable per-user name from the same root.
inline QString societyBackendAddress(const QString &directory)
{
#ifdef Q_OS_WIN
    const auto normalized = QDir::cleanPath(QDir::fromNativeSeparators(directory)).toCaseFolded().toUtf8();
    return QStringLiteral("iisacc-society-backend-")
        + QString::fromLatin1(QCryptographicHash::hash(normalized, QCryptographicHash::Sha256).toHex());
#else
    return QDir(directory).filePath(QStringLiteral("backend.sock"));
#endif
}
