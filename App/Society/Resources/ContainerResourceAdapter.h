#pragma once

#include <iiResource>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace iiSocietyContainer { struct ResourceRecord; class FileRepository; }
namespace society::resources {
// Translate the container's last-observed compatibility projection without
// changing the container's persisted data or promoting declarations to facts.
[[nodiscard]] std::expected<ii::resource::Snapshot,ii::resource::Error>
project(const iiSocietyContainer::ResourceRecord &record);
// A path may be reused by another file. Only return the observation when the
// native source stamp and the unchanged repository record agree.
[[nodiscard]] std::optional<ii::resource::Snapshot> projectCurrent(
    iiSocietyContainer::FileRepository &repository,const std::string &managedPath,
    const std::filesystem::path &nativePath);
}
