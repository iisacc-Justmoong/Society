#include "CloudFilesController.h"
#include <SharedStorage.h>
#include <File.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkCookieJar>
#include <QNetworkCookie>
#include <QUuid>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace {
constexpr qint64 PartBytes = 8 * 1024 * 1024;
constexpr qint64 QuotaBytes = 5'000'000'000;
constexpr qint64 MaximumControlBytes = 256 * 1024;
std::filesystem::path nativePath(const QString& value) {
#ifdef Q_OS_WIN
    return std::filesystem::path(value.toStdWString());
#else
    return std::filesystem::path(value.toStdString());
#endif
}
class NoCookies final : public QNetworkCookieJar {
public:
    QList<QNetworkCookie> cookiesForUrl(const QUrl&) const override { return {}; }
    bool setCookiesFromUrl(const QList<QNetworkCookie>&, const QUrl&) override { return false; }
};
}

CloudFilesController::CloudFilesController(QObject* parent) : CloudFilesController(nullptr, parent) {}
CloudFilesController::CloudFilesController(QNetworkAccessManager* transfers, QObject* parent) : QObject(parent), m_s3(transfers ? transfers : &m_defaultS3) {
    m_s3->setCookieJar(new NoCookies);
}
CloudFilesController::~CloudFilesController() { reset(); }

bool CloudFilesController::validRelativePath(const QString& value) {
    if (value.isEmpty() || value.toUtf8().size() > 1024 || value != value.normalized(QString::NormalizationForm_C)) return false;
    for (const auto& part : value.split('/')) {
        if (part.isEmpty() || part == "." || part == ".." || part.toUtf8().size() > 255) return false;
        for (const auto ch : part)
            if (ch == '\\' || ch == ':' || ch.category() == QChar::Other_Control || ch.category() == QChar::Other_Format) return false;
    }
    return true;
}

bool CloudFilesController::validS3Url(const QUrl& url) {
    const auto host = url.host().toLower();
    return url.isValid() && url.scheme() == "https" && url.port(443) == 443 && url.userInfo().isEmpty()
        && !url.hasFragment() && host.endsWith(".amazonaws.com")
        && (host.contains(".s3.") || host.contains(".s3-"));
}

