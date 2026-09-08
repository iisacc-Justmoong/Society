#include "SocietyInbox.h"
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(inboxLog, "iisacc.society.inbox")

SocietyInbox::SocietyInbox(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &SocietyInbox::refresh);
}
void SocietyInbox::setError(const QString &message)
{
    if (m_error == message) return;
    m_error = message;
    if (!message.isEmpty()) qCWarning(inboxLog).noquote() << message;
    emit statusChanged();
}
bool SocietyInbox::start(const QString &directory)
{
    stop();
    QString error;
    if (!m_store.open(directory, &error)) { setError(error); return false; }
    m_cursor = m_store.acknowledged("com.iisacc.society");
    if (m_cursor < 0) { setError(m_store.errorString()); m_store.close(); return false; }
    m_messages.clear();
    m_snapshot.clear();
    setError({});
    emit statusChanged();
    emit messagesChanged();
    m_timer.start();
    refresh();
    return true;
}
void SocietyInbox::stop()
{
    m_timer.stop();
    m_store.close();
    emit statusChanged();
}
void SocietyInbox::refresh()
{
    const auto messages = m_store.readAfter(m_cursor);
    if (!m_store.errorString().isEmpty()) { setError(m_store.errorString()); return; }
    setError({});
    for (const auto &entry : messages) {
        if (!m_timer.isActive()) return;
        const auto message = entry.toMap();
        m_cursor = message.value("sequence").toLongLong();
        m_messages.append(message);
        if (m_messages.size() > 100) m_messages.removeFirst();
        qCInfo(inboxLog).noquote() << "Society inbox received" << message.value("id").toString()
            << "from" << message.value("sender").toMap().value("applicationId").toString()
            << message.value("topic").toString();
        emit dataReceived(message);
    }
    if (!messages.isEmpty()) emit messagesChanged();
    const auto snapshot = m_store.daemonSnapshot();
    if (!m_store.errorString().isEmpty()) { setError(m_store.errorString()); return; }
    if (snapshot != m_snapshot) { m_snapshot = snapshot; emit snapshotChanged(); }
}
bool SocietyInbox::acknowledge(qint64 sequence)
{
    if (sequence < 0 || sequence > m_cursor) { setError(QStringLiteral("Cannot acknowledge unseen Society inbox data.")); return false; }
    const bool ok = m_store.acknowledge("com.iisacc.society", sequence);
    setError(m_store.errorString());
    return ok;
}
QVariantList SocietyInbox::readAfter(qint64 sequence, int limit)
{
    const auto result = m_store.readAfter(sequence, limit);
    setError(m_store.errorString());
    return result;
}
