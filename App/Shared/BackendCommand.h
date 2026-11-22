#pragma once

#include <QJsonObject>
#include <QString>

struct SocietyBackendCommandReply {
    QJsonObject response;
    QString error;
    bool uncertain = false;
    [[nodiscard]] bool ok() const { return error.isEmpty() && response.value("accepted").toBool(); }
};

// Blocking only for the local acknowledgement. Call from a CLI or a worker,
// never from the GUI thread. A queued result is not a completed file operation.
[[nodiscard]] SocietyBackendCommandReply societyBackendCommand(
    const QString &brokerDirectory, const QString &operation, QJsonObject arguments = {},
    const QString &expectedContainer = {}, int timeoutMs = 10000);

/// Submit a Space request after its transcript loads; mutations are never replayed.
[[nodiscard]] SocietyBackendCommandReply societySpaceCommand(
    const QString &brokerDirectory, const QString &operation, QJsonObject arguments,
    const QString &expectedContainer = {}, int timeoutMs = 10000);

// Up to 200 entries from one revision-bound backend directory session.
// Call off the GUI and backend event-loop threads.
[[nodiscard]] SocietyBackendCommandReply societyBackendDirectoryPage(
    const QString &brokerDirectory, const QString &path, int offset, int limit, int timeoutMs = 15000,
    const QString &expectedContainer = {});
