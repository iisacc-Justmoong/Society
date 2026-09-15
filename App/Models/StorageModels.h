#pragma once

#include <QObject>
#include <QVariantMap>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <memory>

// The four presentation groups share the container's existing model inventory.
class StorageModels : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString directory READ directory WRITE setDirectory NOTIFY directoryChanged)
    Q_PROPERTY(QVariantMap groups READ groups NOTIFY modelsChanged)
    Q_PROPERTY(int count READ count NOTIFY modelsChanged)
    Q_PROPERTY(int uncategorizedCount READ uncategorizedCount NOTIFY modelsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY modelsChanged)
public:
    explicit StorageModels(QObject *parent = nullptr);
    ~StorageModels() override;
    QString directory() const { return m_directory; }
    void setDirectory(const QString &directory);
    QVariantMap groups() const { return m_groups; }
    int count() const { return m_count; }
    int uncategorizedCount() const { return m_uncategorized; }
    bool loading() const { return m_loading; }
    QString errorString() const { return m_error; }
    Q_INVOKABLE void refresh();
signals:
    void directoryChanged();
    void modelsAboutToChange();
    void modelsChanged();
    void loadingChanged();
private:
    QString m_directory, m_error;
    QVariantMap m_groups;
    int m_count = 0, m_uncategorized = 0;
    bool m_loading = false, m_refreshPending = false;
    quint64 m_revision = 0;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QFileSystemWatcher m_files;
    QTimer m_debounce;
};
