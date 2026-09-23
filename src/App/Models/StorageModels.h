#pragma once
#include <StorageModelCatalog.h>
#include <QtQml/qqmlregistration.h>

// QML registration only; metadata browsing and explicit downloads belong to SDK.
class StorageModels : public iiSocietyContainer::StorageModelCatalog {
    Q_OBJECT
    QML_ELEMENT
public:
    using iiSocietyContainer::StorageModelCatalog::StorageModelCatalog;
};
