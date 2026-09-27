#pragma once
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// A GUI adapter for the local application catalogue. No inferred license grants.
class EnvironmentAppsModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList apps READ apps NOTIFY changed)
public:
    explicit EnvironmentAppsModel(QObject *parent = nullptr);
    QVariantList apps() const { return m_apps; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool open(const QString &id);
signals:
    void changed();
    void societyRequested();
private:
    QVariantList m_apps;
};
