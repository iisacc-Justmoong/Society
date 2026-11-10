#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// The Tasks shell owns navigation; the calibrator itself is an LVUI/iiGUI app.
class ModelCalibratorLauncher : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
public:
    explicit ModelCalibratorLauncher(QObject *parent = nullptr) : QObject(parent) {}
    [[nodiscard]] QString errorString() const { return error_; }
    Q_INVOKABLE bool open(const QString &modelsDirectory);
signals:
    void errorChanged();
private:
    QString error_;
};
