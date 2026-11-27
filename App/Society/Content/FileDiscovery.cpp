#include "FileDiscovery.h"
#include "Shared/NativeObservedPath.h"

#include <FileCatalog.h>
#include <DirectoryMetadataIndex.h>
#include <ObjectSource.h>
#include <iiFileProviderPresentation.h>
#include <iiSocietyContainer>
#include <QFileInfo>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace society::content {
namespace {
namespace fs=std::filesystem;
constexpr int maximumEntries=32;
constexpr std::size_t maximumDepth=128;

// Preserve native UTF-8 spelling; invalid names cannot be safely represented by
// the managed-path API and are left for an explicit recovery or future adapter.
QString displayed(const fs::path &path) {
    const auto bytes=path.generic_u8string();
    const auto result=QString::fromUtf8(reinterpret_cast<const char *>(bytes.data()),qsizetype(bytes.size()));
    if (result.toUtf8()!=QByteArray(reinterpret_cast<const char *>(bytes.data()),qsizetype(bytes.size())))
        throw std::runtime_error("A file name is not valid UTF-8.");
    return result;
}

// Match the SDK's public-section boundary instead of following hidden control
// records, redirected paths, or the internals of managed project packages.
bool visible(const fs::path &name) {
    const auto value=name.generic_u8string();
    return !value.empty() && value.front()!=u8'.' &&
        std::ranges::find(value,u8'\\')==value.end() && std::ranges::find(value,u8':')==value.end() &&
        !iiFileProvider::native::Presentation::isControlFile(name);
}

// Distinguish the logical-disk storage identity from the package identity so a
// scan cannot silently continue against a newly selected backing store.
QString identity(const iiSocietyContainer::SharedStorage &storage) {
    const auto &drive=storage.drive();
    return drive.storageKind()==iiSocietyContainer::SocietyDrive::StorageKind::LogicalDisk
        ? drive.storageIdentifier() : drive.identifier();
}

// Automatic intake must not adopt any legacy classification. Its owner and
// history require a separate, verified migration even when the path matches.
void verifyClassification(const iiSocietyContainer::SharedStorage &storage,
                          const fs::path &root,const fs::path &native,const std::string &logical) {
    if (!logical.starts_with("files/") || logical.size()<=6)
        throw std::runtime_error("The managed intake path is invalid.");
    const auto container=societyNativeObservedPath(storage.drive().rootPath());
    auto legacy=iiSocietyContainer::FileRepository::classificationDirectory(container,"files")/native.lexically_relative(root);
    legacy += ".iisacc.json";
    std::error_code statusError;
    const auto legacyStatus=fs::symlink_status(legacy,statusError);
    if ((statusError && statusError!=std::errc::no_such_file_or_directory) || fs::exists(legacyStatus) || fs::is_symlink(legacyStatus))
        throw std::runtime_error("A legacy classification requires separate reconciliation.");
    // Validate existing ancestors too, including redirected private paths.
    if (!iiFileProvider::FileCatalog::classificationOwnerAt(native,legacy).empty())
        throw std::runtime_error("A legacy classification requires separate reconciliation.");
    // A missing sidecar does not mean the catalog abandoned its native file.
    // Check both the old path and native identity before assigning a new owner.
    const auto catalogPath=root/".iisacc-file-provider"/"catalog.sqlite3";
    statusError.clear();
    const auto catalogStatus=fs::symlink_status(catalogPath,statusError);
    if (statusError && statusError!=std::errc::no_such_file_or_directory)
        throw std::runtime_error("The legacy catalog cannot be inspected.");
    if (fs::exists(catalogStatus) || fs::is_symlink(catalogStatus)) {
        iiFileProvider::FileCatalog catalog(root,iiFileProvider::FileCatalog::Access::ReadOnly,
            iiSocietyContainer::FileRepository::classificationDirectory(container,"files"));
        const auto bytes=native.lexically_relative(root).generic_u8string();
        const std::string relative(reinterpret_cast<const char *>(bytes.data()),bytes.size());
        if (catalog.lookupPath(relative) || catalog.lookupIdentity(relative))
            throw std::runtime_error("A legacy catalog owner requires separate reconciliation.");
    }
}
}

