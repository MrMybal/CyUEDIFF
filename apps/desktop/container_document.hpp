#pragma once
#include <cyuediff/schema_context.hpp>
#include <optional>
namespace cyuediff::desktop {
struct ContainerSource {
    fs::path path, script_objects, schema;
    std::string entry;
    ZenProfile profile = ZenProfile::unspecified;
    fs::path after_path, after_script_objects, after_schema;
    ZenProfile after_profile = ZenProfile::unspecified;
    bool compare_payloads = false;
    std::uint64_t max_comparison_bytes = 512ull * 1024 * 1024;
    std::uint64_t max_payload_bytes = 64ull * 1024 * 1024;
};
struct ContainerDocument {
    ContainerSource source;
    std::shared_ptr<const Container> input;
    Json metadata, inspection = nullptr;
    std::optional<ZenPackage> zen;
    std::optional<ZenSchemaManifest> schema_manifest;
    bool schema_loaded = false;
    std::shared_ptr<const ContainerDocument> after;
    std::vector<ContainerPair> members;
    std::vector<std::string> member_states;
    Json comparison = nullptr, selected_comparison = nullptr;
    std::vector<fs::path> protected_files;
    Json report() const;
};
// Every request indexes current files again. No virtual member path becomes a disk destination.
std::shared_ptr<const ContainerDocument>
load_container_document(const ContainerSource &, const fs::path &codec, const OperationControl & = {});
} // namespace cyuediff::desktop
