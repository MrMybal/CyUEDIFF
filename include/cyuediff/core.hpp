#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <array>
#include <functional>
#include <iosfwd>
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
// Random-access bytes of one input: a local file, memory, a version-control blob, a network
// range... The Core reads packages only through sources, so hosts keep their own storage and
// versioning systems. See docs/BYTE_SOURCES.md.
// Contract: size() is constant; read() fills exactly `bytes` bytes at `offset`, where
// offset + bytes <= size(), or throws. Const calls may run concurrently from several threads.
class ByteSource {
  public:
    virtual ~ByteSource() = default;
    virtual std::uint64_t size() const = 0;
    virtual void read(std::uint64_t offset, std::uint8_t *destination, std::size_t bytes) const = 0;
    // Label for diagnostics (file name, depot path, URL...). The Core never opens it.
    virtual std::string name() const { return {}; }
    // Optional sequential session holding a resource (file handle, connection) while one
    // decoder reads. Sessions are used by a single thread. The default forwards to read().
    class Session {
      public:
        virtual ~Session() = default;
        virtual void read(std::uint64_t offset, std::uint8_t *destination, std::size_t bytes) = 0;
    };
    virtual std::unique_ptr<Session> session() const;
};
using ByteSourcePtr = std::shared_ptr<const ByteSource>;
// Local file. Its size is captured now; each session reopens it and rejects a size change
// with Error("source-changed"). No handle stays open between sessions.
ByteSourcePtr file_source(const fs::path &);
// Owned bytes (moved in).
ByteSourcePtr memory_source(std::vector<std::uint8_t> bytes, std::string name = {});
// Zero-copy view of host memory, kept alive by `owner` for as long as the source exists.
ByteSourcePtr memory_source(std::shared_ptr<const void> owner, const std::uint8_t *data, std::size_t size,
                            std::string name = {});
