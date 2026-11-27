#pragma once

#include <QByteArray>
#include <QtGlobal>
class QIODevice;

/// Verify a freshly reopened installer stream against the catalogue's exact
/// size and lowercase SHA-256. The caller owns and positions the readable
/// device. The function has no filesystem or network authority and does not
/// trust a hash computed only before the file was committed to storage.
[[nodiscard]] bool verifyInstallArtifact(QIODevice &input, qint64 expectedSize, const QByteArray &expectedSha256);
