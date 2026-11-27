#pragma once
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <functional>

// A GUI adapter for the local application catalogue. No inferred license grants.
class EnvironmentAppsModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList apps READ apps NOTIFY changed)
public:
    // A distribution channel applies only to its matching device platform.
    enum class DistributionPlatform { Other, WindowsStore, MacOSArm64 };

    // Construct the live catalogue using the packaged distribution settings
    // and the current operating system's URL opener.
    explicit EnvironmentAppsModel(QObject *parent = nullptr);
    // Construct a catalogue with an injected distribution and URL opener;
    // this allows callers to verify dispatch without opening an external app.
    EnvironmentAppsModel(const QByteArray &distribution, bool windows,
                         std::function<bool(const QUrl &)> openStore, QObject *parent = nullptr);
    // Inject the platform, URL opener and installed-bundle lookup independently
    // so release routing can be verified without launching applications.
    EnvironmentAppsModel(const QByteArray &windowsDistribution, const QByteArray &macosDistribution,
                         DistributionPlatform platform, std::function<bool(const QUrl &)> openUrl,
                         std::function<QString(const QString &)> installedAppPath, QObject *parent = nullptr);
    // Return the current device's observed catalogue snapshot.
    QVariantList apps() const { return m_apps; }
    // Refresh installation observations and available distribution actions.
    Q_INVOKABLE void refresh();
    // Open an installed app or its published distribution when available.
    Q_INVOKABLE bool open(const QString &id);
    // Return a validated macOS release only for a currently uninstalled product.
    // The returned values grant no authority to install or update the product.
    Q_INVOKABLE QVariantMap installRelease(const QString &id);
signals:
    void changed();
    void societyRequested();
private:
    // A published package is distinct from an installed app or a license grant.
    struct MacOSPackage {
        QUrl url;
        QString version;
        QString minimumSystemVersion;
        QByteArray sha256;
        qint64 sizeBytes = 0;
    };
    static QHash<QString, MacOSPackage> macosPackages(const QByteArray &distribution);

    QVariantList m_apps;
    QHash<QString, QUrl> m_storeLinks;
    QHash<QString, MacOSPackage> m_macosPackages;
    DistributionPlatform m_platform = DistributionPlatform::Other;
    std::function<bool(const QUrl &)> m_openUrl;
    std::function<QString(const QString &)> m_installedAppPath;
};
