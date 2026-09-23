#include "MobileSyncActivity.h"
#include <QCoreApplication>
#include <QJniObject>
#include <QJniEnvironment>

namespace {
constexpr auto serviceClass = "com/iisacc/society/SocietySyncService";
std::function<void()> expiration;
jlong generation = 0;
void syncExpired(JNIEnv *, jclass, jlong token) {
    QMetaObject::invokeMethod(qApp, [token] {
        if (token == generation && expiration) { const auto callback = expiration; callback(); }
    }, Qt::QueuedConnection);
}
}
bool societyBeginBackgroundSync(std::function<void()> expired) {
    QJniEnvironment env;
    static const bool registered = env.registerNativeMethods(serviceClass, {
        {"syncExpired", "(J)V", reinterpret_cast<void *>(syncExpired)}
    });
    const auto context = QNativeInterface::QAndroidApplication::context();
    if (!registered || !context.isValid() || !QNativeInterface::QAndroidApplication::isActivityContext()) return false;
    const auto token = ++generation; expiration = std::move(expired);
    const auto started = QJniObject::callStaticMethod<jboolean>(serviceClass, "start",
        "(Landroid/content/Context;J)Z", context.object(), token);
    if (env.checkAndClearExceptions() || !started) { expiration = {}; return false; }
    return true;
}
void societyEndBackgroundSync() {
    ++generation; expiration = {};
    const auto context = QNativeInterface::QAndroidApplication::context();
    if (context.isValid()) QJniObject::callStaticMethod<void>(serviceClass, "stop", "(Landroid/content/Context;)V", context.object());
    QJniEnvironment env; env.checkAndClearExceptions();
}

void societySetSyncScreenActive(bool active) {
    const auto activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid() && QNativeInterface::QAndroidApplication::isActivityContext())
        activity.callMethod<void>("setSyncScreenActive", jboolean(active));
}
