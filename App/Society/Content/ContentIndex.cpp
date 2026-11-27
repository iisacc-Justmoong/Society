#include "ContentIndex.h"
#include "Shared/NativeObservedPath.h"
#include "Society/Resources/ResourceInspection.h"

#include <iiFileProviderDatabase.h>
#include <iiFileProviderNativeDevice.h>
#include <ObjectSource.h>
#include <DirectoryMetadataIndex.h>
#include <iiSocietyContainer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSet>
#include <QTemporaryFile>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <limits>
#include <optional>
#include <stdexcept>
#include <variant>
#include <vector>

namespace society::content {
namespace {
namespace sql=iiFileProvider::native::sql;
constexpr std::int64_t applicationId=0x53434958;
constexpr int schemaVersion=1;
constexpr qint64 maximumTextBytes=64*1024;
constexpr qsizetype chunkCharacters=512;
constexpr qsizetype chunkOverlap=64;
constexpr qsizetype maximumDimensions=4096;
constexpr int maximumScanChunks=10000;

// A stable format rejection is retried only when a later observed revision is
// queued. Model, storage, cancellation and concurrency failures remain retryable.
class UnsupportedText final : public std::runtime_error {
public:
    explicit UnsupportedText(const char *message) : std::runtime_error(message) {}
};

// Convert provider failures into ordinary exceptions at this internal boundary.
template<class T> T take(sql::Result<T> result) {
    if (!result) throw std::runtime_error(result.error().message);
    return std::move(*result);
}
void check(sql::Result<void> result) {
    if (!result) throw std::runtime_error(result.error().message);
}
std::string bytes(const QString &value) { return value.toUtf8().toStdString(); }
QString text(const std::string &value) { return QString::fromUtf8(value.data(),qsizetype(value.size())); }
void interrupted(std::stop_token stop) {
    if (stop.stop_requested()) throw std::runtime_error("Content indexing was cancelled.");
}
QString storageId(const iiSocietyContainer::SharedStorage &storage) {
    const auto &drive=storage.drive();
    return drive.storageKind()==iiSocietyContainer::SocietyDrive::StorageKind::LogicalDisk
        ? drive.storageIdentifier() : drive.identifier();
}

// Opening a read-only legacy repository is optional. New directory-owned
// assets must not create an object store merely to inspect a text document.
std::unique_ptr<iiSocietyContainer::FileRepository> legacyRepository(const iiSocietyContainer::SharedStorage &storage) {
    const auto root=societyNativeObservedPath(storage.drive().rootPath());
    const auto package=iiSocietyContainer::FileRepository::packagePath(root);
    if (!std::filesystem::exists(package/"objects.sqlite3")) return {};
    return storage.fileRepository({},iiSocietyContainer::FileRepository::Access::ReadOnly,
                                  iiSocietyContainer::FileRepository::Intent::Background);
}
bool validText(const QString &value,qsizetype maximum,bool allowEmpty) {
    return value.size()<=maximum && (allowEmpty || !value.trimmed().isEmpty()) && !value.contains(QChar::Null)
        && QString::fromUtf8(value.toUtf8())==value;
}
void validateProfile(const Profile &profile) {
    if (!validText(profile.model,1024,false) || !validText(profile.documentPrefix,256,true) ||
        !validText(profile.queryPrefix,256,true)) throw std::runtime_error("Invalid content embedding profile.");
}

// Scope a vector space to the model's declared revision, both prefixes and
// the extractor/chunker version. The hash contains no document or query text.
QString scopeKey(const Profile &profile,const QString &revision) {
    if (!validText(revision,1024,false)) throw std::runtime_error("The embedding model revision is unavailable.");
    const QJsonArray identity{QStringLiteral("society-text-utf8-chunks-v1"),profile.model,revision,
                              profile.documentPrefix,profile.queryPrefix};
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact),
                                                       QCryptographicHash::Sha256).toHex());
}

std::string asString(sql::Statement &statement,int column) {
    const auto value=take(statement.value(column));
    if (!std::holds_alternative<std::string>(value)) throw std::runtime_error("Invalid text index record.");
    return std::get<std::string>(value);
}
std::int64_t asInteger(sql::Statement &statement,int column) {
    const auto value=take(statement.value(column));
    if (!std::holds_alternative<std::int64_t>(value)) throw std::runtime_error("Invalid text index number.");
    return std::get<std::int64_t>(value);
}
sql::Blob asBlob(sql::Statement &statement,int column) {
    auto value=take(statement.value(column));
    if (!std::holds_alternative<sql::Blob>(value)) throw std::runtime_error("Invalid text index vector.");
    return std::move(std::get<sql::Blob>(value));
}
void bind(sql::Statement &statement,int column,const QString &value) { check(statement.bind(column,bytes(value))); }
void bind(sql::Statement &statement,int column,std::int64_t value) { check(statement.bind(column,value)); }
std::int64_t pragma(sql::Database &db,const char *query) {
    auto statement=take(db.prepare(query));
    if (take(statement.step())!=sql::Step::row) throw std::runtime_error("The text index header is unavailable.");
    return asInteger(statement,0);
}

// The index is disposable derived data, but it must not claim or overwrite an
// unrelated database. New files are exclusively created with owner-only mode.
std::optional<sql::Database> openIndex(const iiSocietyContainer::SharedStorage &storage,bool create) {
    QString error;
    if (create && storage.ensureRuntimeDirectory("content-index",&error).isEmpty())
        throw std::runtime_error(bytes(error.isEmpty()?QStringLiteral("Cannot create the content index directory."):error));
    const auto path=storage.runtimePath("content-index/text-v1.sqlite3",&error);
    if (path.isEmpty()) throw std::runtime_error(bytes(error.isEmpty()?QStringLiteral("The content index path is unavailable."):error));
    QFileInfo info(path); bool created=false;
    if (!info.exists() && !info.isSymLink()) {
        if (!create) return {};
        QFile file(path);
        if (file.open(QIODevice::WriteOnly|QIODevice::NewOnly)) {
            created=true;
            if (!file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner))
                throw std::runtime_error("Cannot protect the content index file.");
            file.close();
        }
        info.refresh();
    }
    if (!info.isFile() || info.isSymLink() || info.isJunction())
        throw std::runtime_error("The content index is unavailable or redirected.");
    const auto verified=storage.runtimePath("content-index/text-v1.sqlite3",&error);
    if (verified!=path) throw std::runtime_error("The content index path changed.");
    sql::Options options;
    options.mode=create?sql::OpenMode::read_write:sql::OpenMode::read_only;
    options.busy_timeout_ms=2000;
    options.maximum_value_bytes=4*1024*1024;
    auto db=take(sql::Database::open(societyNativeObservedPath(path),options));
    if (take(db.has_moved())) throw std::runtime_error("The content index moved while opening.");
    const auto app=pragma(db,"PRAGMA application_id"), version=pragma(db,"PRAGMA user_version");
    if (created) {
        if (app || version) throw std::runtime_error("The new content index has an unexpected header.");
        check(db.configure_durable());
        auto transaction=take(sql::Transaction::begin(db));
        check(db.execute("CREATE TABLE documents(scope TEXT NOT NULL,resource_id TEXT NOT NULL,revision TEXT NOT NULL,digest TEXT NOT NULL,dimension INTEGER NOT NULL,chunk_count INTEGER NOT NULL,PRIMARY KEY(scope,resource_id))"));
        check(db.execute("CREATE TABLE chunks(scope TEXT NOT NULL,resource_id TEXT NOT NULL,ordinal INTEGER NOT NULL,vector BLOB NOT NULL,PRIMARY KEY(scope,resource_id,ordinal),FOREIGN KEY(scope,resource_id) REFERENCES documents(scope,resource_id) ON DELETE CASCADE)"));
        check(db.execute("PRAGMA application_id = 1396918616"));
        check(db.execute("PRAGMA user_version = 1"));
        check(transaction.commit());
    } else if (app!=applicationId || version!=schemaVersion) {
        throw std::runtime_error("The content index has an unknown format; it was not modified.");
    } else if (create) check(db.configure_durable());
    check(db.execute("PRAGMA foreign_keys = ON"));
    return std::optional<sql::Database>(std::move(db));
}

