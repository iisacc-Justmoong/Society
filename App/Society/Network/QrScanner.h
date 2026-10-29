#pragma once
#include <QObject>
#include <QtQml/qqmlregistration.h>
class QQuickWindow;

class QrScanner : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool permissionDenied READ permissionDenied NOTIFY changed)
    Q_PROPERTY(QString errorString READ errorString NOTIFY changed)
public:
    explicit QrScanner(QObject *parent = nullptr);
    ~QrScanner() override;
    bool available() const;
    bool active() const { return m_active; }
    bool permissionDenied() const { return m_denied; }
    QString errorString() const { return m_error; }
    Q_INVOKABLE void start(QObject *window);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void openSettings();
    // Native callbacks are delivered on the Qt/UI thread, and ignored after cancellation.
    void captured(const QString &text);
    void failed(const QString &message, bool denied = false);
signals:
    void changed();
    void codeCaptured(QString text);
private:
    bool m_active = false, m_denied = false;
    QString m_error;
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    void startNative(QQuickWindow *window);
    void stopNative();
#endif
#ifdef Q_OS_IOS
    void *m_native = nullptr;
#endif
};
