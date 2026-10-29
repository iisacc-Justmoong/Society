#pragma once
#include <iiAccountManager/AccountManager.h>
#include <QFile>
#include <QDateTime>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>

// Explicit account cloud transfers for the shared Files section. Session
// credentials stay inside AccountManager; the S3 transport has no cookie jar.
class CloudFilesController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(iisacc::accounts::AccountManager* accountManager READ accountManager WRITE setAccountManager NOTIFY configurationChanged)
    Q_PROPERTY(QString containerPath READ containerPath WRITE setContainerPath NOTIFY configurationChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(double usedBytes READ usedBytes NOTIFY changed)
    Q_PROPERTY(double reservedBytes READ reservedBytes NOTIFY changed)
    Q_PROPERTY(double quotaBytes READ quotaBytes NOTIFY changed)
    Q_PROPERTY(QVariantList files READ files NOTIFY changed)
    Q_PROPERTY(QVariantList uploads READ uploads NOTIFY changed)
    Q_PROPERTY(QString nextCursor READ nextCursor NOTIFY changed)
public:
    explicit CloudFilesController(QObject* parent = nullptr);
    CloudFilesController(QNetworkAccessManager* transfers, QObject* parent);
    ~CloudFilesController() override;
    iisacc::accounts::AccountManager* accountManager() const { return m_account; }
    void setAccountManager(iisacc::accounts::AccountManager* value);
    QString containerPath() const { return m_container; }
    void setContainerPath(const QString& value);
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    double usedBytes() const { return m_quota.value("usedBytes").toDouble(); }
    double reservedBytes() const { return m_quota.value("reservedBytes").toDouble(); }
    double quotaBytes() const { return m_quota.value("quotaBytes").toDouble(); }
    QVariantList files() const { return m_files; }
    QVariantList uploads() const { return m_uploads; }
    QString nextCursor() const { return m_cursor; }
    Q_INVOKABLE bool refresh();
    Q_INVOKABLE bool loadMore();
    Q_INVOKABLE bool uploadFile(const QString& path, const QString& expectedRevision = {});
    Q_INVOKABLE bool downloadFile(const QString& path);
    Q_INVOKABLE bool removeFile(const QString& path, const QString& expectedRevision);
    Q_INVOKABLE bool cancelUpload(const QString& id);
    Q_INVOKABLE bool finishUpload(const QString& id);
    Q_INVOKABLE bool retryCleanup();
    static bool validRelativePath(const QString& value);
    static bool validS3Url(const QUrl& value);
signals:
    void configurationChanged();
    void changed();
    void downloaded(const QString& path);
private:
    using Continuation = std::function<void(const QJsonObject&)>;
    bool begin();
    void finish(const QString& message);
    void reset();
    void control(const QJsonObject& input, Continuation next);
    void list(const QString& after = {});
    void sendPart();
    void completeUpload();
    bool bound() const;
    QString localPath(const QString& path, bool existing);
    QPointer<iisacc::accounts::AccountManager> m_account;
    QPointer<QNetworkReply> m_reply;
    QNetworkAccessManager m_defaultS3;
    QPointer<QNetworkAccessManager> m_s3;
    QVariantMap m_binding;
    QString m_container, m_boundContainer, m_status, m_cursor, m_uploadId, m_relative;
    QJsonObject m_quota;
    QVariantList m_files, m_uploads;
    std::unique_ptr<QFile> m_source, m_temporary;
    QString m_temporaryPath;
    qint64 m_size = 0, m_received = 0;
    QDateTime m_sourceModified;
    int m_part = 0;
    bool m_busy = false;
    quint64 m_generation = 0;
};