// The journal cursor and pending IDs live separately from vectors. A queue
// commit can precede or follow a vector commit because indexing is idempotent.
// Unknown files and schemas are rejected without migration or replacement.
std::optional<sql::Database> openQueue(const iiSocietyContainer::SharedStorage &storage,bool create) {
    constexpr std::int64_t queueApplicationId=0x53435158;
    QString error;
    if (create && storage.ensureRuntimeDirectory("content-index",&error).isEmpty())
        throw std::runtime_error(bytes(error.isEmpty()?QStringLiteral("Cannot create the content queue directory."):error));
    const auto path=storage.runtimePath("content-index/queue-v1.sqlite3",&error);
    if (path.isEmpty())
        throw std::runtime_error(bytes(error.isEmpty()?QStringLiteral("The content queue path is unavailable."):error));
    const auto directory=QFileInfo(path).absolutePath();
    QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) {
        if (!create) return {};
        // Build the schema in a private sibling and publish with an exclusive
        // hard link. A crash before publication cannot strand an empty queue.
        QTemporaryFile temporary(QDir(directory).filePath(".queue-init-XXXXXX"));
        if (!temporary.open() || !temporary.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner))
            throw std::runtime_error("Cannot create a protected content queue.");
        const auto temporaryName=QFileInfo(temporary.fileName()).fileName(); temporary.close();
        const auto nativeDirectory=societyNativeObservedPath(directory);
        const auto nativeTemporary=nativeDirectory/std::filesystem::path(temporaryName.toStdString());
        sql::Options temporaryOptions; temporaryOptions.mode=sql::OpenMode::read_write; temporaryOptions.busy_timeout_ms=2000;
        temporaryOptions.maximum_value_bytes=4*1024*1024;
        {
            auto initial=take(sql::Database::open(nativeTemporary,temporaryOptions));
            check(initial.configure_durable());
            auto transaction=take(sql::Transaction::begin(initial));
            check(initial.execute("CREATE TABLE progress(scope TEXT PRIMARY KEY,baseline_after INTEGER NOT NULL,baseline_done INTEGER NOT NULL,journal_after INTEGER NOT NULL)"));
            check(initial.execute("CREATE TABLE pending(scope TEXT NOT NULL,resource_id TEXT NOT NULL,version INTEGER NOT NULL,attempts INTEGER NOT NULL,retry_at INTEGER NOT NULL,PRIMARY KEY(scope,resource_id))"));
            check(initial.execute("CREATE INDEX pending_ready ON pending(scope,retry_at,resource_id)"));
            check(initial.execute("CREATE TABLE policy(singleton INTEGER PRIMARY KEY CHECK(singleton=1),model TEXT NOT NULL,document_prefix TEXT NOT NULL,query_prefix TEXT NOT NULL)"));
            check(initial.execute("PRAGMA application_id = 1396920664"));
            check(initial.execute("PRAGMA user_version = 2"));
            check(transaction.commit());
        }
        if (storage.runtimePath("content-index/queue-v1.sqlite3",&error)!=path)
            throw std::runtime_error("The content queue path changed.");
        std::error_code publishError;
        std::filesystem::create_hard_link(nativeTemporary,nativeDirectory/"queue-v1.sqlite3",publishError);
        if (publishError && publishError!=std::errc::file_exists)
            throw std::runtime_error("Cannot publish the initialized content queue.");
        info.refresh();
    }
    if (!info.isFile() || info.isSymLink() || info.isJunction() ||
        storage.runtimePath("content-index/queue-v1.sqlite3",&error)!=path)
        throw std::runtime_error("The content queue is unavailable or redirected.");
    sql::Options options; options.mode=create?sql::OpenMode::read_write:sql::OpenMode::read_only; options.busy_timeout_ms=2000;
    options.maximum_value_bytes=4*1024*1024;
    auto db=take(sql::Database::open(societyNativeObservedPath(path),options));
    if (take(db.has_moved())) throw std::runtime_error("The content queue moved while opening.");
    const auto app=pragma(db,"PRAGMA application_id"),version=pragma(db,"PRAGMA user_version");
    if (app!=queueApplicationId || (version!=1 && version!=2)) {
        throw std::runtime_error("The content queue has an unknown format; it was not modified.");
    }
    if (create) {
        check(db.configure_durable());
        if (version==1) {
            auto migration=take(sql::Transaction::begin(db));
            check(db.execute("CREATE TABLE policy(singleton INTEGER PRIMARY KEY CHECK(singleton=1),model TEXT NOT NULL,document_prefix TEXT NOT NULL,query_prefix TEXT NOT NULL)"));
            check(db.execute("PRAGMA user_version = 2"));
            check(migration.commit());
        }
    }
    return std::optional<sql::Database>(std::move(db));
}

// Accept explicit text formats only; UTF-8 validation below rejects binary or
// malformed payloads even when a file has one of these extensions.
bool supported(const QString &path) {
    static const QSet<QString> extensions{"txt","md","markdown","rst","json","jsonl","csv","tsv","yaml","yml",
        "toml","xml","html","htm","c","cc","cpp","h","hpp","py","sh","cmake","lvui"};
    return extensions.contains(QFileInfo(path).suffix().toLower());
}
QList<QString> chunks(const QByteArray &data) {
    if (data.size()>maximumTextBytes || data.contains('\0')) throw UnsupportedText("The text payload is too large or contains NUL bytes.");
    const auto decoded=QString::fromUtf8(data.constData(),data.size());
    if (decoded.toUtf8()!=data || decoded.trimmed().isEmpty()) throw UnsupportedText("The resource is not nonempty UTF-8 text.");
    QList<QString> result;
    for (qsizetype start=0;start<decoded.size();) {
        auto end=std::min(start+chunkCharacters,decoded.size());
        if (end<decoded.size() && decoded.at(end).isLowSurrogate() && end>start && decoded.at(end-1).isHighSurrogate()) --end;
        const auto part=decoded.mid(start,end-start);
        if (!part.trimmed().isEmpty()) result.append(part);
        if (end==decoded.size()) break;
        auto next=end-chunkOverlap;
        if (next<=start) next=end;
        if (next>0 && next<decoded.size() && decoded.at(next).isLowSurrogate() && decoded.at(next-1).isHighSurrogate()) --next;
        start=next;
    }
    if (result.isEmpty() || result.size()>160) throw UnsupportedText("The text needs too many embedding chunks.");
    return result;
}

