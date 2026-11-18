#pragma once

#include "NotificationStore.h"
#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

// Temporary Qt-facing adapter for the existing shell. The notification state
// itself is independent of Qt and can be shared with the LVUI view model.
class NotificationCenter : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QAbstractItemModel *entries READ entries CONSTANT)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    explicit NotificationCenter(QObject *parent = nullptr);
    [[nodiscard]] QAbstractItemModel *entries() { return this; }
    [[nodiscard]] int count() const;
    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void record(const QVariant &source, const QVariant &message);
    Q_INVOKABLE void dismiss(int index);
    Q_INVOKABLE void clear();

signals:
    void countChanged();

private:
    enum Role { SourceRole = Qt::UserRole + 1, MessageRole };
    society::notifications::NotificationStore m_store;
};