// Retain only a depth-bounded traversal and one yielded candidate between
// slices; no filenames, cursor or source bytes are persisted by the walker.
class FileDiscovery::Impl final {
public:
    // Each frame owns the next entry in one currently open directory.
    struct Frame final { fs::directory_iterator next; };
    explicit Impl(QString path,Candidate filter,Mode selected) : container(std::move(path)),candidate(std::move(filter)),mode(selected) {}

    QString container,containerId,lastError,lastPath;
    Candidate candidate;
    Mode mode=Mode::Observe;
    fs::path root;
    std::vector<Frame> frames;
    std::optional<fs::path> pending;
    bool again=false;
    int passFailures=0;
};

// Selection and filtering belong to the host; construction performs no I/O.
FileDiscovery::FileDiscovery(QString container,Candidate candidate,Mode mode)
    : m_impl(std::make_unique<Impl>(std::move(container),std::move(candidate),mode)) {}
// Destroying a walker releases its open directory iterators, not source data.
FileDiscovery::~FileDiscovery()=default;

// Observe a bounded slice, yielding to user work and preserving failed files
// for a later pass without translating an incomplete scan into deletions.
FileDiscovery::Batch FileDiscovery::step(bool changed,std::stop_token cancellation) {
    Batch result;
    int visited=0,observed=0,unchanged=0,failed=0;
    QString lastError,lastPath,failedPath;
    const auto noteFailure=[&](QString message) {
        ++failed; lastError=std::move(message); failedPath=lastPath;
    };
    try {
        if (cancellation.stop_requested()) throw std::runtime_error("File intake was cancelled.");
        if (!m_impl->candidate) throw std::runtime_error("A file intake policy is required.");
        QString error;
        const auto storage=iiSocietyContainer::SharedStorage::inspect(m_impl->container,&error);
        if (!storage || !storage->drive().isReady())
            throw std::runtime_error(error.isEmpty()?"The Society drive is unavailable.":error.toStdString());
        if (iiSocietyContainer::FileRepository::requiresLegacyMigration(societyNativeObservedPath(storage->drive().rootPath())))
            throw std::runtime_error("Legacy file history requires offline migration before discovery.");
        const auto currentId=identity(*storage);
        const auto section=storage->filePath(iiSocietyContainer::StoreSection::Files,{},&error);
        const QFileInfo sectionInfo(section);
        if (currentId.isEmpty() || section.isEmpty() || !sectionInfo.isDir() || sectionInfo.isSymLink() || sectionInfo.isJunction())
            throw std::runtime_error("The Files section is unavailable or redirected.");
        const auto root=societyNativeObservedPath(section);
        if (m_impl->containerId.isEmpty()) { m_impl->containerId=currentId; m_impl->root=root; }
        if (currentId!=m_impl->containerId || root!=m_impl->root)
            throw std::runtime_error("The selected container changed during file intake.");
        result.directories.append(section);
        if (changed && (!m_impl->frames.empty() || m_impl->pending)) m_impl->again=true;
        if (m_impl->frames.empty() && !m_impl->pending) {
            std::error_code ec; fs::directory_iterator first(root,fs::directory_options::none,ec);
            if (ec) throw std::runtime_error("Cannot open the Files section for intake: "+ec.message());
            m_impl->passFailures=0;
            m_impl->frames.push_back({std::move(first)});
        }
        const auto container=societyNativeObservedPath(storage->drive().rootPath());
        const auto package=iiSocietyContainer::FileRepository::packagePath(container);
        std::unique_ptr<iiSocietyContainer::FileRepository> repository;
        if (fs::exists(package/"objects.sqlite3"))
            repository=storage->fileRepository({},iiSocietyContainer::FileRepository::Access::ReadOnly,
                                                iiSocietyContainer::FileRepository::Intent::Background);
        bool attempted=false;
        while (visited<maximumEntries && !attempted) {
            if (cancellation.stop_requested()) throw std::runtime_error("File intake was cancelled.");
            lastPath.clear();
            fs::path path;
            if (m_impl->pending) path=*m_impl->pending;
            else {
                while (!m_impl->frames.empty() && m_impl->frames.back().next==fs::directory_iterator{})
                    m_impl->frames.pop_back();
                if (m_impl->frames.empty()) break;
                auto &iterator=m_impl->frames.back().next;
                path=iterator->path();
                std::error_code ec; iterator.increment(ec);
                if (ec) { noteFailure(QString::fromStdString(ec.message())); iterator=fs::directory_iterator{}; }
                ++visited;
            }
            try {
                const auto relative=path.lexically_relative(root);
                if (relative.empty() || relative.is_absolute() || *relative.begin()==".." || !visible(path.filename())) {
                    m_impl->pending.reset(); continue;
                }
                // A parent may have been replaced while its directory iterator
                // was retained between slices. Revalidate it before inspecting
                // or descending into another entry from that iterator.
                const auto parent=displayed(relative.parent_path());
                const auto checkedParent=storage->filePath(iiSocietyContainer::StoreSection::Files,parent,&error);
                if (checkedParent.isEmpty() || !QFileInfo(checkedParent).isDir())
                    throw std::runtime_error("The intake parent is unavailable or redirected.");
                std::error_code ec;
                const auto type=fs::symlink_status(path,ec);
                if (ec) { noteFailure(QString::fromStdString(ec.message())); m_impl->pending.reset(); continue; }
                if (fs::is_symlink(type)) { m_impl->pending.reset(); continue; }
                const auto shown=displayed(path),name=displayed(relative);
                lastPath=QStringLiteral("Files/")+name;
                const QFileInfo info(shown);
                if (info.isSymLink() || info.isJunction()) { m_impl->pending.reset(); continue; }
                if (fs::is_directory(type)) {
                    m_impl->pending.reset();
                    if (path.extension()==".iiproject") continue;
                    if (m_impl->frames.size()>=maximumDepth) { noteFailure("The file intake depth limit was reached."); continue; }
                    const auto checked=storage->filePath(iiSocietyContainer::StoreSection::Files,name,&error);
                    if (checked.isEmpty() || !QFileInfo(checked).isDir())
                        throw std::runtime_error("The intake directory is unavailable or redirected.");
                    fs::directory_iterator child(societyNativeObservedPath(checked),fs::directory_options::none,ec);
                    if (ec) { noteFailure(QString::fromStdString(ec.message())); continue; }
                    if (storage->filePath(iiSocietyContainer::StoreSection::Files,name,&error).isEmpty())
                        throw std::runtime_error("The intake directory changed while opening.");
                    result.directories.append(checked); m_impl->frames.push_back({std::move(child)}); continue;
                }
                if (!fs::is_regular_file(type)) { m_impl->pending.reset(); continue; }
                const auto size=fs::file_size(path,ec);
                if (ec) { noteFailure(QString::fromStdString(ec.message())); m_impl->pending.reset(); continue; }
                if (!m_impl->candidate(name,size)) { m_impl->pending.reset(); continue; }
                const auto native=storage->filePath(iiSocietyContainer::StoreSection::Files,name,&error);
                if (native.isEmpty()) throw std::runtime_error(error.toStdString());
                const auto resolved=societyNativeObservedPath(native);
                const auto stamp=iiFileProvider::ObjectSource::inspect(resolved);
                if (!m_impl->candidate(name,stamp.size)) { m_impl->pending.reset(); continue; }
                const auto logical=storage->managedFilePath(iiSocietyContainer::StoreSection::Files,name);
                const auto address=displayed(resolved.lexically_relative(container)).toUtf8().toStdString();
                // Keep work bounded to one candidate; filesystem events remain
                // hints, and a later pass retries per-file failures.
                m_impl->pending=path; attempted=true;
                enum class Outcome { ignored,unchanged,observed };
                auto outcome=Outcome::ignored;
                const auto admitFile=[&](std::stop_token stop) {
                    if (stop.stop_requested()) throw std::runtime_error("File intake was cancelled.");
                    const auto current=storage->filePath(iiSocietyContainer::StoreSection::Files,name,&error);
                    if (current.isEmpty()) throw std::runtime_error("The file changed during intake.");
                    const auto currentNative=societyNativeObservedPath(current);
                    const auto fresh=iiFileProvider::ObjectSource::inspect(currentNative);
                    if (!m_impl->candidate(name,fresh.size)) return;
                    const auto currentLogical=storage->managedFilePath(iiSocietyContainer::StoreSection::Files,name);
                    if (currentLogical!=logical) throw std::runtime_error("The file path changed during intake.");
                    // Keep legacy resources in their original store without
                    // capturing a new version or transferring their ownership.
                    if (repository && repository->objects().lookupPath(logical)) { outcome=Outcome::unchanged; return; }
                    verifyClassification(*storage,root,currentNative,logical);
                    auto companion=currentNative; companion += ".iisacc.json";
                    const auto companionStatus=fs::symlink_status(companion);
                    if (fs::exists(companionStatus) || fs::is_symlink(companionStatus)) {
                        const auto indexPath=container/".iisacc-file-provider"/"metadata-paths.sqlite3";
                        if (!fs::exists(indexPath)) throw std::runtime_error("A classification requires separate reconciliation.");
                        iiFileProvider::DirectoryMetadataIndex index(container,iiFileProvider::DirectoryMetadataIndex::Access::ReadOnly);
                        if (!index.contains(address)) throw std::runtime_error("A classification requires separate reconciliation.");
                        (void)index.inspectAsset(address);
                    }
                    if (m_impl->mode==Mode::HashMetadata) {
                        iiFileProvider::DirectoryMetadataIndex index(container,iiFileProvider::DirectoryMetadataIndex::Access::Create);
                        (void)index.indexAsset(address,stop);
                    }
                    outcome=Outcome::observed;
                };
                admitFile(cancellation);
                if (outcome==Outcome::observed) ++observed;
                else if (outcome==Outcome::unchanged) ++unchanged;
                if (outcome!=Outcome::ignored) {
                    result.files.append(native);
                    result.logicalFiles.append(QStringLiteral("Files/")+name);
                }
                m_impl->pending.reset();
            } catch (const std::exception &failure) {
                if (cancellation.stop_requested()) throw;
                noteFailure(QString::fromUtf8(failure.what())); m_impl->pending.reset();
            }
        }
        while (!m_impl->frames.empty() && m_impl->frames.back().next==fs::directory_iterator{})
            m_impl->frames.pop_back();
        m_impl->passFailures+=failed;
        if (!lastError.isEmpty()) { m_impl->lastError=lastError; m_impl->lastPath=failedPath; }
        if (m_impl->frames.empty() && !m_impl->pending && !m_impl->passFailures) {
            m_impl->lastError.clear(); m_impl->lastPath.clear();
        }
        bool working=m_impl->pending.has_value() || !m_impl->frames.empty();
        if (!working && m_impl->again) { m_impl->again=false; working=true; }
        result.status={{"state",working?QStringLiteral("working"):QStringLiteral("idle")},
                       {"visited",visited},{"observed",observed},{"unchanged",unchanged},
                       {"failed",failed},{"passFailures",m_impl->passFailures},{"completeness",QStringLiteral("unknown")}};
        if (!m_impl->lastError.isEmpty()) result.status.insert("lastError",m_impl->lastError);
        if (!m_impl->lastPath.isEmpty()) result.status.insert("lastPath",m_impl->lastPath);
    } catch (const std::exception &failure) { result.error=QString::fromUtf8(failure.what()); }
    catch (...) { result.error=QStringLiteral("File intake failed."); }
    return result;
}
}