// Host callback (VCS API, network range reads...). It must follow the ByteSource::read contract.
using ByteReadCallback = std::function<void(std::uint64_t offset, std::uint8_t *destination, std::size_t bytes)>;
ByteSourcePtr callback_source(std::uint64_t size, ByteReadCallback read, std::string name = {});
// One package and the companion files of the same revision, keyed by extension. A missing key
// means "absent": with explicit sources the Core never looks for siblings on disk.
struct PackageSources {
    ByteSourcePtr package;
    // Keys: ".uexp", ".ubulk", ".uptnl", ".m.ubulk", ".o.ubulk", ".upayload".
    std::map<std::string, ByteSourcePtr> companions;
    // Logical asset path (repository/depot path, file name...) reported as Package::source.
    // Empty: package->name().
    fs::path name;
    static const std::array<const char *, 6> &companion_extensions();
    // The package file plus the companions present beside it on disk.
    static PackageSources files(const fs::path &asset);
    // Null when absent.
    ByteSourcePtr companion(const std::string &extension) const;
    // Rejects a null package source, unknown extensions and null companions.
    void validate() const;
};
// Optional side observations. Limits affect identity availability, not decoded stored values.
struct ValueIdentityOptions {
    bool names = false;
    std::uint32_t max_names = 100'000;
    std::uint64_t max_text_bytes = 4ull * 1024 * 1024;
    std::function<void(std::uint32_t, std::uint64_t)> charge;
};
// Required typed-name correspondence, collected even when optional observations are disabled.
// Exhaustion preserves readable values but prevents certification of affected name comparisons.
struct NameValueReadOptions {
    std::uint32_t max_names = 100'000;
    std::uint64_t max_text_bytes = 4ull * 1024 * 1024;
    std::function<void(std::uint32_t, std::uint64_t)> charge;
};
enum class NarrowStringEncoding { unspecified, ascii, latin1, windows_1252, utf8 };
class DecoderRegistry;
struct ReadOptions {
    bool properties = true, fingerprints = true, native_graphs = true, native_textures = true,
         native_meshes = true, native_materials = true;
    bool native_skeletons = true, native_skeleton_retargeting = true, native_skeleton_metadata = true;
    bool native_reflection = true, native_metadata = true;
    std::uint32_t max_table_entries = 1'000'000, max_string_bytes = 4 * 1024 * 1024;
    std::uint64_t max_property_bytes = 32 * 1024 * 1024;
    std::uint32_t max_properties = 100'000, max_depth = 48;
    std::shared_ptr<const DecoderRegistry> decoders;
    OperationControl control;
    NarrowStringEncoding narrow_string_encoding = NarrowStringEncoding::unspecified;
    ValueIdentityOptions value_identities;
    NameValueReadOptions name_values;
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
    // Location label for reports; bytes are read from `source` when set, else from `file`.
    fs::path file;
    // Keeps the payload's bytes alive and readable without the original file.
    ByteSourcePtr source;
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
    // Host store for virtualized payloads (DDC, VCS virtualization backend...), keyed by the
    // 40-digit FIoHash. Takes precedence over virtualized_payload_root. Return null when absent.
    std::function<ByteSourcePtr(const std::string &content_id)> virtualized_payload_source;
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
struct MeshCloth;
struct MeshVertexOverlaps;
struct MeshVertexAttributes;
class MeshRenderMorphs;
struct MeshGeometry {
    std::vector<MeshVertex> vertices;
    std::vector<MeshInstance> instances;
    std::vector<MeshTriangle> triangles;
    std::map<std::uint32_t, std::string> material_slots;
    Json attributes = Json::object(), bounds = Json::object();
    std::shared_ptr<const MeshSkin> skin;
    std::shared_ptr<const MeshMorphs> morphs;
    std::shared_ptr<const MeshCloth> cloth;
    std::shared_ptr<const MeshVertexOverlaps> vertex_overlaps;
    std::shared_ptr<const MeshVertexAttributes> vertex_attributes;
    std::shared_ptr<const MeshRenderMorphs> render_morphs;
    Json to_json() const;
};
struct MeshComparison {
    Json changes = Json::array();
    bool complete = true;
    std::vector<std::string> diagnostics;
    Json to_json() const;
};
MeshGeometry decode_mesh(const MeshSource &, const MeshDecodeOptions &options = {});
MeshComparison compare_meshes(const MeshGeometry &, const MeshGeometry &);
void write_obj(const fs::path &, const MeshGeometry &);
struct Package {
    // Canonical file path, or PackageSources::name for explicit sources.
    fs::path source;
    // Byte sources this package was read from; null for hand-built packages, which fall back
    // to files beside `source`.
    std::shared_ptr<const PackageSources> inputs;
    PackageSummary summary;
    std::vector<std::string> names;
    std::vector<ObjectImport> imports;
    std::vector<ObjectExport> exports;
    std::vector<TextureSource> textures;
    std::vector<MeshSource> mesh_sources;
    Json fingerprints = Json::object();
    std::vector<std::string> diagnostics;
    bool complete_input = true;
    Json source_type_context = nullptr;
    // Paths address semantic_snapshot(); observations do not modify asset members.
    Json value_identities = nullptr, name_values = nullptr;
    Json source_read_limits = nullptr;
    bool source_read_limit_failed = false;
    std::string decoder_context_sha256;
    // Decoder IDs to explicit definition identities; provenance, not stored asset members.
    std::map<std::string, std::string> decoder_definitions;
    static Package open(const fs::path &file, const ReadOptions &options = {});
    // Reads from host-supplied bytes; never touches the filesystem for the package or companions.
    static Package open(const PackageSources &sources, const ReadOptions &options = {});
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
    bool source_read_limit_failed = false;
    Json unavailable_scopes = Json::array();
    Json source_contexts = nullptr;
    std::string status() const;
    Json to_json() const;
    // Stream borrowed result arrays; additional object fields cannot override core report fields.
    // The stream may contain a partial document if writing/cancellation fails.
    void write_json(std::ostream &, int indent = 2, const Json &additional = Json::object(),
                    const OperationControl & = {}) const;
};
DiffResult compare(const Package &before, const Package &after, const OperationControl &control = {});
struct MaterialComparison {
    Json changes = Json::array();
    bool complete = false;
    Json unavailable_scopes = Json::array();
    std::string status() const;
    Json to_json() const;
};
// Compares the serialized graph/parameter projection; no parent or shader evaluation.
MaterialComparison compare_materials(const Package &before, const Package &after);
std::string sha256_file(const fs::path &path, const OperationControl &control = {});
std::string sha256_bytes(const std::vector<std::uint8_t> &bytes, const OperationControl &control = {});
} // namespace cyuediff
