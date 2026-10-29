#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <QFutureWatcher>
#include <QtQml/qqmlregistration.h>

// Validates the view's native path before Qt's folder model is instantiated.
class DirectoryLocation : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(QUrl folderUrl READ folderUrl NOTIFY pathChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY pathChanged)
    Q_PROPERTY(QVariantMap details READ details NOTIFY detailsChanged)
    Q_PROPERTY(bool inspecting READ inspecting NOTIFY detailsChanged)

public:
    explicit DirectoryLocation(QObject *parent = nullptr);
    ~DirectoryLocation() override;

    QString path() const;
    void setPath(const QString &path);
    QUrl folderUrl() const;
    QString errorString() const;
    Q_INVOKABLE QVariantMap fileDetails(const QString &path) const;
    Q_INVOKABLE void inspectFile(const QString &path, const QString &revision = {});
    QVariantMap details() const { return m_details; }
    bool inspecting() const { return m_inspecting; }

signals:
    void pathChanged();
    void detailsChanged();

private:
    QString m_path;
    QUrl m_folderUrl;
    QString m_errorString;
    QVariantMap m_details;
    quint64 m_inspection = 0;
    bool m_inspecting = false;
    QString m_requestedPath;
    QString m_requestedRevision;
    QFutureWatcher<QVariantMap> *m_inspectionWatcher = nullptr;
    void startInspection();
};

#include <StorageDirectoryModel.h>
#include <FileActions.h>
class FileActions : public iiSocietyContainer::FileActions {
    Q_OBJECT
    QML_ELEMENT
public:
    using iiSocietyContainer::FileActions::FileActions;
};
class StorageDirectoryModel : public iiSocietyContainer::StorageDirectoryModel {
    Q_OBJECT
    QML_ELEMENT
public:
    using iiSocietyContainer::StorageDirectoryModel::StorageDirectoryModel;
};