// Normalize and encode finite vectors in a portable little-endian format.
std::vector<float> normalized(const QList<float> &values) {
    if (values.isEmpty() || values.size()>maximumDimensions) throw std::runtime_error("Invalid embedding dimension.");
    long double squared=0;
    for (const auto value:values) {
        if (!std::isfinite(value)) throw std::runtime_error("The embedding contains a non-finite value.");
        squared+=static_cast<long double>(value)*value;
    }
    if (!(squared>0) || !std::isfinite(squared)) throw std::runtime_error("The embedding has no usable magnitude.");
    const auto scale=1/std::sqrt(squared);
    std::vector<float> result; result.reserve(std::size_t(values.size()));
    for (const auto value:values) result.push_back(float(value*scale));
    return result;
}
sql::Blob encode(const std::vector<float> &values) {
    sql::Blob result; result.reserve(values.size()*4);
    for (const auto value:values) {
        const auto bits=std::bit_cast<std::uint32_t>(value);
        for (unsigned shift=0;shift<32;shift+=8) result.push_back(std::uint8_t(bits>>shift));
    }
    return result;
}
std::vector<float> decode(const sql::Blob &data,std::size_t dimension) {
    if (!dimension || dimension>std::size_t(maximumDimensions) || data.size()!=dimension*4)
        throw std::runtime_error("The content index vector has an invalid size.");
    std::vector<float> result; result.reserve(dimension);
    long double squared=0;
    for (std::size_t i=0;i<data.size();i+=4) {
        const auto bits=std::uint32_t(data[i]) | (std::uint32_t(data[i+1])<<8) |
                        (std::uint32_t(data[i+2])<<16) | (std::uint32_t(data[i+3])<<24);
        const auto value=std::bit_cast<float>(bits);
        if (!std::isfinite(value)) throw std::runtime_error("The content index vector is not finite.");
        squared+=static_cast<long double>(value)*value; result.push_back(value);
    }
    if (!(squared>0) || !std::isfinite(squared) || std::abs(squared-1.0L)>0.001L)
        throw std::runtime_error("The content index vector is not normalized.");
    return result;
}

struct Observation {
    QString path,id,revision,digest,publicId;
    iiFileProvider::ObjectRecord record;
    QByteArray payload;
    bool liveFile=false;
};

// Recheck the live visible file against the managed record rather than
// treating a path, prior hash or SQLite row as a current resource identity.
Observation observe(const QString &container,const iiSocietyContainer::SharedStorage &storage,
                    iiSocietyContainer::FileRepository *repository,const QString &input) {
    if (!input.startsWith("Files/") || input.size()>4096 || QDir::cleanPath(input)!=input ||
        input.contains('\\') || input.contains(QChar::Null)) throw std::runtime_error("Use a visible Files path.");
    QString error;
    const auto absolute=storage.filePath(iiSocietyContainer::StoreSection::Files,input.mid(6),&error);
    if (absolute.isEmpty() || !QFileInfo::exists(absolute))
        throw std::runtime_error(bytes(error.isEmpty()?QStringLiteral("The text file is unavailable."):error));
    const auto root=QFileInfo(storage.drive().sectionPath(iiSocietyContainer::StoreSection::Files)).canonicalFilePath();
    if (root.isEmpty()) throw std::runtime_error("The Files section is unavailable.");
    auto relative=QDir(root).relativeFilePath(absolute);
#ifdef Q_OS_MACOS
    // Do not conflate different Unicode spellings unless the native path
    // adapter can establish that they resolve to the very same entry.
    (void)societyNativeObservedPath(absolute);
    relative=relative.normalized(QString::NormalizationForm_D);
#endif
    const auto visible=QStringLiteral("Files/")+relative;
    const auto managed=storage.managedFilePath(iiSocietyContainer::StoreSection::Files,relative);
    const auto rootPath=societyNativeObservedPath(storage.drive().rootPath());
    const auto native=societyNativeObservedPath(absolute);
    const auto addressPath=native.lexically_relative(rootPath).generic_u8string();
    const std::string address(reinterpret_cast<const char *>(addressPath.data()),addressPath.size());
    const auto database=rootPath/".iisacc-file-provider"/"metadata-paths.sqlite3";
    if (std::filesystem::exists(database)) {
        iiFileProvider::DirectoryMetadataIndex index(rootPath,iiFileProvider::DirectoryMetadataIndex::Access::ReadOnly);
        if (index.contains(address)) {
            if (repository && repository->objects().lookupPath(managed))
                throw std::runtime_error("The file has conflicting legacy and directory owners.");
            auto legacy=iiSocietyContainer::FileRepository::classificationDirectory(rootPath,"files")/
                        native.lexically_relative(societyNativeObservedPath(storage.drive().sectionPath(iiSocietyContainer::StoreSection::Files)));
            legacy += ".iisacc.json";
            if (std::filesystem::exists(legacy) || std::filesystem::is_symlink(std::filesystem::symlink_status(legacy)))
                throw std::runtime_error("The file has conflicting legacy and directory metadata.");
            const auto document=index.read(address);
            if (!document) throw std::runtime_error("The directory metadata is unavailable.");
            QJsonParseError parse;
            const auto metadata=QJsonDocument::fromJson(QByteArray::fromStdString(*document),&parse).object();
            const auto fileKey=metadata.value("fileKey").toString();
            if (parse.error!=QJsonParseError::NoError || fileKey.isEmpty() ||
                metadata.value("nativeIdentity").toString().isEmpty())
                throw std::runtime_error("The directory metadata owner cannot be verified.");
            const auto state=index.inspectAsset(address);
            if (state.size>std::uint64_t(maximumTextBytes)) throw UnsupportedText("The text resource exceeds 64 KiB.");
            const auto range=index.readAsset(address,0,std::size_t(maximumTextBytes),state.contentVersion);
            if (!range.eof || range.size!=state.size) throw std::runtime_error("The text file changed while reading.");
            const QByteArray payload(reinterpret_cast<const char *>(range.bytes.data()),qsizetype(range.bytes.size()));
            const auto hash=QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex());
            const auto id=QStringLiteral("path:")+visible;
            if (bytes(id).size()>4096) throw std::runtime_error("The text resource path is too long.");
            Observation result;
            result.path=visible; result.id=id; result.publicId=fileKey; result.digest=hash;
            result.revision=QStringLiteral("file:")+fileKey+QStringLiteral(":")+hash;
            result.payload=payload; result.liveFile=true;
            return result;
        }
    }
    const auto legacyRecord=repository?repository->lookup(managed):std::nullopt;
    if (!legacyRecord) {
        // A fresh resident file needs no durable capture. Do not adopt any
        // adjacent or private document whose owner has not been established.
        auto companion=native; companion += ".iisacc.json";
        const auto status=std::filesystem::symlink_status(companion);
        auto privateDocument=iiSocietyContainer::FileRepository::classificationDirectory(rootPath,"files")/
            native.lexically_relative(societyNativeObservedPath(storage.drive().sectionPath(iiSocietyContainer::StoreSection::Files)));
        privateDocument += ".iisacc.json";
        const auto privateStatus=std::filesystem::symlink_status(privateDocument);
        if (std::filesystem::exists(status) || std::filesystem::is_symlink(status) ||
            std::filesystem::exists(privateStatus) || std::filesystem::is_symlink(privateStatus))
            throw std::runtime_error("The text file has unverified metadata.");
        const auto identity=iiFileProvider::ObjectSource::inspect(native);
        if (identity.size>std::uint64_t(maximumTextBytes)) throw UnsupportedText("The text resource exceeds 64 KiB.");
        iiFileProvider::native::Device input(false);
        input.open(native,{.read=true,.noFollow=true});
        if (input.size()!=qint64(identity.size)) throw std::runtime_error("The text file changed while opening.");
        QByteArray payload(qsizetype(identity.size),Qt::Uninitialized);
        qint64 position=0;
        while (position<payload.size()) {
            const auto count=input.read(payload.data()+position,payload.size()-position);
            if (count<=0) throw std::runtime_error("The text file changed while reading.");
            position+=count;
        }
        if (iiFileProvider::ObjectSource::inspect(native)!=identity)
            throw std::runtime_error("The text file changed while reading.");
        const auto hash=QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex());
        const auto id=QStringLiteral("path:")+visible;
        if (bytes(id).size()>4096) throw std::runtime_error("The text resource path is too long.");
        Observation result;
        result.path=visible; result.id=id; result.publicId=id; result.digest=hash;
        // Until metadata ownership is established this identifier represents
        // the current path and bytes, not a persistent identity across moves.
        result.revision=QStringLiteral("live:")+hash;
        result.payload=payload; result.liveFile=true;
        return result;
    }
    const auto observation=society::resources::inspect(container,visible);
    if (!observation.error.isEmpty()) throw std::runtime_error(bytes(observation.error));
    const auto resource=observation.snapshot.value("resource").toObject();
    const auto revision=observation.snapshot.value("contentRevision").toObject();
    const auto digest=revision.value("digest").toObject();
    const auto record=repository->lookup(managed);
    if (!record || record->deleted || resource.value("id").toString()!=text(record->key) ||
        revision.value("id").toString().isEmpty() || digest.value("algorithm")!="sha256" ||
        digest.value("value").toString()!=text(record->sha256))
        throw std::runtime_error("The observed text revision is unavailable or changed.");
    Observation result;
    result.path=visible; result.id=text(record->key); result.publicId=result.id;
    result.revision=revision.value("id").toString(); result.digest=text(record->sha256); result.record=*record;
    return result;
}

