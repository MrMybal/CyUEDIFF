#pragma once
#include <cyuediff/zen.hpp>
#include <optional>
namespace cyuediff::desktop {
struct ContainerSource {
    fs::path path, script_objects;
    std::string entry;
    ZenProfile profile = ZenProfile::unspecified;
    std::uint64_t max_payload_bytes = 64ull * 1024 * 1024;
};
struct ContainerDocument {
    ContainerSource source;
    std::shared_ptr<const Container> input;
    Json metadata, inspection = nullptr;
    std::optional<ZenPackage> zen;
    std::vector<fs::path> protected_files;
    Json report() const;
};
// Every request indexes current files again. No virtual member path becomes a disk destination.
std::shared_ptr<const ContainerDocument>
load_container_document(const ContainerSource &, const fs::path &codec, const OperationControl & = {});
} // namespace cyuediff::desktop
