#include "QrScanner.h"
#include <QCoreApplication>
#include <QHash>
#include <QJniEnvironment>
#include <QJniObject>
#include <QPointer>

namespace {
constexpr char activityClass[] = "com/iisacc/society/SocietyActivity";
QHash<jlong, QPointer<QrScanner>> scanners;
jlong nextRequest = 0;
void qrFinished(JNIEnv *, jclass, jlong request, jstring code, jstring error, jboolean denied) {
    const auto text = QJniObject(code).toString(), message = QJniObject(error).toString();
    QMetaObject::invokeMethod(qApp, [request, text, message, denied] {
        auto scanner = scanners.take(request);
        if (!scanner || !scanner->active()) return;
        if (!message.isEmpty()) scanner->failed(message, denied);
        else if (!text.isEmpty()) scanner->captured(text);
        else scanner->stop();
    }, Qt::QueuedConnection);
}
}
void QrScanner::startNative(QQuickWindow *) {
    QJniEnvironment env;
    static const bool registered = env.registerNativeMethods(activityClass, {
        {"qrFinished", "(JLjava/lang/String;Ljava/lang/String;Z)V", reinterpret_cast<void *>(qrFinished)}
    });
    const auto activity = QNativeInterface::QAndroidApplication::context();
    if (!registered || !activity.isValid() || !QNativeInterface::QAndroidApplication::isActivityContext()) {
        failed(tr("Could not open the QR camera.")); return;
    }
    const auto request = ++nextRequest; scanners.insert(request, this);
    activity.callMethod<void>("startQrScan", request);
    if (env.checkAndClearExceptions()) { scanners.remove(request); failed(tr("Could not open the QR camera.")); }
}
void QrScanner::stopNative() {
    const auto activity = QNativeInterface::QAndroidApplication::context();
    for (const auto request : scanners.keys()) if (scanners.value(request) == this) {
        scanners.remove(request);
        if (activity.isValid()) activity.callMethod<void>("stopQrScan", request);
    }
}
void QrScanner::openSettings() {
    const auto activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) activity.callMethod<void>("openCameraSettings");
}