bool same(const Observation &a,const Observation &b) {
    if (a.liveFile || b.liveFile)
        // Each read checked its own native state. Across model inference,
        // require the same owner and bytes; unrelated metadata changes need
        // not discard vectors that still describe the current file contents.
        return a.liveFile && b.liveFile && a.id==b.id && a.publicId==b.publicId &&
            a.revision==b.revision && a.digest==b.digest;
    return a.id==b.id && a.revision==b.revision && a.digest==b.digest && a.record.version==b.record.version &&
        a.record.validationKey==b.record.validationKey && a.record.path==b.record.path;
}
QJsonObject emptyPage(const Profile &profile,const QString &state) {
    return {{"items",QJsonArray{}},{"model",profile.model},{"index",state},{"evidence","local_text_embeddings"},
            {"completeness","unknown"},{"truncated",false},{"skipped",0}};
}
}

// Apply only the cheap filename/size admission check; content validation stays
// with the extractor after a managed version has been captured.
bool fileContentEmbeddingEnabled() noexcept { return false; }

// Preserve the old callable boundary while making policy rejection explicit
// before it can read a file, initialize a model, or create a derived database.
static Result contentIndexDisabled() {
    return {{},QStringLiteral("File content indexing is disabled; file indexes use only hashes and metadata."),
            Result::Failure::unsupported};
}

bool isTextCandidate(const QString &path,quint64 size) {
    return size>0 && size<=quint64(maximumTextBytes) && supported(path);
}

