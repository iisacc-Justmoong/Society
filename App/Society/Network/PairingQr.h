#pragma once
#include <QImage>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

class PairingQr : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY textChanged)
public:
    explicit PairingQr(QQuickItem *parent = nullptr) : QQuickPaintedItem(parent) { setAntialiasing(false); setSmooth(false); }
    QString text() const { return m_text; }
    bool valid() const { return !m_modules.isNull(); }
    void setText(const QString &text);
    void paint(QPainter *painter) override;
    static QImage encode(const QString &text, int modulePixels = 8);
signals:
    void textChanged();
private:
    QString m_text;
    QImage m_modules;
};
