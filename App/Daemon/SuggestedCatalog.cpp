#include "SuggestedCatalog.h"
#include "Society/Resources/ContainerResourceAdapter.h"
#include <iiSocietyContainer>
#include <iiFileProvider>
#include <QDir>
#include <QFileInfo>
#include <QStringDecoder>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>

namespace society::suggested {
namespace {
namespace fs=std::filesystem;
using Storage=iiSocietyContainer::SharedStorage;
using Repository=iiSocietyContainer::FileRepository;
using Catalog=iiFileProvider::FileCatalog;
using Section=iiSocietyContainer::StoreSection;
QString text(const std::string &s) { return QString::fromUtf8(s.data(),qsizetype(s.size())); }
std::string bytes(const QString &s) { return s.toUtf8().toStdString(); }
fs::path nativePath(const QString &s) {
#ifdef _WIN32
    return fs::path(s.toStdWString());
#else
    return fs::path(s.toStdString());
#endif
}
bool cancelled(const std::atomic_bool *c) { return c && c->load(); }
bool privatePath(const QString &path) {
    for (const auto &part:path.split('/'))
        if (part.isEmpty() || part=="." || part==".." || part.startsWith('.') || part.endsWith(".iisacc.json",Qt::CaseInsensitive)) return true;
    return false;
}
bool textFile(const QString &path) {
    const auto suffix=QFileInfo(path).suffix().toLower();
    return suffix=="md" || suffix=="markdown" || suffix=="txt";
}
struct Reader {
    Storage storage;
    std::unique_ptr<Repository> repository;
    std::unique_ptr<Catalog> catalog;
};
std::optional<Reader> open(const std::string &container, std::string &error, bool &pending) {
    QString problem;
    auto storage=Storage::open(text(container),&problem);
    if (!storage) { error=bytes(problem); return {}; }
    const auto &drive=storage->drive();
    const auto root=fs::path(drive.rootPath().toStdU16String());
    const auto section=fs::path(drive.sectionPath(Section::Files).toStdU16String());
    if (!fs::exists(Repository::packagePath(root)) || !fs::exists(section/".iisacc-file-provider/catalog.sqlite3")) {
        pending=true; return {};
    }
    auto repository=storage->fileRepository({},Repository::Access::ReadOnly,Repository::Intent::Background);
    auto catalog=std::make_unique<Catalog>(section,Catalog::Access::ReadOnly,Repository::classificationDirectory(root,"files"));
    return Reader{std::move(*storage),std::move(repository),std::move(catalog)};
}
std::optional<Candidate> candidate(Reader &reader,const iiFileProvider::CatalogFileSummary &summary) {
    const auto relative=text(summary.path);
    if (privatePath(relative)) return {};
    const auto native=reader.storage.filePath(Section::Files,relative);
    if (native.isEmpty() || !QFileInfo(native).isFile()) return {};
    const auto record=reader.catalog->lookup(summary.key);
    if (!record || record->path!=summary.path || record->revision!=summary.revision ||
        record->contentVersion!=summary.contentVersion || record->contentVersion.empty() || record->metadataPending) return {};
    auto projected=society::resources::projectCurrent(*reader.repository,"files/"+summary.path,nativePath(native));
    if (!projected) return {};
    // Provider read verifies current native identity and the expected working
    // content version before returning a small bounded text prefix.
    const auto maximum=textFile(relative) ? std::size_t(4096) : std::size_t(1);
    const auto chunk=reader.catalog->read(record->key,0,maximum,record->contentVersion);
    Evidence e; e.resourceId=projected->resource.id.value; e.version=record->contentVersion;
    e.path=bytes(native); e.name=bytes(QFileInfo(native).fileName());
    e.description=record->metadata.description.substr(0,2048);
    if (textFile(relative) && !chunk.bytes.empty()) {
        const auto raw=QByteArray(reinterpret_cast<const char *>(chunk.bytes.data()),qsizetype(chunk.bytes.size()));
        QStringDecoder decoder(QStringDecoder::Utf8); const QString decoded=decoder.decode(raw);
        if (!decoder.hasError() && !decoded.contains(QChar::Null)) e.text=bytes(decoded.left(2048));
    }
    {
        const auto &fields=record->metadata.fields;
        bool inferred=false;
        for (const auto *key:{"workflow.status.source","workflow:status:source","status.source","status_source"}) {
            const auto provenance=fields.find(key); if (provenance==fields.end()) continue;
            const auto source=text(provenance->second).trimmed().toLower();
            inferred=inferred || source=="ai" || source=="model" || source=="inferred" || source=="agent";
        }
        if (!inferred) for (const auto *key:{"workflow.status","workflow:status","status"}) {
            const auto it=fields.find(key); if (it!=fields.end()) e.statuses.push_back(it->second);
        }
    }
    return Candidate{std::move(e),record->key,std::move(*projected),record->revision,record->size,record->modifiedAtNs};
}
}

CatalogSnapshot SuggestedCatalog::scan(const std::string &container,std::size_t limit,const std::atomic_bool *cancel) {
    CatalogSnapshot result;
    if (!limit) { result.partial=true; return result; }
    try {
        auto reader=open(container,result.error,result.pending); if (!reader) return result;
        std::optional<iiFileProvider::FileCursor> cursor;
        std::size_t visited=0;
        do {
            if (cancelled(cancel)) return {};
            // Read the catalog before the repository filters inaccessible or
            // unprojected entries, so a partial view is never presented as a
            // complete inventory. candidate() still verifies each resource.
            const auto page=reader->catalog->search({},cursor,std::min<std::size_t>(128,limit-visited));
            for (const auto &item:page.files) {
                if (cancelled(cancel)) return {};
                ++visited;
                if (privatePath(text(item.path))) continue;
                try {
                    if (auto value=candidate(*reader,item)) result.items.push_back(std::move(*value));
                    else result.partial=true;
                }
                catch (const std::exception &) { result.partial=true; }
            }
            cursor=page.next;
            if (cursor && visited>=limit) { result.partial=true; break; }
        } while (cursor);
    } catch (const std::exception &e) { result.error=e.what(); }
    return result;
}
bool SuggestedCatalog::validate(const std::string &container,const Candidate &value) {
    try {
        std::string error; bool pending=false; auto reader=open(container,error,pending); if (!reader) return false;
        const auto current=reader->catalog->lookup(value.catalogKey);
        if (!current || current->revision!=value.metadataRevision || current->contentVersion!=value.evidence.version) return false;
        const auto native=reader->storage.filePath(Section::Files,text(current->path));
        if (bytes(native)!=value.evidence.path) return false;
        const auto projected=society::resources::projectCurrent(*reader->repository,"files/"+current->path,nativePath(native));
        if (!projected || projected->resource.id!=value.resource.resource.id ||
            projected->resource.observedRevision!=value.resource.resource.observedRevision ||
            projected->resource.id.value!=value.evidence.resourceId) return false;
        (void)reader->catalog->read(current->key,0,1,current->contentVersion);
        return true;
    } catch (...) { return false; }
}
}
