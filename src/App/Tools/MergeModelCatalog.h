#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <memory>

// Lists model inputs without loading tensors or invoking the Python runtime.
class MergeModelCatalog : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString directory READ directory WRITE setDirectory NOTIFY directoryChanged)
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY modelsChanged)
public:
    explicit MergeModelCatalog(QObject *parent = nullptr);
    ~MergeModelCatalog() override;
    QString directory() const { return m_directory; }
    void setDirectory(const QString &directory);
    QVariantList models() const { return m_models; }
    bool loading() const { return m_loading; }
    QString errorString() const { return m_error; }
    // Resolve a single embedded checkpoint without flattening cascades or adapters.
    static QString checkpointPath(const QString &path);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool contains(const QString &path, bool baseOnly = false) const;
    Q_INVOKABLE QString outputDirectory(const QString &basePath) const;
signals:
    void directoryChanged();
    void modelsChanged();
    void loadingChanged();
private:
    QString m_directory, m_error;
    QVariantList m_models;
    bool m_loading = false;
    quint64 m_revision = 0;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
