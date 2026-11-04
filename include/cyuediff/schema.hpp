#pragma once
#include <cyuediff/container.hpp>
#include <set>
namespace cyuediff {
struct SchemaReadOptions {
    std::uint64_t max_bytes = 64ull * 1024 * 1024;
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
    std::string name, super;
    std::uint16_t slots = 0;
    std::map<std::uint16_t, SchemaProperty> properties;
};
// Immutable, explicit host-supplied USMAP mappings. No asset or engine discovery.
class UnversionedSchemas {
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit UnversionedSchemas(std::shared_ptr<const Impl>);

  public:
    static UnversionedSchemas parse(const std::vector<std::uint8_t> &, const SchemaReadOptions & = {});
    static UnversionedSchemas read(const fs::path &, const SchemaReadOptions & = {});
    const SchemaDefinition &schema(const std::string &) const;
    Json to_json() const;
    const Json &versioning() const;
    const std::string &sha256() const;
};
struct UnversionedReadOptions {
    const std::vector<std::string> *names = nullptr;
    std::function<Json(std::int32_t)> reference;
    // Only explicitly certified reflected structs use a nested unversioned header.
    // Native struct serializers are never guessed from a matching USMAP name.
    std::set<std::string> reflected_structs;
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
