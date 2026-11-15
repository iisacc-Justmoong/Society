#include "ContainerResourceAdapter.h"
#include <FileRepository.h>
#include <ObjectSource.h>
#include <filesystem>

namespace society::resources {
namespace r=ii::resource;
namespace {
std::string text(const std::filesystem::path &path) {
    const auto value=path.generic_u8string();
    return {reinterpret_cast<const char *>(value.data()),value.size()};
}
}
std::expected<r::Snapshot,r::Error> project(const iiSocietyContainer::ResourceRecord &record) {
    r::Snapshot snapshot;
    snapshot.resource.id={record.id};
    snapshot.resource.displayPath=record.location;
    const auto separator=record.location.find_last_of('/');
    snapshot.resource.displayName=separator==std::string::npos?record.location:record.location.substr(separator+1);
    if (!record.contentRevision.empty()) {
        snapshot.resource.observedRevision=r::RevisionId{record.contentRevision};
        r::ContentRevision revision{{record.contentRevision},{record.id},{},{}};
        if (const auto hash=record.hashes.find("sha256");hash!=record.hashes.end() && !hash->second.empty())
            revision.digest=r::Digest{"sha256",hash->second,r::Evidence::observed};
        snapshot.revision=std::move(revision);
    }
    const auto locator=text(record.localLocator);
    if (!record.containerId.empty() || !locator.empty())
        snapshot.locations.push_back({{},record.containerId,{},locator,r::Availability::unknown});
    for (const auto &replica:record.replicas)
        snapshot.replicas.push_back({{replica.id},{record.id},{replica.revision},{},replica.locator,r::ReplicaState::unknown});
    if (const auto error=r::validate(snapshot);error!=r::Error::none) return std::unexpected(error);
    return snapshot;
}
std::optional<r::Snapshot> projectCurrent(iiSocietyContainer::FileRepository &repository,
                                           const std::string &managedPath,const std::filesystem::path &nativePath) {
    const auto object=repository.lookup(managedPath);
    if (!object || object->deleted || object->sourceStamp.empty()) return {};
    const auto record=repository.resource(managedPath);
    if (!record || record->id!=object->key) return {};
    // Inspect metadata only; compare again against the repository after the
    // filesystem observation so a concurrently updated record is not mixed in.
    const auto observed=iiFileProvider::ObjectSource::inspect(nativePath);
    const auto current=repository.lookup(managedPath);
    if (observed.stamp!=object->sourceStamp || !current || current->deleted ||
        current->key!=object->key || current->version!=object->version ||
        current->sourceStamp!=object->sourceStamp) return {};
    auto snapshot=project(*record);
    if (!snapshot) return {};
    return std::move(*snapshot);
}
}
