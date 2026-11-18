#pragma once

#include <iiSocietyContainer>
#include <QJsonObject>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <optional>

// A client-scoped view of the SDK-owned, asynchronous directory model.
// Paths are logical locators; they are not asset identities.
class NativeDirectorySession final : public QObject {
    Q_OBJECT
public:
    explicit NativeDirectorySession(QObject *parent = nullptr);
    bool open(const QString &container, const QJsonObject &arguments, QString *error);
    void invalidate();
    bool valid() const { return m_drive.has_value(); }
    QJsonObject state() const;
    QJsonObject page(const QJsonObject &arguments, QString *error) const;
    bool activate(const QJsonObject &arguments, QString *error);
    std::optional<QPair<QByteArray, QJsonObject>> cachedReply(const QString &id) const;
    void rememberReply(const QString &id, const QByteArray &request, const QJsonObject &reply);
signals:
    void changed(const QJsonObject &directory);
    void openResult(const QJsonObject &result);
private:
    struct Activation { QString id, generation, revision, relative, absolute; };
    void scheduleChanged();
    iiSocietyContainer::StorageDirectoryModel m_model;
    std::optional<iiSocietyContainer::SocietyDrive> m_drive;
    QString m_container, m_path, m_generation, m_revision;
    QString m_query, m_sort{"name"};
    bool m_descending = false;
    QHash<QString, QPair<QByteArray, QJsonObject>> m_replies;
    QHash<QString, qsizetype> m_replySizes;
    QQueue<QString> m_replyOrder;
    qsizetype m_replyBytes = 0;
    bool m_notificationPending = false;
    std::optional<Activation> m_activation;
};
