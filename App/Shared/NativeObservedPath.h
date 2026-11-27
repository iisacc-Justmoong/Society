#pragma once

#include <QString>
#include <QtGlobal>
#include <filesystem>
#include <stdexcept>
#include <system_error>

// Resolve Qt's displayed path spelling to the same native filesystem entry.
// The caller must still use a no-follow SDK operation to reject redirects.
[[nodiscard]] inline std::filesystem::path societyNativeObservedPath(const QString &path) {
#ifdef Q_OS_MACOS
    const auto original=std::filesystem::path(path.toStdU16String());
    const auto decomposed=std::filesystem::path(path.normalized(QString::NormalizationForm_D).toStdU16String());
    if (original!=decomposed) {
        std::error_code error;
        const auto actual=std::filesystem::canonical(original,error);
        if (error || actual!=decomposed || !std::filesystem::equivalent(original,decomposed,error) || error)
            throw std::runtime_error("Cannot safely resolve this Unicode path spelling.");
    }
    return decomposed;
#elif defined(Q_OS_WIN)
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}
