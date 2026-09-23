#pragma once
#include <iiSocietyContainer/DashboardFiles.h>
#include <QtQml/qqmlregistration.h>

// QML registration only; both Society and Dreamscapes use the SDK snapshot.
class DashboardFiles : public iiSocietyContainer::DashboardFiles
{
    Q_OBJECT
    QML_ELEMENT
public:
    using iiSocietyContainer::DashboardFiles::DashboardFiles;
};
