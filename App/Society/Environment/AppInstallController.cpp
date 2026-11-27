#include "AppInstallController.h"
#include "InstallArtifact.h"
#include "InstallWorkflow.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPromise>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QUrl>
#include <QVersionNumber>
#include <QtConcurrentRun>

#if defined(SOCIETY_NATIVE_INSTALLER_AVAILABLE)
#include <iiNetwork/Http.h>
#include <iiUpdateManager/NativeInstaller.h>
#include <iiSDKCore/Runtime/EventLoop.h>
#include <chrono>
#include <filesystem>
#endif

namespace {
struct HandoffResult final { QString message; };

#if defined(SOCIETY_NATIVE_INSTALLER_AVAILABLE)
// These paths are the exact destinations observed by the existing catalogue.
// Refuse even an incomplete bundle so a first install cannot silently become
// an update of a running or partially installed application.
bool targetPresent(const QString &name) {
    for (const auto &base : {QStringLiteral("/Applications"), QDir::homePath() + QStringLiteral("/Applications")}) {
        const QFileInfo candidate(base + QLatin1Char('/') + name + QStringLiteral(".app"));
        if (candidate.exists() || candidate.isSymLink()) return true;
    }
    return false;
}

// Execute on the controller's worker. The validated catalogue value and promise
// are owned for the call; failures delete the private staging directory. A
// successful installer handoff retains it because the OS may read it later.
HandoffResult install(const QVariantMap &release, QPromise<HandoffResult> &promise) {
    const auto name = release.value("name").toString();
    const auto id = release.value("id").toString();
    const auto version = release.value("version").toString();
    const auto url = release.value("url").toUrl();
    const auto expectedSize = release.value("sizeBytes").toLongLong();
    const auto expectedHash = release.value("sha256").toByteArray();
    const auto minimum = QVersionNumber::fromString(release.value("minimumSystemVersion").toString());
    const auto current = QVersionNumber::fromString(QSysInfo::productVersion());
    const QRegularExpression idPattern(QStringLiteral("^[a-z][a-z0-9-]{0,63}$"));
    const QRegularExpression namePattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9 ._-]{0,127}$"));
    const QRegularExpression versionPattern(QStringLiteral("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$"));
    const QRegularExpression pathPattern(QStringLiteral("^/archive/%1/macos-arm64/%2/[A-Za-z0-9][A-Za-z0-9._-]*\\.pkg$")
        .arg(QRegularExpression::escape(id), QRegularExpression::escape(version)));
    if (!idPattern.match(id).hasMatch() || !namePattern.match(name).hasMatch() ||
        !url.isValid() || url.scheme() != QLatin1String("https") || url.host() != QLatin1String("downloads.iisacc.com") ||
        url.port() != -1 || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() ||
        !versionPattern.match(version).hasMatch() || !pathPattern.match(url.path(QUrl::FullyEncoded)).hasMatch() ||
        expectedSize <= 0 || expectedSize > 1024ll * 1024 * 1024 || minimum.isNull() || current.isNull())
        return {QObject::tr("The release information is invalid.")};
    if (QVersionNumber::compare(current, minimum) < 0) return {QObject::tr("This app requires macOS %1 or later.").arg(minimum.toString())};
    if (targetPresent(name)) return {QObject::tr("The app is already present. Refresh the catalogue before continuing.")};
    const auto cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cache.isEmpty() || !QDir().mkpath(cache)) return {QObject::tr("The installer cache is unavailable.")};
    QTemporaryDir directory(cache + QStringLiteral("/society-install-XXXXXX"));
    if (!directory.isValid() || !QFile::setPermissions(directory.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
        return {QObject::tr("A private installer directory could not be created.")};
    const auto path = directory.filePath(QStringLiteral("installer.pkg"));
    QSaveFile file(path); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return {QObject::tr("The installer file could not be created.")};
    const auto packagePath = std::filesystem::path(path.toUtf8().constData());
    promise.setProgressRange(0, 100);
    FirstInstallPorts ports;
    ports.targetPresent = [&] { return targetPresent(name); };
    ports.downloadAndCheck = [&]() -> QString {
        if (promise.isCanceled()) return QObject::tr("Installation was cancelled.");
        promise.setProgressValueAndText(0, QObject::tr("Downloading %1…").arg(name));
        ii::runtime::EventLoop loop;
        ii::network::Http client(loop);
        ii::network::HttpRequest request;
        request.url = url.toString(QUrl::FullyEncoded).toStdString();
        request.maximum_bytes = static_cast<std::size_t>(expectedSize);
        request.follow_redirects = false;
        request.timeout = std::chrono::minutes(5);
        request.connect_timeout = std::chrono::seconds(30);
        request.inactivity_timeout = std::chrono::seconds(15);
        bool accepted = false, writeFailed = false, finished = false;
        qint64 received = 0;
        ii::network::HttpResponse response;
        request.received_headers = [&](long status, const ii::network::HeaderFields &) { accepted = status == 200; return accepted; };
        request.consume = [&](std::string_view bytes) {
            if (promise.isCanceled() || !accepted || bytes.size() > static_cast<std::size_t>(expectedSize - received)) return false;
            const auto size = static_cast<qint64>(bytes.size());
            if (file.write(bytes.data(), size) != size) { writeFailed = true; return false; }
            received += size;
            promise.setProgressValueAndText(int(received * 100 / expectedSize), QObject::tr("Downloading %1…").arg(name));
            return true;
        };
        const auto started = client.request(std::move(request), [&](ii::network::HttpResponse result) {
            response = std::move(result); finished = true; loop.request_stop();
        });
        if (!started) return QObject::tr("The download could not be started.");
        if (!finished) loop.run();
        if (!finished || !response.ok() || response.status != 200 || !accepted || writeFailed || received != expectedSize)
            return QObject::tr("The installer download failed or was incomplete.");
        if (!file.commit()) return QObject::tr("The downloaded installer could not be saved.");
        promise.setProgressValueAndText(100, QObject::tr("Verifying %1…").arg(name));
        QFile stored(path);
        if (!stored.open(QIODevice::ReadOnly) || !verifyInstallArtifact(stored, expectedSize, expectedHash))
            return QObject::tr("The downloaded installer did not match its release checksum.");
        return {};
    };
    ports.verifyIdentity = [&]() -> QString {
        if (promise.isCanceled()) return QObject::tr("Installation was cancelled.");
        ii::runtime::EventLoop loop;
        iisacc::updates::native::Installer verifier(loop);
        bool complete = false, okay = false;
        QString failure;
        verifier.verify(packagePath, id.toStdString(), version.toStdString(), [&](bool valid, std::string detail) {
            okay = valid; failure = QString::fromStdString(detail); complete = true; loop.request_stop();
        });
        if (!complete) loop.run();
        return okay ? QString{} : failure.isEmpty() ? QObject::tr("The installer identity could not be verified.") : failure;
    };
    ports.handoff = [&]() -> QString {
        if (promise.isCanceled()) return QObject::tr("Installation was cancelled.");
        promise.setProgressValueAndText(100, QObject::tr("Opening the system installer…"));
        ii::runtime::EventLoop loop;
        iisacc::updates::native::Installer launcher(loop);
        bool complete = false, okay = false;
        QString failure;
        launcher.launch(packagePath, [&](bool opened, std::string detail) {
            okay = opened; failure = QString::fromStdString(detail); complete = true; loop.request_stop();
        });
        if (!complete) loop.run();
        return okay ? QString{} : failure.isEmpty() ? QObject::tr("The system installer could not be opened.") : failure;
    };
    const auto result = runFirstInstall(ports);
    if (!result.handedOff) return {result.error};
    // Installer.app can read after this process returns; retain this private
    // verified package rather than deleting an in-use handoff file.
    directory.setAutoRemove(false);
    return {QObject::tr("Installer opened. Complete installation in the system installer, then return to Society.")};
}
#else
HandoffResult install(const QVariantMap &, QPromise<HandoffResult> &) {
    return {QObject::tr("Native installation is unavailable on this platform.")};
}
#endif
}

