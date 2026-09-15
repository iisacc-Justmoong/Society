#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Navigation identities and snapshots are independent of their storage/network providers.
class StorageNavigation : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString accountId READ accountId WRITE setAccountId NOTIFY inputsChanged)
    Q_PROPERTY(QString currentDeviceId READ currentDeviceId WRITE setCurrentDeviceId NOTIFY inputsChanged)
    Q_PROPERTY(QVariantList rememberedDevices READ rememberedDevices WRITE setRememberedDevices NOTIFY inputsChanged)
    Q_PROPERTY(QVariantList nearbyDevices READ nearbyDevices WRITE setNearbyDevices NOTIFY inputsChanged)
    Q_PROPERTY(QVariantList hosts READ hosts WRITE setHosts NOTIFY inputsChanged)
    Q_PROPERTY(QString currentSection READ currentSection WRITE setCurrentSection NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedSection READ selectedSection NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList sections READ sections CONSTANT)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList guilds READ guilds NOTIFY membershipsChanged)
    Q_PROPERTY(QVariantList organizations READ organizations NOTIFY membershipsChanged)
public:
    explicit StorageNavigation(QObject *parent = nullptr);
    QString accountId() const { return m_accountId; }
    QString currentDeviceId() const { return m_currentDeviceId; }
    QVariantList rememberedDevices() const { return m_remembered; }
    QVariantList nearbyDevices() const { return m_nearby; }
    QVariantList hosts() const { return m_hosts; }
    QString currentSection() const { return m_currentSection; }
    QString selectedSection() const;
    QVariantList sections() const { return m_sections; }
    QVariantList devices() const { return m_devices; }
    QVariantList guilds() const { return m_guilds; }
    QVariantList organizations() const { return m_organizations; }
    void setAccountId(const QString &id);
    void setCurrentDeviceId(const QString &id);
    void setRememberedDevices(const QVariantList &devices);
    void setNearbyDevices(const QVariantList &devices);
    void setHosts(const QVariantList &hosts);
    void setCurrentSection(const QString &section);
    Q_INVOKABLE void replaceDevices(const QString &accountId, const QString &currentDeviceId,
                                   const QVariantList &remembered, const QVariantList &nearby,
                                   const QVariantList &hosts);
    // A membership provider supplies a complete, account-scoped {id, name} snapshot.
    // Profile author affiliation is not a storage membership or an access grant.
    Q_INVOKABLE bool replaceMemberships(const QString &accountId, const QVariantList &guilds,
                                       const QVariantList &organizations);
    Q_INVOKABLE bool activate(const QString &group, const QString &id);
signals:
    void inputsChanged();
    void selectionChanged();
    void devicesChanged();
    void membershipsChanged();
    void sectionRequested(QString key);
    void deviceRequested(QString id, QString name, QString peerId);
    void workspaceRequested(QString kind, QString id, QString name);
private:
    void updateDevices();
    QString m_accountId, m_currentDeviceId, m_currentSection;
    QVariantList m_sections, m_remembered, m_nearby, m_hosts, m_devices, m_guilds, m_organizations;
};