Result indexText(const QString &container,const QString &path,const Profile &profile,Embedder &embedder,
                 const QString &temporaryRoot,std::stop_token cancellation) {
    if (!fileContentEmbeddingEnabled()) return contentIndexDisabled();
    Result result;
    try {
        // Retained for source compatibility; live reads require no staging directory.
        (void)temporaryRoot;
        interrupted(cancellation); validateProfile(profile);
        if (!supported(path)) throw UnsupportedText("This file type is not supported by text indexing.");
        QString error; const auto storage=iiSocietyContainer::SharedStorage::inspect(container,&error);
        if (!storage || !storage->drive().isReady()) throw std::runtime_error(bytes(error.isEmpty()?QStringLiteral("The Society drive is unavailable."):error));
        const auto containerId=storageId(*storage);
        if (containerId.isEmpty()) throw std::runtime_error("The Society storage identity is unavailable.");
        // Inference and publication for this derived index have one writer
        // across processes. Never wait behind a model load on the caller thread.
        if (storage->ensureRuntimeDirectory("content-index",&error).isEmpty())
            throw std::runtime_error(bytes(error.isEmpty()?QStringLiteral("Cannot create the content index directory."):error));
        const auto lockPath=storage->runtimePath("content-index/text-write.lock",&error);
        if (lockPath.isEmpty()) throw std::runtime_error("The content index lock is unavailable.");
        QLockFile writer(lockPath); writer.setStaleLockTime(0);
        if (!writer.tryLock(0)) throw std::runtime_error("The content index is busy.");
        interrupted(cancellation);
        auto repository=legacyRepository(*storage);
        const auto before=observe(container,*storage,repository.get(),path);
        if (!before.liveFile && before.record.size>std::uint64_t(maximumTextBytes)) throw UnsupportedText("The text resource exceeds 64 KiB.");
        const auto modelRevision=embedder.revision(profile.model,cancellation);
        interrupted(cancellation);
        const auto scope=scopeKey(profile,modelRevision);
        auto db=openIndex(*storage,true);
        int existingChunks=0;
        {
            auto current=take(db->prepare("SELECT revision,digest,chunk_count FROM documents WHERE scope=? AND resource_id=?"));
            bind(current,1,scope); bind(current,2,before.id);
            if (take(current.step())==sql::Step::row && text(asString(current,0))==before.revision &&
                text(asString(current,1))==before.digest) existingChunks=int(asInteger(current,2));
        }
        if (existingChunks>0) {
            interrupted(cancellation);
            const auto after=observe(container,*storage,repository.get(),before.path);
            const auto checked=iiSocietyContainer::SharedStorage::inspect(container,&error);
            if (!same(before,after) || !checked || !checked->drive().isReady() || storageId(*checked)!=containerId ||
                embedder.revision(profile.model,cancellation)!=modelRevision)
                throw std::runtime_error("The text resource, model or Society storage changed during indexing.");
            interrupted(cancellation);
            result.value={{"state","current"},{"containerId",containerId},{"resourceId",before.publicId},
                          {"revisionId",before.revision},{"model",profile.model},{"modelRevision",modelRevision},
                          {"chunks",existingChunks}};
            return result;
        }
        QByteArray payload=before.payload;
        if (!before.liveFile) {
            const auto fileName=storage->filePath(iiSocietyContainer::StoreSection::Files,before.path.mid(6),&error);
            if (fileName.isEmpty()) throw std::runtime_error("The current text file is unavailable.");
            const auto native=societyNativeObservedPath(fileName);
            const auto identity=iiFileProvider::ObjectSource::inspect(native);
            if (identity.stamp!=before.record.sourceStamp || identity.size!=before.record.size || identity.size>std::uint64_t(maximumTextBytes))
                throw std::runtime_error("The text file changed before it could be read.");
            iiFileProvider::native::Device input(false);
            input.open(native,{.read=true,.noFollow=true});
            if (input.size()!=qint64(identity.size)) throw std::runtime_error("The text file changed while opening.");
            payload=QByteArray(qsizetype(identity.size),Qt::Uninitialized);
            qint64 position=0;
            while (position<payload.size()) {
                interrupted(cancellation);
                const auto count=input.read(payload.data()+position,payload.size()-position);
                if (count<=0) throw std::runtime_error("The text file changed while reading.");
                position+=count;
            }
            if (iiFileProvider::ObjectSource::inspect(native)!=identity ||
                QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex())!=before.digest)
                throw std::runtime_error("The current text does not match the observed revision.");
        }
        const auto parts=chunks(payload);
        std::vector<sql::Blob> vectors; vectors.reserve(std::size_t(parts.size())); std::size_t dimension=0;
        for (const auto &part:parts) {
            interrupted(cancellation);
            const auto vector=normalized(embedder.embed(profile.model,profile.documentPrefix+part,cancellation));
            if (dimension && dimension!=vector.size()) throw std::runtime_error("The embedding dimension changed during indexing.");
            dimension=vector.size(); vectors.push_back(encode(vector));
        }
        interrupted(cancellation);
        if (embedder.revision(profile.model,cancellation)!=modelRevision)
            throw std::runtime_error("The embedding model changed during indexing.");
        const auto after=observe(container,*storage,repository.get(),before.path);
        const auto checked=iiSocietyContainer::SharedStorage::inspect(container,&error);
        if (!same(before,after) || !checked || !checked->drive().isReady() || storageId(*checked)!=containerId)
            throw std::runtime_error("The text resource or Society storage changed during indexing.");
        interrupted(cancellation);
        auto transaction=take(sql::Transaction::begin(*db));
        auto remove=take(db->prepare("DELETE FROM documents WHERE scope=? AND resource_id=?"));
        bind(remove,1,scope); bind(remove,2,before.id); (void)take(remove.step());
        auto document=take(db->prepare("INSERT INTO documents(scope,resource_id,revision,digest,dimension,chunk_count) VALUES(?,?,?,?,?,?)"));
        bind(document,1,scope); bind(document,2,before.id); bind(document,3,before.revision); bind(document,4,before.digest);
        bind(document,5,std::int64_t(dimension)); bind(document,6,std::int64_t(vectors.size())); (void)take(document.step());
        auto insert=take(db->prepare("INSERT INTO chunks(scope,resource_id,ordinal,vector) VALUES(?,?,?,?)"));
        for (std::size_t i=0;i<vectors.size();++i) {
            interrupted(cancellation);
            bind(insert,1,scope); bind(insert,2,before.id); bind(insert,3,std::int64_t(i)); check(insert.bind(4,vectors[i]));
            (void)take(insert.step()); check(insert.reset());
        }
        interrupted(cancellation); check(transaction.commit());
        result.value={{"state","indexed"},{"containerId",containerId},{"resourceId",before.publicId},
                      {"revisionId",before.revision},{"model",profile.model},{"modelRevision",modelRevision},
                      {"chunks",int(vectors.size())},{"snapshot","verified_live_file_non_atomic"}};
    } catch (const UnsupportedText &error) { result.error=QString::fromUtf8(error.what()); result.failure=Result::Failure::unsupported; }
      catch (const std::exception &error) { result.error=QString::fromUtf8(error.what()); result.failure=Result::Failure::retryable; }
      catch (...) { result.error=QStringLiteral("Could not index the text resource."); result.failure=Result::Failure::retryable; }
    if (result.error.isEmpty() && result.value.isEmpty()) {
        result.error=QStringLiteral("Could not index the text resource."); result.failure=Result::Failure::retryable;
    }
    return result;
}

