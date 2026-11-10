#pragma once
#include <cyuediff/container.hpp>
#include <cyuediff/schema.hpp>
#include <cyuediff/visual_diff.hpp>
namespace cyuediff {
// Unversioned cooked headers require an explicit layout; no automatic engine execution.
enum class ZenProfile { unspecified, unreal_5_3, unreal_5_5 };
class ScriptObjects;
struct ZenSchemaBinding {
    std::string schema;
    std::uint64_t property_offset = 0;
};
struct ZenSchemaContext {
    ZenProfile profile = ZenProfile::unspecified;
    std::shared_ptr<const UnversionedSchemas> schemas;
    std::map<std::string, ZenSchemaBinding> bindings;
    std::set<std::string> reflected_structs;
    std::map<std::string, NativeStructLayout> native_structs;
    std::shared_ptr<const BinaryLayouts> binary_layouts;
    std::map<std::string, std::string> struct_layouts;
    std::map<std::string, std::string> struct_tails;
    NarrowStringEncoding narrow_string_encoding = NarrowStringEncoding::unspecified;
    std::string fingerprint() const;
};
struct ZenReadOptions {
    ZenProfile profile = ZenProfile::unspecified;
    std::uint64_t max_package_bytes = 256ull * 1024 * 1024, max_header_bytes = 64ull * 1024 * 1024;
    std::uint64_t max_text_bytes = 64ull * 1024 * 1024;
    std::uint32_t max_entries = 1'000'000, max_name_bytes = 65536, max_depth = 128;
    std::shared_ptr<const ScriptObjects> script_objects;
    std::shared_ptr<const ZenSchemaContext> schema_context;
    std::uint32_t max_property_values = 1'000'000;
    OperationControl control;
};
class ScriptObjects {
    std::map<std::uint64_t, std::string> paths_;
    std::string sha256_;

  public:
    static ScriptObjects parse(const std::vector<std::uint8_t> &, const ZenReadOptions & = {});
    std::string resolve(std::uint64_t index) const;
    std::size_t size() const {
        return paths_.size();
    }
    const std::string &sha256() const {
        return sha256_;
    }
};
struct ZenExport {
    std::string name, path, class_name, sha256;
    std::uint64_t offset = 0, size = 0, public_hash = 0;
    std::uint64_t outer_index = ~0ull, class_index = ~0ull, super_index = ~0ull, template_index = ~0ull;
    std::uint32_t flags = 0;
    std::uint8_t filter_flags = 0;
    Json references = Json::object();
    // Null means no explicit binding. Failed reads retain the whole opaque body.
    Json property_read = nullptr;
    Json to_json() const;
};
// Coverage for retained propertyRead JSON (pointers distinguish absent objects from
// existing unbound/opaque objects). Reuse it for flat tables and paged property views.
ValueCoverage cooked_property_coverage(const Json *before_read, const Json *after_read,
                                       const OperationControl & = {});
struct ZenPackage {
    std::string name, profile, sha256, header_sha256, script_objects_sha256, schema_context_sha256;
    std::uint32_t header_size = 0, cooked_header_size = 0, flags = 0;
    bool unversioned_properties = false;
    std::uint32_t property_exports_decoded = 0, property_exports_failed = 0;
    Json versioning = nullptr;
    std::vector<std::string> names, imported_package_names;
    std::vector<ZenExport> exports;
    Json imports = Json::array(), bulk_data = Json::array(), dependencies = Json::array();
    Json other_payload = Json::array();
    std::vector<std::string> diagnostics;
    // Zen export-bundle bytes, not classic loose .uasset bytes. Explicit schema bindings
    // can decode property regions; complete export-body hashes are always retained.
    static ZenPackage parse(const std::vector<std::uint8_t> &, const ZenReadOptions &);
    Json snapshot() const;
    Json to_json() const;
};
ZenPackage read_zen_package(const Container &, std::size_t entry, const ZenReadOptions &,
                            const ContainerReadOptions & = {});
// Matches objects by resolved outer/name path. Opaque body changes remain explicitly opaque.
Json compare_zen_packages(const ZenPackage &, const ZenPackage &, const OperationControl & = {});
struct ZenCatalogOptions {
    bool include_snapshots = false;
    std::uint32_t max_packages = 100'000, max_exports = 1'000'000;
    std::uint64_t max_report_bytes = 128ull * 1024 * 1024;
};
// Enumerates chunk type 1 only. Per-package failures produce complete:false; cancellation propagates.
Json catalog_zen_packages(const Container &, const ZenReadOptions &, const ContainerReadOptions & = {},
                          const ZenCatalogOptions & = {});
} // namespace cyuediff
