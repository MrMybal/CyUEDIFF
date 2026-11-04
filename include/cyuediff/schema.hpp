#pragma once
#include <cyuediff/container.hpp>
#include <set>
namespace cyuediff {
struct SchemaReadOptions {
    std::uint64_t max_bytes = 64ull * 1024 * 1024;
    std::uint64_t max_text_bytes = 64ull * 1024 * 1024;
    std::uint32_t max_entries = 1'000'000, max_depth = 48;
    ContainerBlockDecoder decoder;
    OperationControl control;
};
struct SchemaType {
    std::uint8_t kind = 255;
    std::string name;
    std::vector<SchemaType> arguments;
};
struct SchemaProperty {
    std::string name;
    std::uint16_t index = 0;
    std::uint8_t array_size = 1;
    SchemaType type;
};
struct SchemaDefinition {
    std::string name, super, module, path;
    bool attributes_present = false;
    std::uint8_t struct_kind = 0;
    std::uint32_t flags = 0;
    // Raw EATR sequence; no inferred association to sparse descriptor indices.
    std::vector<std::uint64_t> serialized_property_flags;
    std::uint16_t slots = 0;
    std::map<std::uint16_t, SchemaProperty> properties;
};
struct SchemaEnum {
    std::string name, module, path;
    bool attributes_present = false;
    std::uint32_t flags = 0;
    Json values = Json::array();
};
// Immutable, explicit host-supplied USMAP mappings. No asset or engine discovery.
class UnversionedSchemas {
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit UnversionedSchemas(std::shared_ptr<const Impl>);

  public:
    static UnversionedSchemas parse(const std::vector<std::uint8_t> &, const SchemaReadOptions & = {});
    static UnversionedSchemas read(const fs::path &, const SchemaReadOptions & = {});
    // Exact qualified identity, or a unique short name. Ambiguous names throw.
    const SchemaDefinition &schema(const std::string &) const;
    const SchemaEnum &enumeration(const std::string &) const;
    Json to_json() const;
    const Json &versioning() const;
    const std::string &sha256() const;
};
// Explicit serialized layouts, independent of in-memory type size or engine aliases.
enum class NativeStructLayout {
    vector2_f32,
    vector2_f64,
    vector3_f32,
    vector3_f64,
    vector4_f32,
    vector4_f64,
    quat_f32,
    quat_f64,
    rotator_f32,
    rotator_f64,
    transform_f32,
    transform_f64,
    guid,
    linear_color_f32,
    color_bgra8,
    int_point,
    int_vector
};
const char *native_struct_layout_name(NativeStructLayout);
NativeStructLayout parse_native_struct_layout(const std::string &);
enum class SoftPathLayout { unspecified, ue5_top_level_names };
// Flags + history tag; None includes the modern uint32 invariant-source marker.
enum class TextLayout { unspecified, ue5_history };
struct UnversionedReadOptions {
    const std::vector<std::string> *names = nullptr;
    std::function<Json(std::int32_t)> reference;
    // Only explicitly certified reflected structs use a nested unversioned header.
    // Native struct serializers are never guessed from a matching USMAP name.
    std::set<std::string> reflected_structs;
    // Enables the -1 removed-key marker checked against the UE 5.5 map layout.
    // Zero-masked empty containers do not require this flag.
    bool allow_map_replacement = false;
    // Schema identity -> wire layout. No basename inference; cannot also be reflected.
    std::map<std::string, NativeStructLayout> native_structs;
    SoftPathLayout soft_path_layout = SoftPathLayout::unspecified;
    TextLayout text_layout = TextLayout::unspecified;
    std::uint32_t max_values = 100'000, max_depth = 48;
    std::uint64_t max_text_bytes = 4ull * 1024 * 1024;
    OperationControl control;
    // Optional enclosing-operation budget, charged even if this read later fails.
    std::function<void(std::uint32_t, std::uint64_t)> charge;
};
struct UnversionedProperties {
    Json properties = Json::object();
    std::uint64_t consumed_bytes = 0, text_bytes = 0;
    std::uint32_t values_read = 0;
};
// Decodes only properties present in the header. Skipped slots require archetype
// defaults and are not synthesized. Throws on unsupported types or invalid data;
// callers must discard the whole result and retain the original opaque range.
UnversionedProperties decode_unversioned(const std::vector<std::uint8_t> &, std::uint64_t offset,
                                         std::uint64_t size, const UnversionedSchemas &,
                                         const std::string &schema, const UnversionedReadOptions & = {});
} // namespace cyuediff