Result searchText(const QString &container,const QString &query,const Profile &profile,Embedder &embedder,
                  int limit,std::stop_token cancellation) {
    if (!fileContentEmbeddingEnabled()) return contentIndexDisabled();
    Result result;
    try {
        interrupted(cancellation); validateProfile(profile);
        if (!validText(query,4096,false) || limit<1 || limit>20) throw std::runtime_error("Invalid semantic text query or limit.");
        QString error; const auto storage=iiSocietyContainer::SharedStorage::inspect(container,&error);
        if (!storage || !storage->drive().isReady()) throw std::runtime_error(bytes(error.isEmpty()?QStringLiteral("The Society drive is unavailable."):error));
        const auto containerId=storageId(*storage);
        if (containerId.isEmpty()) throw std::runtime_error("The Society storage identity is unavailable.");
        const auto modelRevision=embedder.revision(profile.model,cancellation);
        interrupted(cancellation);
        const auto scope=scopeKey(profile,modelRevision);
        auto db=openIndex(*storage,false);
        if (!db) { interrupted(cancellation); result.value=emptyPage(profile,"not_started"); result.value.insert("containerId",containerId); return result; }
        std::int64_t documents=0,expectedChunks=0;
        {
            auto count=take(db->prepare("SELECT COUNT(*),COALESCE(SUM(chunk_count),0) FROM documents WHERE scope=?"));
            bind(count,1,scope);
            if (take(count.step())!=sql::Step::row) throw std::runtime_error("The content index count is unavailable.");
            documents=asInteger(count,0); expectedChunks=asInteger(count,1);
        }
        if (!documents) { interrupted(cancellation); result.value=emptyPage(profile,"not_started"); result.value.insert("containerId",containerId); return result; }
        if (documents<0 || expectedChunks<documents) throw std::runtime_error("The content index counts are invalid.");
        const auto queryVector=normalized(embedder.embed(profile.model,profile.queryPrefix+query,cancellation));
        interrupted(cancellation);
        if (embedder.revision(profile.model,cancellation)!=modelRevision)
            throw std::runtime_error("The embedding model changed during search.");
        auto rows=take(db->prepare("SELECT d.resource_id,d.revision,d.digest,d.dimension,d.chunk_count,c.ordinal,c.vector FROM documents d JOIN chunks c ON c.scope=d.scope AND c.resource_id=d.resource_id WHERE d.scope=? ORDER BY d.resource_id,c.ordinal LIMIT 10001"));
        bind(rows,1,scope);
        struct Candidate { QString id,revision,digest; double score=-2; int chunk=0,seen=0,expected=0; };
        std::map<std::string,Candidate> candidates; int scanned=0; bool truncated=false;
        while (take(rows.step())==sql::Step::row) {
            interrupted(cancellation);
            if (scanned>=maximumScanChunks) { truncated=true; break; }
            const auto id=asString(rows,0),revision=asString(rows,1),digest=asString(rows,2);
            const auto dimension=asInteger(rows,3),count=asInteger(rows,4),ordinal=asInteger(rows,5);
            if (id.empty() || id.size()>4096 || revision.empty() || revision.size()>4096 || digest.size()!=64 ||
                dimension!=std::int64_t(queryVector.size()) || count<1 || count>160 || ordinal<0 || ordinal>=count)
                throw std::runtime_error("The content index does not match the embedding model.");
            const auto vector=decode(asBlob(rows,6),std::size_t(dimension));
            double score=0; for (std::size_t i=0;i<vector.size();++i) score+=double(vector[i])*queryVector[i];
            auto [it,inserted]=candidates.try_emplace(id,Candidate{text(id),text(revision),text(digest),score,int(ordinal),0,int(count)});
            if ((!inserted && (it->second.revision!=text(revision) || it->second.digest!=text(digest) || it->second.expected!=count)) ||
                ordinal!=it->second.seen) throw std::runtime_error("The content index chunks are inconsistent.");
            if (!inserted && score>it->second.score) { it->second.score=score; it->second.chunk=int(ordinal); }
            ++it->second.seen; ++scanned;
        }
        if (!truncated && (scanned!=expectedChunks || std::int64_t(candidates.size())!=documents))
            throw std::runtime_error("The content index is incomplete or corrupt.");
        std::vector<Candidate> ranked; ranked.reserve(candidates.size()); int skipped=0;
        for (const auto &[key,candidate]:candidates) {
            if (candidate.seen!=candidate.expected) { ++skipped; continue; }
            ranked.push_back(candidate);
        }
        std::sort(ranked.begin(),ranked.end(),[](const Candidate &a,const Candidate &b) {
            return a.score==b.score ? a.id<b.id : a.score>b.score;
        });
        auto repository=legacyRepository(*storage);
        QJsonArray items;
        for (const auto &candidate:ranked) {
            interrupted(cancellation);
            try {
                QString path;
                if (candidate.id.startsWith("path:Files/")) path=candidate.id.mid(5);
                else {
                    if (!repository) { ++skipped; continue; }
                    const auto record=repository->objects().lookup(bytes(candidate.id));
                    if (!record || record->deleted || !record->path.starts_with("files/") || text(record->sha256)!=candidate.digest) { ++skipped; continue; }
                    path=QStringLiteral("Files/")+text(record->path).mid(6);
                }
                const auto observation=observe(container,*storage,repository.get(),path);
                if (observation.id!=candidate.id || observation.revision!=candidate.revision || observation.digest!=candidate.digest) { ++skipped; continue; }
                items.append(QJsonObject{{"path",observation.path},{"resourceId",observation.publicId},{"revisionId",candidate.revision},
                                         {"score",std::clamp(candidate.score,-1.0,1.0)},{"chunk",candidate.chunk}});
                if (items.size()>=limit) break;
            } catch (const std::exception &) { ++skipped; }
        }
        const auto checked=iiSocietyContainer::SharedStorage::inspect(container,&error);
        if (!checked || !checked->drive().isReady() || storageId(*checked)!=containerId ||
            embedder.revision(profile.model,cancellation)!=modelRevision)
            throw std::runtime_error("The selected Society storage or embedding model changed during search.");
        interrupted(cancellation);
        result.value={{"items",items},{"containerId",containerId},{"model",profile.model},{"modelRevision",modelRevision},
                      {"index","available"},{"evidence","local_text_embeddings"},{"completeness","unknown"},
                      {"scannedChunks",scanned},{"truncated",truncated},{"skipped",skipped}};
    } catch (const std::exception &error) { result.error=QString::fromUtf8(error.what()); }
      catch (...) { result.error=QStringLiteral("Could not search the text index."); }
    if (result.error.isEmpty() && result.value.isEmpty()) result.error=QStringLiteral("Could not search the text index.");
    return result;
}

