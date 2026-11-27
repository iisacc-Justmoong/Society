#include "InstallArtifact.h"

#include <QCryptographicHash>
#include <QIODevice>
#include <algorithm>

bool verifyInstallArtifact(QIODevice &input, qint64 expectedSize, const QByteArray &expectedSha256) {
    if (!input.isOpen() || !input.isReadable() || expectedSize <= 0 || expectedSha256.size() != 64 ||
        !std::all_of(expectedSha256.cbegin(), expectedSha256.cend(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 total = 0;
    while (!input.atEnd()) {
        const auto chunk = input.read(64 * 1024);
        if (chunk.isEmpty()) return false;
        if (chunk.size() > expectedSize - total) return false;
        total += chunk.size(); hash.addData(chunk);
    }
    return total == expectedSize && hash.result().toHex() == expectedSha256;
}
