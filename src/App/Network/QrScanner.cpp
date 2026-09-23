#include "QrScanner.h"
#include <QGuiApplication>
#include <QQuickWindow>

QrScanner::QrScanner(QObject *parent) : QObject(parent) {
    if (qGuiApp) connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationSuspended || state == Qt::ApplicationHidden) stop();
    });
}
QrScanner::~QrScanner() { stop(); }
bool QrScanner::available() const {
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    return true;
#else
    return false;
#endif
}
void QrScanner::start(QObject *object) {
    if (m_active) return;
    m_error.clear(); m_denied = false;
    auto *window = qobject_cast<QQuickWindow *>(object);
    if (!window || !available()) { m_error = tr("Scan the desktop QR code with the mobile Society app."); emit changed(); return; }
    m_active = true; emit changed();
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    startNative(window);
#endif
}
void QrScanner::stop() {
    const bool wasActive = m_active; m_active = false;
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    stopNative();
#endif
    if (wasActive) emit changed();
}
void QrScanner::captured(const QString &text) {
    if (!m_active) return;
    stop(); emit codeCaptured(text);
}
void QrScanner::failed(const QString &message, bool denied) {
    if (!m_active) return;
    stop(); m_error = message; m_denied = denied; emit changed();
}
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
void QrScanner::openSettings() {}
#endif
