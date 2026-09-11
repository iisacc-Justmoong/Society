#pragma once
#include <QByteArray>
// AccountManager permits a 192 KiB snapshot; retain room for paired devices.
inline constexpr qsizetype MaximumSocietyStateBytes = 256 * 1024;
QByteArray sealSocietyState(const QByteArray &plain, const QByteArray &key, const QByteArray &context);
QByteArray openSocietyState(const QByteArray &sealed, const QByteArray &key, const QByteArray &context);
