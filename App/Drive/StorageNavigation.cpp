#include "StorageNavigation.h"
#include <SocietyDrive.h>
#include <QMap>
#include <QSet>
#include <algorithm>

namespace {
QString deviceIcon(const QString &kind) {
    if (kind == "phone") return "iPhoneDevice";
    if (kind == "tablet") return "nodesdataColumn";
    return "screens";
}
int deviceOrder(const QString &kind) {
    if (kind == "desktop") return 0;
    if (kind == "phone") return 1;
    if (kind == "tablet") return 2;
    return 3;
}
QVariantList memberships(const QVariantList &input, const QString &icon) {
    QVariantList result;
    QSet<QString> ids;
    for (const auto &value : input) {
        const auto row = value.toMap();
        const auto id = row.value("id").toString().trimmed();
        const auto name = row.value("name").toString().trimmed();
        if (id.isEmpty() || id.size() > 256 || name.isEmpty() || ids.contains(id)) continue;
        ids.insert(id);
        result.append(QVariantMap{{"id", id}, {"name", name}, {"icon", icon}});
    }
    return result;
}
}

StorageNavigation::StorageNavigation(QObject *parent) : QObject(parent) {
    using iiSocietyContainer::StoreSection;
    struct Definition { StoreSection section; const char *label; const char *icon; };
    const QList<Definition> order{
        {StoreSection::Files, QT_TR_NOOP("Files"), "nodesfolder"},
        {StoreSection::Photos, QT_TR_NOOP("Photos"), "fileTypesimage"},
        {StoreSection::AssetLibrary, QT_TR_NOOP("Asset Library"), "nodesfolder"},
        {StoreSection::GenerationHistory, QT_TR_NOOP("Generation History"), "fileTypesimage"},
        {StoreSection::Models, QT_TR_NOOP("Models"), "warehouse"},
        {StoreSection::ThinkingSpace, QT_TR_NOOP("Thinking Space"), "fileTypestext"},
        {StoreSection::Forked, QT_TR_NOOP("Forked"), "RemoteChanges"},
        {StoreSection::Published, QT_TR_NOOP("Published"), "application"},
        {StoreSection::Deleted, QT_TR_NOOP("Deleted"), "generaldelete"}};
    for (const auto &[section, label, icon] : order) {
        const auto directory = iiSocietyContainer::storeSectionName(section);
        m_sections.append(QVariantMap{{"id", iiSocietyContainer::storeSectionKey(section)},
            {"name", tr(label)}, {"directory", directory}, {"icon", icon}});
    }
}

void StorageNavigation::setAccountId(const QString &id) {
    if (m_accountId == id) return;
    replaceDevices(id, m_currentDeviceId, {}, {}, {});
}
void StorageNavigation::replaceDevices(const QString &accountId, const QString &currentDeviceId,
                                       const QVariantList &remembered, const QVariantList &nearby,
                                       const QVariantList &hosts) {
    if (m_accountId == accountId && m_currentDeviceId == currentDeviceId && m_remembered == remembered
        && m_nearby == nearby && m_hosts == hosts) return;
    const bool accountChanged = m_accountId != accountId;
    m_accountId = accountId; m_currentDeviceId = currentDeviceId;
    m_remembered = remembered; m_nearby = nearby; m_hosts = hosts;
    if (accountChanged && (!m_guilds.isEmpty() || !m_organizations.isEmpty())) {
        m_guilds.clear(); m_organizations.clear(); emit membershipsChanged();
    }
    updateDevices(); emit inputsChanged();
}
void StorageNavigation::setCurrentDeviceId(const QString &id) {
    if (m_currentDeviceId == id) return;
    m_currentDeviceId = id; updateDevices(); emit inputsChanged();
}
void StorageNavigation::setRememberedDevices(const QVariantList &devices) {
    if (m_remembered == devices) return;
    m_remembered = devices; updateDevices(); emit inputsChanged();
}
void StorageNavigation::setNearbyDevices(const QVariantList &devices) {
    if (m_nearby == devices) return;
    m_nearby = devices; updateDevices(); emit inputsChanged();
}
void StorageNavigation::setHosts(const QVariantList &hosts) {
    if (m_hosts == hosts) return;
    m_hosts = hosts; updateDevices(); emit inputsChanged();
}
void StorageNavigation::setCurrentSection(const QString &section) {
    if (m_currentSection == section) return;
    m_currentSection = section; emit selectionChanged();
}
QString StorageNavigation::selectedSection() const {
    for (const auto &value : m_sections) {
        const auto row = value.toMap();
        if (row.value("directory").toString() == m_currentSection) return row.value("id").toString();
    }
    return {};
}

