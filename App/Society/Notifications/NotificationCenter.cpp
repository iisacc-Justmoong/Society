#include "NotificationCenter.h"

#include <QStringList>
#include <utility>

NotificationCenter::NotificationCenter(QObject *parent) : QAbstractListModel(parent) {
    setObjectName(QStringLiteral("notificationCenter"));
}

int NotificationCenter::count() const { return static_cast<int>(m_store.entries().size()); }
int NotificationCenter::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : count(); }

QVariant NotificationCenter::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.parent().isValid() || index.row() < 0 || index.row() >= count()) return {};
    const auto &entry = m_store.entries()[static_cast<std::size_t>(index.row())];
    if (role == SourceRole) return QString::fromStdString(entry.source);
    if (role == MessageRole) return QString::fromStdString(entry.message);
    return {};
}

QHash<int, QByteArray> NotificationCenter::roleNames() const {
    return {{SourceRole, "source"}, {MessageRole, "message"}};
}

void NotificationCenter::record(const QVariant &source, const QVariant &message) {
    std::vector<std::string> lines;
    for (const auto &part : message.toString().split(QLatin1Char('\n'))) {
        const auto trimmed = part.trimmed();
        if (!trimmed.isEmpty()) lines.push_back(trimmed.toStdString());
    }
    auto next = m_store;
    next.record(source.toString().toStdString(), std::move(lines));
    const int before = count();
    const int after = static_cast<int>(next.entries().size());
    if (after > before) beginInsertRows({}, before, after - 1);
    m_store = std::move(next);
    if (after > before) { endInsertRows(); emit countChanged(); }
}

void NotificationCenter::dismiss(int index) {
    if (index < 0 || index >= count()) return;
    auto next = m_store;
    if (!next.dismiss(static_cast<std::size_t>(index))) return;
    beginRemoveRows({}, index, index);
    m_store = std::move(next);
    endRemoveRows();
    emit countChanged();
}

void NotificationCenter::clear() {
    if (count() == 0) return;
    auto next = m_store;
    next.clear();
    beginResetModel();
    m_store = std::move(next);
    endResetModel();
    emit countChanged();
}
