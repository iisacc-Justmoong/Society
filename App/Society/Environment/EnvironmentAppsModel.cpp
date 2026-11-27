#include "EnvironmentAppsModel.h"
#include <QDesktopServices>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QDir>
#include <filesystem>
#include <array>
#include <cmath>
#include <utility>

namespace {
// Read the packaged, release-controlled distribution catalogue. Failure is
// closed: an absent or malformed catalogue grants no download action.
QByteArray packagedStoreDistribution()
{
    QFile file(QStringLiteral(":/iisacc/distribution/windows-store.json"));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

// An unavailable or malformed packaged catalogue must not enable downloads.
QByteArray packagedMacOSDistribution()
{
    QFile file(QStringLiteral(":/iisacc/distribution/macos-arm64.json"));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

// Accept only published product pages on Microsoft's Store domain. Arbitrary
// web URLs, redirects, credentials and unknown catalogue formats are refused.
QHash<QString, QUrl> storeLinks(const QByteArray &data)
{
    const auto root = QJsonDocument::fromJson(data).object();
    if (root.value(QStringLiteral("schema")).toInt() != 1 || !root.value(QStringLiteral("products")).isArray()) return {};
    const QRegularExpression pathPattern(QStringLiteral("^/detail/[A-Z0-9]{12}$"));
    QHash<QString, QUrl> links;
    for (const auto &value : root.value(QStringLiteral("products")).toArray()) {
        const auto item = value.toObject();
        if (!item.value(QStringLiteral("published")).isBool() || !item.value(QStringLiteral("published")).toBool()) continue;
        const auto id = item.value(QStringLiteral("id")).toString();
        const QUrl url(item.value(QStringLiteral("url")).toString(), QUrl::StrictMode);
        if (id.isEmpty() || !url.isValid() || url.scheme() != QStringLiteral("https")
            || url.host() != QStringLiteral("apps.microsoft.com") || url.port() != -1
            || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()
            || !pathPattern.match(url.path()).hasMatch() || links.contains(id)) continue;
        links.insert(id, url);
    }
    return links;
}

// Detect only unambiguous, locally installed products. Generic product names
// cannot safely be matched to unrelated applications from other publishers.
QString installedAppPath(const QString &name)
{
#if defined(Q_OS_MACOS)
    for (const auto &base : {QStringLiteral("/Applications"), QDir::homePath() + "/Applications"}) {
        const auto candidate = base + '/' + name + ".app";
        std::error_code ec;
        if (std::filesystem::is_regular_file((candidate + "/Contents/MacOS/" + name).toStdString(), ec))
            return candidate;
    }
#else
    Q_UNUSED(name);
#endif
    return {};
}

// Only a native Apple Silicon build advertises the matching macOS packages.
EnvironmentAppsModel::DistributionPlatform nativePlatform()
{
#if defined(Q_OS_WIN)
    return EnvironmentAppsModel::DistributionPlatform::WindowsStore;
#elif defined(Q_OS_MACOS) && defined(Q_PROCESSOR_ARM_64)
    return EnvironmentAppsModel::DistributionPlatform::MacOSArm64;
#else
    return EnvironmentAppsModel::DistributionPlatform::Other;
#endif
}
}

// Accept only complete, published package records whose URL is confined to the
// official host and to the recorded product, platform and version directory.
QHash<QString, EnvironmentAppsModel::MacOSPackage> EnvironmentAppsModel::macosPackages(const QByteArray &data)
{
    const auto root = QJsonDocument::fromJson(data).object();
    if (root.value(QStringLiteral("schema")).toInt() != 1
        || root.value(QStringLiteral("platform")).toString() != QStringLiteral("macos-arm64")
        || !root.value(QStringLiteral("products")).isArray()) return {};

    const QRegularExpression idPattern(QStringLiteral("^[a-z][a-z0-9-]{0,63}$"));
    const QRegularExpression versionPattern(QStringLiteral("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$"));
    const QRegularExpression systemPattern(QStringLiteral("^[0-9]+\\.[0-9]+(\\.[0-9]+)?$"));
    const QRegularExpression hashPattern(QStringLiteral("^[a-f0-9]{64}$"));
    QHash<QString, MacOSPackage> packages;
    for (const auto &value : root.value(QStringLiteral("products")).toArray()) {
        const auto item = value.toObject();
        if (!item.value(QStringLiteral("published")).isBool() || !item.value(QStringLiteral("published")).toBool()) continue;
        const auto id = item.value(QStringLiteral("id")).toString();
        const auto version = item.value(QStringLiteral("version")).toString();
        const auto minimum = item.value(QStringLiteral("minimumSystemVersion")).toString();
        const auto hash = item.value(QStringLiteral("sha256")).toString();
        const auto sizeValue = item.value(QStringLiteral("sizeBytes"));
        const auto size = sizeValue.toDouble();
        const QUrl url(item.value(QStringLiteral("url")).toString(), QUrl::StrictMode);
        if (!idPattern.match(id).hasMatch() || !versionPattern.match(version).hasMatch()
            || !systemPattern.match(minimum).hasMatch() || !hashPattern.match(hash).hasMatch()
            || !sizeValue.isDouble() || size <= 0 || size > 9007199254740991.0 || std::floor(size) != size
            || !url.isValid() || url.scheme() != QStringLiteral("https")
            || url.host() != QStringLiteral("downloads.iisacc.com") || url.port() != -1
            || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() || packages.contains(id)) continue;
        const QRegularExpression pathPattern(QStringLiteral("^/archive/%1/macos-arm64/%2/[A-Za-z0-9][A-Za-z0-9._-]*\\.pkg$")
            .arg(QRegularExpression::escape(id), QRegularExpression::escape(version)));
        if (!pathPattern.match(url.path(QUrl::FullyEncoded)).hasMatch()) continue;
        packages.insert(id, MacOSPackage{url, version, minimum, hash.toLatin1(), static_cast<qint64>(size)});
    }
    return packages;
}

EnvironmentAppsModel::EnvironmentAppsModel(QObject *parent)
    : EnvironmentAppsModel(packagedStoreDistribution(), packagedMacOSDistribution(), nativePlatform(),
                           [](const QUrl &url) { return QDesktopServices::openUrl(url); }, installedAppPath, parent) {}

EnvironmentAppsModel::EnvironmentAppsModel(const QByteArray &distribution, bool windows,
                                           std::function<bool(const QUrl &)> openStore, QObject *parent)
    : EnvironmentAppsModel(distribution, {}, windows ? DistributionPlatform::WindowsStore : DistributionPlatform::Other,
                           std::move(openStore), installedAppPath, parent) {}

EnvironmentAppsModel::EnvironmentAppsModel(const QByteArray &windowsDistribution, const QByteArray &macosDistribution,
                                           DistributionPlatform platform, std::function<bool(const QUrl &)> openUrl,
                                           std::function<QString(const QString &)> appPath, QObject *parent)
    : QObject(parent), m_storeLinks(storeLinks(windowsDistribution)), m_macosPackages(macosPackages(macosDistribution)),
      m_platform(platform), m_openUrl(std::move(openUrl)), m_installedAppPath(std::move(appPath))
{
    refresh();
}

void EnvironmentAppsModel::refresh()
{
    struct App { const char *id, *name, *symbol, *description; };
    constexpr std::array catalogue {
        App{"society", "Society", "So", "Keep your files and devices together."},
        App{"dreamscapes", "Dreamscapes", "Dr", "Create images and explore visual ideas."},
        App{"congregation", "Congregation", "Co", "Paint and animate on your native canvas."},
        App{"canvas", "Canvas", "Ca", "Arrange layouts, boards and design assets."},
        App{"motion", "Motion", "Mo", "Edit short videos and motion studies."},
        App{"notes", "Notes", "No", "Capture notes and keep project references."},
        App{"studio", "Studio", "St", "A shared workspace for creative projects."},
        App{"render", "Render", "Re", "Prepare and process media on your devices."},
        App{"library", "Library", "Li", "Collect and organize reusable assets."}
    };
    QVariantList next;
    for (size_t i = 0; i < catalogue.size(); ++i) {
        const auto &app = catalogue[i];
        const auto id = QString::fromLatin1(app.id);
        const auto name = QString::fromLatin1(app.name);
        QString path;
        if (m_platform != DistributionPlatform::WindowsStore && m_installedAppPath
            && (id == QStringLiteral("dreamscapes") || id == QStringLiteral("congregation")))
            path = m_installedAppPath(name);
        const bool installed = i == 0 || !path.isEmpty();
        const bool storeAvailable = m_platform == DistributionPlatform::WindowsStore && m_storeLinks.contains(id);
        const bool downloadAvailable = m_platform == DistributionPlatform::MacOSArm64 && m_macosPackages.contains(id);
        const auto package = m_macosPackages.value(id);
        const auto status = installed ? tr("Installed · This device") : downloadAvailable
            ? tr("Not installed · %1 · macOS %2+").arg(package.version, package.minimumSystemVersion) : tr("Not installed");
        next.append(QVariantMap{{"id", app.id}, {"name", app.name}, {"symbol", app.symbol},
            {"description", app.description}, {"publisher", "iisacc"}, {"palette", int(i % 6)},
            {"installed", installed}, {"path", path},
            {"status", status},
            {"license", i == 0 ? tr("Included with your account") : tr("License not verified")},
            {"action", installed ? tr("Open") : downloadAvailable ? tr("Download") : tr("Install")},
            {"actionEnabled", installed || storeAvailable || downloadAvailable}});
        // Keep the installed application's Open action, while exposing its
        // independently downloadable package as a clearly identified entry.
        if (installed && downloadAvailable) {
            next.append(QVariantMap{{"id", id + QStringLiteral(".installer")}, {"distributionId", id},
                {"name", tr("%1 installer").arg(name)}, {"symbol", app.symbol}, {"publisher", "iisacc"},
                {"description", tr("Download the %1 installer for Apple silicon.").arg(name)},
                {"palette", int(i % 6)}, {"installed", false}, {"path", QString{}},
                {"status", tr("%1 · macOS %2+ · Apple silicon").arg(package.version, package.minimumSystemVersion)},
                {"license", tr("License not verified")}, {"action", tr("Download")}, {"actionEnabled", true}});
        }
    }
    if (next != m_apps) { m_apps = next; emit changed(); }
}

bool EnvironmentAppsModel::open(const QString &id)
{
    refresh();
    for (const auto &value : m_apps) {
        const auto app = value.toMap();
        if (app.value("id").toString() != id) continue;
        if (app.value("installed").toBool()) {
            if (id == "society") { emit societyRequested(); return true; }
            return m_openUrl && m_openUrl(QUrl::fromLocalFile(app.value("path").toString()));
        }
        const auto productId = app.value("distributionId", id).toString();
        const auto link = m_platform == DistributionPlatform::WindowsStore ? m_storeLinks.value(productId)
            : m_platform == DistributionPlatform::MacOSArm64 ? m_macosPackages.value(productId).url : QUrl{};
        return !link.isEmpty() && m_openUrl && m_openUrl(link);
    }
    return false;
}

QVariantMap EnvironmentAppsModel::installRelease(const QString &id)
{
    if (m_platform != DistributionPlatform::MacOSArm64 || !m_macosPackages.contains(id)) return {};
    refresh();
    for (const auto &value : m_apps) {
        const auto app = value.toMap();
        if (app.value("id").toString() != id || app.value("installed").toBool()) continue;
        const auto release = m_macosPackages.value(id);
        return {{"id", id}, {"name", app.value("name")}, {"url", release.url}, {"version", release.version},
                {"minimumSystemVersion", release.minimumSystemVersion}, {"sha256", release.sha256},
                {"sizeBytes", release.sizeBytes}};
    }
    return {};
}
