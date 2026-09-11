#include "MobileSyncActivity.h"
#include <QCoreApplication>
#include <QJniObject>

void societySetSyncScreenActive(bool active) {
    const auto activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid() && QNativeInterface::QAndroidApplication::isActivityContext())
        activity.callMethod<void>("setSyncScreenActive", jboolean(active));
}
