#pragma once
#include <cyuediff/decoders.hpp>
namespace cyuediff {
class UnversionedSchemas;
struct TaggedType {
    std::string name;
    std::vector<TaggedType> parameters;
    // Exact metadata struct identity. Serialized tag spelling remains separate.
    std::string schema;
};
struct TaggedTypesOptions {
    std::size_t max_nodes = 100'000, max_text_bytes = 2 * 1024 * 1024;
    std::uint32_t max_depth = 48;
    OperationControl control;
};
// Immutable explicit source metadata. Completes omitted type parameters, never defaults or serializers.
class TaggedTypes {
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit TaggedTypes(std::shared_ptr<const Impl>);

  public:
    static TaggedTypes parse(const Json &, const TaggedTypesOptions & = {});
    static TaggedTypes from_schemas(const UnversionedSchemas &, const std::vector<std::string> &roots,
                                    const SerializationProfile &, const TaggedTypesOptions & = {});
    bool matches(const PackageSummary &) const;
    // Unknown owner/field returns nullptr. Ambiguous aliases, profile/index conflicts throw.
    const TaggedType *property(const std::string &owner, const std::string &field, std::uint32_t array_index,
                               const PackageSummary &) const;
    bool native(const std::string &owner) const;
    const std::string &sha256() const;
    Json to_json() const;
    static TaggedType complete(const TaggedType &serialized, const TaggedType &declared);
};
Json tagged_type_json(const TaggedType &, bool metadata = true);
} // namespace cyuediff