void CloudFilesController::setAccountManager(iisacc::accounts::AccountManager* value) {
    if (m_account == value) return;
    if (m_account) disconnect(m_account, nullptr, this, nullptr);
    reset(); m_account = value;
    if (value) {
        connect(value, &iisacc::accounts::AccountManager::authenticatedChanged, this, [this] { reset(); });
        connect(value, &iisacc::accounts::AccountManager::sessionEnding, this, [this] { reset(); });
        connect(value, &iisacc::accounts::AccountManager::loginSessionChanged, this, [this] { if (!bound()) reset(); });
        connect(value, &QObject::destroyed, this, [this] { reset(); });
    }
    emit configurationChanged();
}
void CloudFilesController::setContainerPath(const QString& value) {
    if (m_container == value) return;
    reset(); m_container = value; emit configurationChanged();
}
void CloudFilesController::reset() {
    ++m_generation;
    if (m_reply) { m_reply->abort(); m_reply->deleteLater(); m_reply = nullptr; }
    m_source.reset(); m_temporary.reset();
    if (!m_temporaryPath.isEmpty()) {
        try { iiFileProvider::File::remove(m_temporaryPath); } catch (const std::exception&) {}
        m_temporaryPath.clear();
    }
    m_busy = false; m_binding.clear(); m_quota = {}; m_files.clear(); m_uploads.clear(); m_cursor.clear(); m_uploadId.clear(); m_status.clear();
    emit changed();
}
bool CloudFilesController::begin() {
    if (m_busy) return false;
    if (!m_account || !m_account->isAuthenticated()) { finish(tr("Sign in to Society to use your 5 GB cloud storage.")); return false; }
    m_binding = m_account->sessionBinding();
    if (m_binding.isEmpty()) return false;
    m_boundContainer = m_container;
    ++m_generation; m_busy = true; m_status.clear(); emit changed(); return true;
}
bool CloudFilesController::bound() const {
    return m_account && m_account->matchesSessionBinding(m_binding) && m_container == m_boundContainer;
}
void CloudFilesController::finish(const QString& message) {
    m_busy = false; m_status = message; m_source.reset(); m_temporary.reset();
    if (!m_temporaryPath.isEmpty()) {
        try { iiFileProvider::File::remove(m_temporaryPath); } catch (const std::exception&) {}
        m_temporaryPath.clear();
    }
    emit changed();
}
void CloudFilesController::control(const QJsonObject& input, Continuation next) {
    if (!bound()) { reset(); return; }
    const auto wire = QJsonDocument(QJsonObject{{"query", "mutation SocietyFiles($input: JSON!) { societyFiles(input: $input) }"},
        {"variables", QJsonObject{{"input", input}}}}).toJson(QJsonDocument::Compact);
    auto* reply = m_account->postAuthenticated(m_account->serviceUrl().resolved(QUrl("/Account/GraphQL")), wire);
    if (!reply) { finish(tr("Cloud storage is unavailable for this session.")); return; }
    m_reply = reply;
    const auto generation = m_generation;
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, bytes] {
        *bytes += reply->readAll(); if (bytes->size() > MaximumControlBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, bytes, next = std::move(next)] {
        *bytes += reply->readAll(); reply->deleteLater();
        if (generation != m_generation) return;
        m_reply = nullptr;
        if (!bound()) { reset(); return; }
        const auto root = QJsonDocument::fromJson(*bytes).object();
        const auto errors = root.value("errors").toArray();
        const auto result = root.value("data").toObject().value("societyFiles");
        if (bytes->size() > MaximumControlBytes || reply->error() != QNetworkReply::NoError || !errors.isEmpty() || !result.isObject()) {
            const auto extensions = errors.isEmpty() ? QJsonObject{} : errors.first().toObject().value("extensions").toObject();
            const auto code = extensions.value("code").toString();
            finish(code == "files_quota_exceeded" ? tr("Your 5 GB cloud storage is full.")
                : code == "files_conflict" ? tr("The cloud file changed. Refresh before replacing it.")
                : tr("Cloud request failed. Refresh to check pending uploads before retrying."));
            return;
        }
        next(result.toObject());
    });
}
bool CloudFilesController::refresh() {
    if (!begin()) return false;
    control({{"operation", "status"}}, [this](const QJsonObject& row) {
        m_quota = row; m_uploads = row.value("uploads").toArray().toVariantList();
        list();
    });
    return true;
}
void CloudFilesController::list(const QString& after) {
    control({{"operation", "list"}, {"after", after}}, [this, after](const QJsonObject& row) {
        if (after.isEmpty()) m_files.clear();
        m_files += row.value("files").toArray().toVariantList(); m_cursor = row.value("nextCursor").toString();
        m_quota = row.value("quota").toObject(); finish(tr("5 GB cloud storage · Society.societycontainer/Files"));
    });
}
bool CloudFilesController::loadMore() {
    if (m_cursor.isEmpty() || !begin()) return false;
    list(m_cursor); return true;
}
QString CloudFilesController::localPath(const QString& path, bool existing) {
    if (m_container.isEmpty() || !validRelativePath(path)) return {};
    const auto storage = iiSocietyContainer::SharedStorage::open(m_container);
    if (!storage) return {};
    const auto native = storage->filePath(iiSocietyContainer::StoreSection::Files, path);
    const QFileInfo info(native);
    if (native.isEmpty() || info.isSymLink() || (existing && !info.isFile()) || (!existing && info.exists())) return {};
    return native;
}
bool CloudFilesController::uploadFile(const QString& path, const QString& expectedRevision) {
    if (!begin()) return false;
    const auto native = localPath(path, true);
    m_source = std::make_unique<QFile>(native);
    if (native.isEmpty() || !m_source->open(QIODevice::ReadOnly) || m_source->size() > QuotaBytes) {
        finish(tr("Choose a resident file in Society.societycontainer/Files, at most 5 GB.")); return false;
    }
    m_size = m_source->size(); m_relative = path; m_part = 0;
    m_sourceModified = QFileInfo(native).lastModified();
    m_uploadId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    control({{"operation", "begin"}, {"uploadId", m_uploadId}, {"path", path}, {"size", m_size}, {"expectedRevision", expectedRevision}},
        [this](const QJsonObject& row) {
            if (row.value("state") != "uploading" || row.value("partBytes").toInteger() != PartBytes) {
                finish(tr("Refresh cloud storage to check the pending upload.")); return;
            }
            sendPart();
        });
    return true;
}
void CloudFilesController::sendPart() {
    if (!bound()) { reset(); return; }
    if (!m_source || m_source->size() != m_size || localPath(m_relative, true) != m_source->fileName()
        || QFileInfo(m_source->fileName()).lastModified() != m_sourceModified) {
        finish(tr("The local file changed. Cancel the pending cloud upload before retrying.")); return;
    }
    auto bytes = std::make_shared<QByteArray>(m_source->read(PartBytes));
    const auto expected = qMin(PartBytes, m_size - qint64(m_part) * PartBytes);
    if (bytes->size() != expected) { finish(tr("The local file could not be read completely.")); return; }
    ++m_part;
    const auto checksum = QString::fromLatin1(QCryptographicHash::hash(*bytes, QCryptographicHash::Sha256).toBase64());
    control({{"operation", "part"}, {"uploadId", m_uploadId}, {"partNumber", m_part}, {"checksumSHA256", checksum}},
        [this, bytes](const QJsonObject& row) {
            const QUrl url(row.value("url").toString());
            if (!validS3Url(url) || row.value("size").toInteger() != bytes->size()) { finish(tr("Cloud upload authorization is invalid.")); return; }
            QNetworkRequest request(url);
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
            request.setTransferTimeout(120000);
            const auto headers = row.value("headers").toObject();
            for (auto it = headers.begin(); it != headers.end(); ++it) {
                const auto name = it.key().toLower();
                if (name == "content-length" || name == "x-amz-checksum-sha256" || name == "host") request.setRawHeader(it.key().toLatin1(), it.value().toString().toUtf8());
                else { finish(tr("Cloud upload headers are invalid.")); return; }
            }
            if (!m_s3) { finish(tr("Cloud transfer is unavailable.")); return; }
            auto* reply = m_s3->put(request, *bytes); m_reply = reply;
            const auto generation = m_generation;
            connect(reply, &QNetworkReply::finished, this, [this, reply, generation, bytes] {
                reply->deleteLater();
                if (generation != m_generation) return;
                m_reply = nullptr;
                if (!bound()) { reset(); return; }
                if (reply->error() != QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
                    finish(tr("Upload interrupted. Refresh to cancel or finish the pending upload.")); return;
                }
                if (qint64(m_part) * PartBytes >= m_size) completeUpload(); else sendPart();
            });
        });
}
void CloudFilesController::completeUpload() {
    if (m_source && (m_source->size() != m_size || QFileInfo(m_source->fileName()).lastModified() != m_sourceModified)) {
        finish(tr("The local file changed. Cancel the pending cloud upload before retrying.")); return;
    }
    control({{"operation", "complete"}, {"uploadId", m_uploadId}}, [this](const QJsonObject& row) {
        m_quota = row.value("quota").toObject(); m_uploadId.clear(); finish(tr("File uploaded.")); refresh();
    });
}
bool CloudFilesController::finishUpload(const QString& id) {
    if (!begin()) return false;
    m_uploadId = id; completeUpload(); return true;
}
bool CloudFilesController::cancelUpload(const QString& id) {
    if (!begin()) return false;
    control({{"operation", "cancel"}, {"uploadId", id}}, [this](const QJsonObject&) { finish(tr("Upload cancelled.")); refresh(); }); return true;
}
bool CloudFilesController::retryCleanup() {
    if (!begin()) return false;
    control({{"operation", "cleanup"}}, [this](const QJsonObject&) { finish(tr("Cleanup checked.")); refresh(); }); return true;
}
bool CloudFilesController::removeFile(const QString& path, const QString& expectedRevision) {
    if (!begin()) return false;
    control({{"operation", "delete"}, {"path", path}, {"expectedRevision", expectedRevision}},
        [this](const QJsonObject&) { finish(tr("Cloud file removed.")); refresh(); }); return true;
}
bool CloudFilesController::downloadFile(const QString& path) {
    if (!begin()) return false;
    const auto destination = localPath(path, false);
    if (destination.isEmpty()) { finish(tr("Choose a cloud file whose local folder exists and whose local name is unused.")); return false; }
    control({{"operation", "download"}, {"path", path}}, [this, path, destination](const QJsonObject& row) {
        const auto size = row.value("file").toObject().value("size").toInteger(-1);
        const QUrl url(row.value("url").toString());
        if (size < 0 || size > QuotaBytes || !validS3Url(url)) { finish(tr("Cloud download authorization is invalid.")); return; }
        m_size = size; m_received = 0;
        // Stage beside the destination; final publication is atomic and cannot
        // replace an existing file or follow a redirected Files ancestor.
        m_temporaryPath = QFileInfo(destination).absolutePath() + "/.society-cloud-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        try { iiFileProvider::File::create(m_temporaryPath, {}); }
        catch (const std::exception&) { finish(tr("The local staging file could not be created.")); return; }
        m_temporary = std::make_unique<QFile>(m_temporaryPath);
        if (!m_temporary->open(QIODevice::WriteOnly)) { finish(tr("The local file could not be opened.")); return; }
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(120000);
        if (!m_s3) { finish(tr("Cloud transfer is unavailable.")); return; }
        auto* reply = m_s3->get(request); m_reply = reply;
        const auto generation = m_generation;
        const auto consume = [this, reply, generation] {
            if (generation != m_generation || !m_temporary) return;
            while (reply->bytesAvailable() > 0) {
                const auto bytes = reply->read(1024 * 1024); m_received += bytes.size();
                if (!bound() || m_received > m_size || m_temporary->write(bytes) != bytes.size()) { reply->abort(); return; }
            }
        };
        reply->setReadBufferSize(1024 * 1024);
        connect(reply, &QNetworkReply::readyRead, this, consume);
        connect(reply, &QNetworkReply::finished, this, [this, reply, generation, consume, path, destination] {
            consume(); reply->deleteLater();
            if (generation != m_generation) return;
            m_reply = nullptr;
            if (!bound()) { reset(); return; }
            if (reply->error() != QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 || m_received != m_size
                || !m_temporary->flush() || localPath(path, false) != destination) {
                finish(tr("Download failed; existing local files were preserved.")); return;
            }
            m_temporary.reset();
            const auto result = iiFileProvider::File::publish(nativePath(m_temporaryPath), nativePath(destination), false);
            if (!result.succeeded) { finish(tr("The file could not be published without replacing a local file.")); return; }
            m_temporaryPath.clear();
            auto* watcher = new QFutureWatcher<bool>(this);
            connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, generation, destination] {
                const auto recorded = watcher->result(); watcher->deleteLater();
                if (generation != m_generation) return;
                finish(recorded ? tr("Cloud file downloaded into Files.") : tr("File downloaded. Refresh Files to update its catalog."));
                emit downloaded(destination);
            });
            watcher->setFuture(QtConcurrent::run([container = m_container, path] {
                try {
                    const auto storage = iiSocietyContainer::SharedStorage::open(container);
                    return storage && storage->recordFile(iiSocietyContainer::StoreSection::Files, path);
                } catch (const std::exception&) { return false; }
            }));
        });
    });
    return true;
}