Result indexTrackedTextBatch(const QString &container,const Profile &profile,Embedder &embedder,
                             const BatchOptions &options,std::stop_token cancellation) {
    if (!fileContentEmbeddingEnabled()) return contentIndexDisabled();
    Result result;
    try {
        interrupted(cancellation); validateProfile(profile);
        if (options.scanLimit<1 || options.scanLimit>128 || options.workLimit<1 || options.workLimit>16 || options.nowMs< -1)
            throw std::runtime_error("Invalid content queue batch limits.");
        const auto now=options.nowMs<0?QDateTime::currentMSecsSinceEpoch():options.nowMs;
        if (now<0 || now>std::numeric_limits<qint64>::max()-300000)
            throw std::runtime_error("The content queue clock is invalid.");
        QString error; const auto storage=iiSocietyContainer::SharedStorage::inspect(container,&error);
        if (!storage || !storage->drive().isReady())
            throw std::runtime_error(bytes(error.isEmpty()?QStringLiteral("The Society drive is unavailable."):error));
        const auto containerId=storageId(*storage);
        if (containerId.isEmpty()) throw std::runtime_error("The Society storage identity is unavailable.");
        const auto modelRevision=embedder.revision(profile.model,cancellation);
        interrupted(cancellation);
        const auto scope=scopeKey(profile,modelRevision);
        auto repository=legacyRepository(*storage);
        if (!repository) {
            // A missing legacy store must not make an unknown or unfinished
            // durable queue look successfully drained.
            if (auto existing=openQueue(*storage,false)) {
                auto pending=take(existing->prepare("SELECT COUNT(*) FROM pending"));
                if (take(pending.step())!=sql::Step::row || asInteger(pending,0)!=0)
                    throw std::runtime_error("The legacy object store for the pending content queue is unavailable.");
            }
            const auto checked=iiSocietyContainer::SharedStorage::inspect(container,&error);
            if (!checked || !checked->drive().isReady() || storageId(*checked)!=containerId ||
                embedder.revision(profile.model,cancellation)!=modelRevision)
                throw std::runtime_error("The selected Society storage or embedding model changed during queue processing.");
            interrupted(cancellation);
            result.value={{"containerId",containerId},{"model",profile.model},{"modelRevision",modelRevision},
                          {"state","idle"},{"baselineComplete",true},{"pending",0},{"completeness","unknown"}};
            return result;
        }
        auto opened=openQueue(*storage,true);
        if (!opened) throw std::runtime_error("The content queue is unavailable.");
        auto db=std::move(*opened);
        {
            auto create=take(db.prepare("INSERT OR IGNORE INTO progress(scope,baseline_after,baseline_done,journal_after) VALUES(?,0,0,0)"));
            bind(create,1,scope); (void)take(create.step());
        }
        struct Progress { std::int64_t baseline=0,journal=0; bool complete=false; };
        const auto progress=[&] {
            auto query=take(db.prepare("SELECT baseline_after,baseline_done,journal_after FROM progress WHERE scope=?"));
            bind(query,1,scope);
            if (take(query.step())!=sql::Step::row) throw std::runtime_error("The content queue checkpoint is unavailable.");
            const auto baseline=asInteger(query,0),done=asInteger(query,1),journal=asInteger(query,2);
            if (baseline<0 || journal<0 || (done!=0 && done!=1)) throw std::runtime_error("The content queue checkpoint is invalid.");
            return Progress{baseline,journal,done==1};
        };
        const auto start=progress();
        std::vector<iiFileProvider::ObjectIndexEntry> baseline;
        if (!start.complete) baseline=repository->objects().index(start.baseline,std::size_t(options.scanLimit),true);
        iiFileProvider::ObjectJournalQuery query; query.afterSequence=start.journal; query.limit=std::size_t(options.scanLimit);
        const auto journal=repository->objects().journal(query);
        auto baselineAfter=start.baseline,journalAfter=start.journal;
        for (const auto &entry:baseline) {
            if (entry.indexKey<=baselineAfter) throw std::runtime_error("The content baseline page is invalid.");
            baselineAfter=entry.indexKey;
        }
        for (const auto &entry:journal) {
            if (entry.sequence<=journalAfter) throw std::runtime_error("The content journal page is invalid.");
            journalAfter=entry.sequence;
        }
        const bool baselineDone=start.complete || baseline.size()<std::size_t(options.scanLimit);
        const auto stillSelected=[&] {
            const auto current=iiSocietyContainer::SharedStorage::inspect(container,&error);
            if (!current || !current->drive().isReady() || storageId(*current)!=containerId ||
                embedder.revision(profile.model,cancellation)!=modelRevision)
                throw std::runtime_error("The selected Society storage or embedding model changed during queue processing.");
            interrupted(cancellation);
        };
        stillSelected();
        int enqueued=0;
        {
            // Every cursor advance and its corresponding work are one durable
            // transaction. Replayed/overlapping pages preserve the newest head.
            auto transaction=take(sql::Transaction::begin(db));
            auto enqueue=take(db.prepare("INSERT INTO pending(scope,resource_id,version,attempts,retry_at) VALUES(?,?,?,0,0) "
                "ON CONFLICT(scope,resource_id) DO UPDATE SET "
                "attempts=CASE WHEN excluded.version>pending.version THEN 0 ELSE pending.attempts END, "
                "retry_at=CASE WHEN excluded.version>pending.version THEN 0 ELSE pending.retry_at END, "
                "version=MAX(pending.version,excluded.version)"));
            const auto add=[&](const std::string &key,std::uint64_t version,const std::string &path) {
                if (!path.starts_with("files/")) return;
                const auto id=text(key);
                if (key.empty() || key.size()>4096 || bytes(id)!=key || !version ||
                    version>std::uint64_t(std::numeric_limits<std::int64_t>::max()))
                    throw std::runtime_error("The content queue resource identity is invalid.");
                bind(enqueue,1,scope); bind(enqueue,2,id); bind(enqueue,3,std::int64_t(version));
                (void)take(enqueue.step()); check(enqueue.reset()); ++enqueued;
            };
            for (const auto &entry:baseline) { interrupted(cancellation); add(entry.key,entry.version,entry.path); }
            for (const auto &entry:journal) { interrupted(cancellation); add(entry.objectKey,entry.version,entry.path); }
            auto checkpoint=take(db.prepare("UPDATE progress SET baseline_after=MAX(baseline_after,?), "
                "baseline_done=MAX(baseline_done,?),journal_after=MAX(journal_after,?) WHERE scope=?"));
            bind(checkpoint,1,baselineAfter); bind(checkpoint,2,std::int64_t(baselineDone));
            bind(checkpoint,3,journalAfter); bind(checkpoint,4,scope); (void)take(checkpoint.step());
            interrupted(cancellation); check(transaction.commit());
        }
        struct Task { QString id; std::int64_t version=0,attempts=0; };
        std::vector<Task> tasks;
        {
            auto pending=take(db.prepare("SELECT resource_id,version,attempts FROM pending WHERE scope=? AND retry_at<=? "
                "ORDER BY retry_at,resource_id LIMIT ?"));
            bind(pending,1,scope); bind(pending,2,std::int64_t(now)); bind(pending,3,std::int64_t(options.workLimit));
            while (take(pending.step())==sql::Step::row) {
                Task task{text(asString(pending,0)),asInteger(pending,1),asInteger(pending,2)};
                if (task.id.isEmpty() || task.version<1 || task.attempts<0 || task.attempts>8)
                    throw std::runtime_error("The pending content task is invalid.");
                tasks.push_back(std::move(task));
            }
        }
        int indexed=0,current=0,skipped=0,failed=0; QString retryReason;
        for (const auto &task:tasks) {
            interrupted(cancellation); stillSelected();
            const auto observed=repository->objects().lookup(bytes(task.id));
            Result indexedResult;
            if (!observed || observed->deleted || !observed->path.starts_with("files/")) {
                indexedResult.failure=Result::Failure::unsupported;
                indexedResult.error=QStringLiteral("The observed text resource is absent from Files.");
            } else {
                indexedResult=indexText(container,QStringLiteral("Files/")+text(observed->path).mid(6),
                                        profile,embedder,options.temporaryRoot,cancellation);
            }
            interrupted(cancellation); stillSelected();
            if (indexedResult.error.isEmpty() || indexedResult.failure==Result::Failure::unsupported) {
                auto remove=take(db.prepare("DELETE FROM pending WHERE scope=? AND resource_id=? AND version=?"));
                bind(remove,1,scope); bind(remove,2,task.id); bind(remove,3,task.version); (void)take(remove.step());
                if (!indexedResult.error.isEmpty()) ++skipped;
                else if (indexedResult.value.value("state")=="current") ++current;
                else ++indexed;
            } else {
                const auto delay=std::min<std::int64_t>(300000,std::int64_t(1000)<<int(std::min<std::int64_t>(task.attempts,8)));
                auto retry=take(db.prepare("UPDATE pending SET attempts=?,retry_at=? WHERE scope=? AND resource_id=? AND version=?"));
                bind(retry,1,std::min<std::int64_t>(8,task.attempts+1)); bind(retry,2,std::int64_t(now)+delay);
                bind(retry,3,scope); bind(retry,4,task.id); bind(retry,5,task.version); (void)take(retry.step());
                ++failed; if (retryReason.isEmpty()) retryReason=indexedResult.error;
            }
        }
        stillSelected();
        std::int64_t pendingCount=0,readyCount=0;
        {
            auto count=take(db.prepare("SELECT COUNT(*),COALESCE(SUM(CASE WHEN retry_at<=? THEN 1 ELSE 0 END),0) FROM pending WHERE scope=?"));
            bind(count,1,std::int64_t(now)); bind(count,2,scope);
            if (take(count.step())!=sql::Step::row) throw std::runtime_error("The content queue count is unavailable.");
            pendingCount=asInteger(count,0); readyCount=asInteger(count,1);
        }
        const auto final=progress();
        const bool journalExhausted=journal.size()<std::size_t(options.scanLimit);
        const auto state=final.complete && journalExhausted && !pendingCount ? QStringLiteral("idle")
            : final.complete && journalExhausted && pendingCount && !readyCount ? QStringLiteral("waiting") : QStringLiteral("working");
        result.value={{"containerId",containerId},{"model",profile.model},{"modelRevision",modelRevision},
            {"baselineComplete",final.complete},{"baselineAfter",QString::number(final.baseline)},
            {"journalAfter",QString::number(final.journal)},{"enqueued",enqueued},{"indexed",indexed},
            {"current",current},{"skipped",skipped},{"failed",failed},{"pending",double(pendingCount)},
            {"ready",double(readyCount)},{"state",state},{"completeness","unknown"}};
        if (!retryReason.isEmpty()) result.value.insert("retryReason",retryReason);
    } catch (const std::exception &error) { result.error=QString::fromUtf8(error.what()); result.failure=Result::Failure::retryable; }
      catch (...) { result.error=QStringLiteral("Could not advance the content queue."); result.failure=Result::Failure::retryable; }
    return result;
}

