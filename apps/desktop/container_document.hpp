#pragma once
#include <cyuediff/schema_context.hpp>
#include <cyuediff/cooked_texture.hpp>
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
struct ContainerTexturePreview {
    std::string key, object_path, format;
    std::uint32_t mip = 0;
    ImageRgba8 image;
    std::optional<ImageRgba32f> hdr;
};
struct ContainerTextureDifference {
    std::string key;
    ImageComparison comparison;
    std::optional<HdrImageComparison> hdr;
};
struct ContainerDocument {
    ContainerSource source;
    std::shared_ptr<const Container> input;
    Json metadata, inspection = nullptr;
    std::optional<ZenPackage> zen;
    std::optional<ZenSchemaManifest> schema_manifest;
    bool schema_loaded = false;
    Json textures = Json::array();
    std::vector<ContainerTexturePreview> previews;
    std::vector<ContainerTextureDifference> pixel_differences;
    std::vector<std::string> texture_diagnostics;
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
