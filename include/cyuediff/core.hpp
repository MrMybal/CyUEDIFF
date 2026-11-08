#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <array>
#include <functional>
#include <map>
#include <memory>
namespace cyuediff {
using Json = nlohmann::json;
namespace fs = std::filesystem;
class Error : public std::runtime_error {
  public:
    Error(std::string code, std::string message, std::uint64_t offset = 0);
    const std::string code;
    const std::uint64_t offset;
};
struct OperationProgress {
    std::string phase;
    std::uint64_t completed = 0, total = 0;
};
// Callbacks execute on the calling worker. They must be thread-safe and must not throw.
// Cancellation propagates as Error("cancelled"); it is never converted into opaque data.
struct OperationControl {
    std::function<bool()> cancelled;
    std::function<void(const OperationProgress &)> progress;
    void check() const {
        if (cancelled && cancelled())
            throw Error("cancelled", "Operation cancelled");
    }
    void checkpoint(const char *phase, std::uint64_t completed = 0, std::uint64_t total = 0) const {
        check();
        if (progress)
            progress({phase, completed, total});
        check();
    }
};
enum class NarrowStringEncoding { unspecified, ascii, latin1, windows_1252, utf8 };
class DecoderRegistry;
struct ReadOptions {
    bool properties = true, fingerprints = true, native_graphs = true, native_textures = true,
         native_meshes = true, native_materials = true;
    bool native_skeletons = true, native_skeleton_retargeting = true, native_skeleton_metadata = true;
    bool native_reflection = true;
    std::uint32_t max_table_entries = 1'000'000, max_string_bytes = 4 * 1024 * 1024;
    std::uint64_t max_property_bytes = 32 * 1024 * 1024;
    std::uint32_t max_properties = 100'000, max_depth = 48;
    std::shared_ptr<const DecoderRegistry> decoders;
    OperationControl control;
    NarrowStringEncoding narrow_string_encoding = NarrowStringEncoding::unspecified;
};
struct EngineInfo {
    fs::path root;
    int major = 0, minor = 0, patch = 0;
    Json to_json() const;
    static EngineInfo open(const fs::path &root);
    static EngineInfo resolve(const std::string &association, const fs::path &explicit_root = {});
};
struct MountPoint {
    std::string name;
    fs::path directory;
};
struct ProjectContext {
    fs::path project_file;
    std::string engine_association;
    EngineInfo engine;
    std::vector<MountPoint> mounts;
    std::vector<std::string> diagnostics;
    static ProjectContext open(const fs::path &uproject, const fs::path &engine_root = {});
    fs::path resolve(const std::string &package_name) const;
    std::string package_name(const fs::path &asset) const;
    Json to_json() const;
};
struct PackageSummary {
    int legacy_version = 0, ue4_version = 0, ue5_version = 0, licensee_version = 0;
    std::uint32_t flags = 0;
    std::int32_t header_size = 0, name_count = 0, name_offset = 0;
    std::int32_t import_count = 0, import_offset = 0, export_count = 0, export_offset = 0;
    std::string package_name, saved_engine;
    Json custom_versions = Json::object();
    Json to_json() const;
};
struct ObjectImport {
    std::string name, class_package, class_name, package_name;
    std::int32_t outer_index = 0;
};
struct ObjectExport {
    std::string name, path, class_name, category;
    std::int32_t class_index = 0, outer_index = 0, super_index = 0, template_index = 0;
    std::uint32_t flags = 0;
    std::uint64_t size = 0, offset = 0, script_start = 0, script_end = 0;
    std::uint64_t native_start = 0, native_end = 0;
    Json properties = Json::object(), opaque = Json::object();
    Json native_data = Json::object();
    std::string native_coverage = "unavailable";
    std::string coverage = "metadata";
};
struct BulkPayload {
    fs::path file;
    std::uint64_t offset = 0, stored_bytes = 0, raw_bytes = 0;
    std::uint32_t method = 0;
    std::string storage = "unavailable", content_id, sha256;
};
using TexturePayload = BulkPayload;
// Return the exact number of bytes written. The callback owns its codec and must respect output_bytes.
using BlockDecoder =
    std::function<std::size_t(std::uint32_t, const std::uint8_t *, std::size_t, std::uint8_t *, std::size_t)>;
struct BulkDecodeOptions {
    std::uint64_t max_payload_bytes = 256ull * 1024 * 1024;
    BlockDecoder decoder;
    OperationControl control;
    fs::path virtualized_payload_root;
    // Virtualized reads always verify the full raw BLAKE3. Local reads may opt in.
    bool verify_raw_hash = false;
};
// Loading is explicit; the Core never discovers or executes an engine automatically.
BlockDecoder load_block_decoder(const fs::path &module);
std::vector<std::uint8_t> decode_bulk_payload(const BulkPayload &, const BulkDecodeOptions &options = {});
struct TextureSource {
    std::string object_path, format = "unknown", compression = "TSCF_None";
    std::uint32_t width = 0, height = 0, mips = 0, slices = 0, layers = 0;
    std::vector<std::uint64_t> block_offsets;
    Json settings = Json::object();
    TexturePayload payload;
    std::vector<std::string> diagnostics;
    Json to_json(bool include_location = false) const;
};
struct ImageRgba8 {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> pixels;
};
struct TextureDecodeOptions : BulkDecodeOptions {
    std::uint64_t max_pixels = 64ull * 1024 * 1024;
    std::uint64_t max_decoder_bytes = 512ull * 1024 * 1024;
};
// Half-open pixel region: [x,x+width) x [y,y+height). Zero size means no changed pixels.
struct ImageChangeBounds {
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
    Json to_json() const;
};
struct ImageComparison {
    bool comparable = false, same_size = false;
    std::uint32_t before_width = 0, before_height = 0, after_width = 0, after_height = 0;
    std::uint64_t changed_pixels = 0;
    std::uint32_t max_channel_delta = 0;
    double mean_absolute_error = 0;
    std::uint8_t threshold = 0;
    ImageRgba8 heatmap;
    ImageChangeBounds changed_bounds;
    Json to_json() const;
};
// LDR returns encoded RGBA8 samples. HDR uses the documented display tone map in hdr.hpp.
ImageRgba8 decode_texture(const TextureSource &, std::uint32_t mip = 0,
                          const TextureDecodeOptions &options = {});
ImageComparison compare_images(const ImageRgba8 &, const ImageRgba8 &, std::uint8_t threshold = 0,
                               bool heatmap = false);
ImageComparison compare_images(const ImageRgba8 &, const ImageRgba8 &, std::uint8_t threshold,
                               bool heatmap, const OperationControl &);
void write_png(const fs::path &, const ImageRgba8 &);
struct MeshSource {
    std::string object_path;
    BulkPayload payload;
    int ue5_version = 0;
    Json custom_versions = Json::object();
    std::vector<std::string> diagnostics;
    Json to_json(bool include_location = false) const;
};
struct MeshDecodeOptions : BulkDecodeOptions {
    std::uint32_t max_elements = 1'000'000, max_attribute_values = 8'000'000;
};
struct MeshVertex {
    std::uint32_t id = 0;
    std::array<float, 3> position{};
};
struct MeshInstance {
    std::uint32_t id = 0, vertex = 0;
    std::vector<std::array<float, 2>> uvs;
    std::array<float, 3> normal{}, tangent{};
    std::array<float, 4> color{1, 1, 1, 1};
    float binormal_sign = 0;
};
struct MeshTriangle {
    std::uint32_t id = 0, polygon = 0, group = 0;
    std::array<std::uint32_t, 3> instances{};
};
struct MeshSkin;
struct MeshMorphs;
struct MeshGeometry {
    std::vector<MeshVertex> vertices;
    std::vector<MeshInstance> instances;
    std::vector<MeshTriangle> triangles;
    std::map<std::uint32_t, std::string> material_slots;
    Json attributes = Json::object(), bounds = Json::object();
    std::shared_ptr<const MeshSkin> skin;
    std::shared_ptr<const MeshMorphs> morphs;
    Json to_json() const;
};
struct MeshComparison {
    Json changes = Json::array();
    Json to_json() const;
};
MeshGeometry decode_mesh(const MeshSource &, const MeshDecodeOptions &options = {});
MeshComparison compare_meshes(const MeshGeometry &, const MeshGeometry &);
void write_obj(const fs::path &, const MeshGeometry &);
struct Package {
    fs::path source;
    PackageSummary summary;
    std::vector<std::string> names;
    std::vector<ObjectImport> imports;
    std::vector<ObjectExport> exports;
    std::vector<TextureSource> textures;
    std::vector<MeshSource> mesh_sources;
    Json fingerprints = Json::object();
    std::vector<std::string> diagnostics;
    bool complete_input = true;
    static Package open(const fs::path &file, const ReadOptions &options = {});
    std::string reference(std::int32_t index) const;
    Json semantic_snapshot() const;
    // Graphs, nodes and pins are keyed by GUID; dangling connections stay visible.
    Json blueprint_snapshot() const;
    Json texture_snapshot() const;
    Json mesh_snapshot() const;
    Json material_snapshot() const;
    Json collision_snapshot() const;
    Json skeleton_snapshot() const;
    const MeshSource &mesh_source(const std::string &object = "", std::uint32_t lod = 0) const;
    Json to_json() const;
};
struct DiffResult {
    bool binary_comparison_available = false, binary_changed = false, complete_input = true;
    Json changes = Json::array();
    Json graph_changes = Json::array();
    Json texture_changes = Json::array();
    Json mesh_changes = Json::array();
    Json material_changes = Json::array();
    Json collision_changes = Json::array();
    Json skeleton_changes = Json::array();
    std::vector<std::string> diagnostics;
    std::string status() const;
    Json to_json() const;
};
DiffResult compare(const Package &before, const Package &after, const OperationControl &control = {});
struct MaterialComparison {
    Json changes = Json::array();
    bool complete = false;
    std::string status() const;
    Json to_json() const;
};
// Compares the serialized graph/parameter projection; no parent or shader evaluation.
MaterialComparison compare_materials(const Package &before, const Package &after);
std::string sha256_file(const fs::path &path, const OperationControl &control = {});
std::string sha256_bytes(const std::vector<std::uint8_t> &bytes, const OperationControl &control = {});
} // namespace cyuediff