Result trackedTextPolicy(const QString &container,std::stop_token cancellation) {
    Result result;
    try {
        interrupted(cancellation); QString error;
        const auto storage=iiSocietyContainer::SharedStorage::inspect(container,&error);
        if (!storage || !storage->drive().isReady())
            throw std::runtime_error(bytes(error.isEmpty()?QStringLiteral("The Society drive is unavailable."):error));
        const auto containerId=storageId(*storage);
        if (containerId.isEmpty()) throw std::runtime_error("The Society storage identity is unavailable.");
        auto db=openQueue(*storage,false);
        QJsonObject value{{"containerId",containerId},{"enabled",false}};
        if (db && pragma(*db,"PRAGMA user_version")>=2) {
            auto query=take(db->prepare("SELECT model,document_prefix,query_prefix FROM policy WHERE singleton=1"));
            if (take(query.step())==sql::Step::row) {
                Profile profile{text(asString(query,0)),text(asString(query,1)),text(asString(query,2))};
                validateProfile(profile);
                value.insert("enabled",true); value.insert("model",profile.model);
                value.insert("documentPrefix",profile.documentPrefix); value.insert("queryPrefix",profile.queryPrefix);
            }
        }
        if (!fileContentEmbeddingEnabled()) {
            value.insert("configured",value.value("enabled").toBool());
            value.insert("enabled",false);
            value.insert("reason",QStringLiteral("hash_metadata_only"));
        }
        interrupted(cancellation);
        const auto checked=iiSocietyContainer::SharedStorage::inspect(container,&error);
        if (!checked || !checked->drive().isReady() || storageId(*checked)!=containerId)
            throw std::runtime_error("The selected Society storage changed while reading its content policy.");
        interrupted(cancellation); result.value=value;
    } catch (const std::exception &error) { result.error=QString::fromUtf8(error.what()); result.failure=Result::Failure::retryable; }
      catch (...) { result.error=QStringLiteral("Could not read the content policy."); result.failure=Result::Failure::retryable; }
    return result;
}
Result setTrackedTextPolicy(const QString &container,const std::optional<Profile> &profile,Embedder *embedder,
                            std::stop_token cancellation) {
    if (!fileContentEmbeddingEnabled()) {
        if (profile) return contentIndexDisabled();
        return trackedTextPolicy(container,cancellation);
    }
    Result result;
    try {
        interrupted(cancellation); QString error;
        const auto storage=iiSocietyContainer::SharedStorage::inspect(container,&error);
        if (!storage || !storage->drive().isReady())
            throw std::runtime_error(bytes(error.isEmpty()?QStringLiteral("The Society drive is unavailable."):error));
        const auto containerId=storageId(*storage);
        if (containerId.isEmpty()) throw std::runtime_error("The Society storage identity is unavailable.");
        QString modelRevision;
        if (profile) {
            validateProfile(*profile);
            if (!embedder) throw std::runtime_error("A local embedding service is required to enable automatic indexing.");
            modelRevision=embedder->revision(profile->model,cancellation);
            (void)scopeKey(*profile,modelRevision); interrupted(cancellation);
        } else {
            // Disabling an unconfigured drive is a read-only no-op.
            auto existing=openQueue(*storage,false);
            if (!existing) {
                interrupted(cancellation);
                const auto checked=iiSocietyContainer::SharedStorage::inspect(container,&error);
                if (!checked || !checked->drive().isReady() || storageId(*checked)!=containerId)
                    throw std::runtime_error("The selected Society storage changed while updating its content policy.");
                interrupted(cancellation); result.value={{"containerId",containerId},{"enabled",false}}; return result;
            }
        }
        auto opened=openQueue(*storage,true);
        if (!opened) throw std::runtime_error("The content queue is unavailable.");
        auto db=std::move(*opened);
        const auto checked=iiSocietyContainer::SharedStorage::inspect(container,&error);
        if (!checked || !checked->drive().isReady() || storageId(*checked)!=containerId ||
            (profile && embedder->revision(profile->model,cancellation)!=modelRevision))
            throw std::runtime_error("The selected Society storage or embedding model changed while updating its content policy.");
        interrupted(cancellation);
        auto transaction=take(sql::Transaction::begin(db));
        if (profile) {
            auto update=take(db.prepare("INSERT INTO policy(singleton,model,document_prefix,query_prefix) VALUES(1,?,?,?) "
                "ON CONFLICT(singleton) DO UPDATE SET model=excluded.model,document_prefix=excluded.document_prefix,query_prefix=excluded.query_prefix"));
            bind(update,1,profile->model); bind(update,2,profile->documentPrefix); bind(update,3,profile->queryPrefix);
            (void)take(update.step());
        } else check(db.execute("DELETE FROM policy WHERE singleton=1"));
        interrupted(cancellation);
        interrupted(cancellation); check(transaction.commit());
        result.value={{"containerId",containerId},{"enabled",bool(profile)}};
        if (profile) {
            result.value.insert("model",profile->model); result.value.insert("documentPrefix",profile->documentPrefix);
            result.value.insert("queryPrefix",profile->queryPrefix); result.value.insert("modelRevision",modelRevision);
        }
    } catch (const std::exception &error) { result.error=QString::fromUtf8(error.what()); result.failure=Result::Failure::retryable; }
      catch (...) { result.error=QStringLiteral("Could not update the content policy."); result.failure=Result::Failure::retryable; }
    return result;
}
}
