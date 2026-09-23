#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// Validates the view's native path before Qt's folder model is instantiated.
class DirectoryLocation : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(QUrl folderUrl READ folderUrl NOTIFY pathChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY pathChanged)

public:
    explicit DirectoryLocation(QObject *parent = nullptr);

    QString path() const;
    void setPath(const QString &path);
    QUrl folderUrl() const;
    QString errorString() const;
    Q_INVOKABLE QVariantMap fileDetails(const QString &path) const;

signals:
    void pathChanged();

private:
    QString m_path;
    QUrl m_folderUrl;
    QString m_errorString;
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