AppInstallController::AppInstallController(QObject *parent) : QObject(parent) { pool_.setMaxThreadCount(1); }
AppInstallController::~AppInstallController() { if (watcher_) watcher_->cancel(); }
bool AppInstallController::start(const QVariantMap &release) {
    if (busy_) return false;
    busy_ = true;
    auto *watcher = new QFutureWatcher<HandoffResult>(this);
    watcher_ = watcher;
    connect(watcher, &QFutureWatcher<HandoffResult>::progressTextChanged, this, [this](const QString &text) { emit activityChanged(text, true); });
    connect(watcher, &QFutureWatcher<HandoffResult>::progressValueChanged, this, [this, watcher](int value) {
        const auto text = watcher->progressText();
        if (!text.isEmpty()) emit activityChanged(tr("%1 %2%").arg(text).arg(value), true);
    });
    connect(watcher, &QFutureWatcher<HandoffResult>::finished, this, [this, watcher] {
        busy_ = false;
        watcher_ = nullptr;
        emit activityChanged(watcher->result().message, false);
        emit handoffFinished();
        watcher->deleteLater();
    });
    emit activityChanged(tr("Preparing installer…"), true);
    watcher->setFuture(QtConcurrent::run(&pool_, [release](QPromise<HandoffResult> &promise) {
        try { promise.addResult(install(release, promise)); }
        catch (const std::exception &) { promise.addResult({QObject::tr("The installation task failed unexpectedly.")}); }
    }));
    return true;
}
