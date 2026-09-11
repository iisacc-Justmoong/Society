#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>

// The account service provisions bounded daily seeds. Discovery derives its
// five-minute authentication keys locally; no login token is used on the LAN.
namespace SocietyPairingCredentials {
bool valid(const QJsonObject &credentials, QDateTime now = QDateTime::currentDateTimeUtc());
QByteArray key(const QJsonObject &credentials, qint64 epoch, QDateTime now = QDateTime::currentDateTimeUtc());
}
