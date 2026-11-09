#pragma once
#include <cyuediff/zen.hpp>
namespace cyuediff {
struct ZenSchemaManifestOptions {
    std::uint64_t max_bytes = 2ull * 1024 * 1024, max_text_bytes = 2ull * 1024 * 1024;
    std::uint32_t max_entries = 100'000, max_depth = 48, max_string_bytes = 65536;
    OperationControl control;
};
// Explicit local JSON context. Reading the manifest never opens its mapping or loads a codec.
// Immutable after publication; paths are absolute and relative USMAP paths use the manifest directory.
class ZenSchemaManifest {
    fs::path source_, mapping_;
    ZenProfile profile_ = ZenProfile::unspecified;
    std::string sha256_, expected_sha256_;
    std::map<std::string, ZenSchemaBinding> bindings_;
    std::vector<std::string> reflected_structs_;
    std::map<std::string, NativeStructLayout> native_structs_;
    std::shared_ptr<const BinaryLayouts> binary_layouts_;
    std::map<std::string, std::string> struct_layouts_;
    std::map<std::string, std::string> struct_tails_;
    NarrowStringEncoding narrow_string_encoding_ = NarrowStringEncoding::unspecified;
    std::string format_ = "cyuediff.zen-schema/1";
    ZenSchemaManifest() = default;

  public:
    static ZenSchemaManifest read(const fs::path &, const ZenSchemaManifestOptions & = {});
    // Verifies the mapping fingerprint, resolves schema identities and validates reflected bindings.
    // A matching fingerprint does not certify that the schema describes a particular cooked build.
    std::shared_ptr<const ZenSchemaContext> load(const SchemaReadOptions & = {}) const;
    const fs::path &source_path() const {
        return source_;
    }
    const fs::path &mapping_path() const {
        return mapping_;
    }
    ZenProfile profile() const {
        return profile_;
    }
    const std::string &sha256() const {
        return sha256_;
    }
    Json to_json() const;
};
} // namespace cyuediff