void StorageNavigation::updateDevices() {
    QMap<QString, QVariantMap> merged;
    const auto add = [&](const QVariantMap &input, bool nearby, const QString &peerId) {
        const auto id = input.value("id").toString().trimmed();
        if (id.isEmpty() || id == m_currentDeviceId) return;
        auto row = merged.value(id, {{"id", id}, {"name", tr("Device")}, {"kind", "desktop"},
            {"online", false}, {"connected", false}, {"peerId", ""}});
        const auto name = input.value("name").toString().trimmed();
        const auto kind = input.value("kind").toString();
        if (!name.isEmpty()) row.insert("name", name);
        if (!kind.isEmpty()) row.insert("kind", kind);
        row.insert("online", row.value("online").toBool() || nearby || !peerId.isEmpty());
        row.insert("connected", row.value("connected").toBool() || input.value("connected").toBool() || !peerId.isEmpty());
        if (!peerId.isEmpty()) row.insert("peerId", peerId);
        row.insert("icon", deviceIcon(row.value("kind").toString()));
        merged.insert(id, row);
    };
    if (!m_accountId.isEmpty()) {
        for (const auto &value : m_remembered) add(value.toMap(), false, {});
        for (const auto &value : m_nearby) {
            const auto row = value.toMap();
            if (row.value("verified").toBool() || row.value("connected").toBool()) add(row, true, {});
        }
    }
    // Authenticated transport hosts also cover manual connections without an account session.
    for (const auto &value : m_hosts) {
        auto row = value.toMap();
        const auto peerId = row.value("peerId").toString();
        if (peerId.isEmpty()) continue;
        const auto deviceId = row.value("metadata").toMap().value("deviceId").toString();
        row.insert("id", deviceId.isEmpty() ? peerId : deviceId);
        add(row, true, peerId);
    }
    QVariantList result;
    for (auto row : merged) {
        row.insert("status", row.value("connected").toBool() ? tr("Connected")
            : row.value("online").toBool() ? tr("Nearby") : tr("Offline"));
        result.append(row);
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        const auto left = a.toMap(), right = b.toMap();
        const auto leftOrder = deviceOrder(left.value("kind").toString());
        const auto rightOrder = deviceOrder(right.value("kind").toString());
        if (leftOrder != rightOrder) return leftOrder < rightOrder;
        const auto comparison = QString::compare(left.value("name").toString(), right.value("name").toString(), Qt::CaseInsensitive);
        return comparison == 0 ? left.value("id").toString() < right.value("id").toString() : comparison < 0;
    });
    if (result == m_devices) return;
    m_devices = result; emit devicesChanged();
}

bool StorageNavigation::replaceMemberships(const QString &accountId, const QVariantList &guilds,
                                           const QVariantList &organizations) {
    if (accountId.isEmpty() || accountId != m_accountId) return false;
    const auto nextGuilds = memberships(guilds, "option");
    const auto nextOrganizations = memberships(organizations, "warehouse");
    if (m_guilds == nextGuilds && m_organizations == nextOrganizations) return true;
    m_guilds = nextGuilds; m_organizations = nextOrganizations; emit membershipsChanged(); return true;
}

bool StorageNavigation::activate(const QString &group, const QString &id) {
    const auto rows = group == "storage" ? m_sections : group == "devices" ? m_devices
        : group == "guild" ? m_guilds : group == "organization" ? m_organizations : QVariantList{};
    for (const auto &value : rows) {
        const auto row = value.toMap();
        if (row.value("id").toString() != id) continue;
        if (group == "storage") emit sectionRequested(id);
        else if (group == "devices") emit deviceRequested(id, row.value("name").toString(), row.value("peerId").toString());
        else emit workspaceRequested(group, id, row.value("name").toString());
        return true;
    }
    return false;
}
