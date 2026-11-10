#pragma once
#include <cyuediff/schema.hpp>
namespace cyuediff {
struct DefaultSnapshotReadOptions {
    // Bounds the complete capture document, including diagnostics and reflection values.
    SchemaReadOptions capture;
    // Wire routes remain explicit. Work/text limits and charges span all captured fields.
    UnversionedReadOptions values;
    std::uint64_t max_wire_bytes = 64ull * 1024 * 1024;
    // Exact matching producer archive is mandatory; no Engine installation is loaded.
    std::string expected_engine_version, expected_sha256;
    std::int32_t expected_ue4 = -1, expected_ue5 = -1;
    bool filter_editor_only = false;
};
// Immutable, unbound class-default capture from the optional matching Engine exporter.
// Reading it decodes actual field wires; it neither applies defaults to an asset nor
// certifies the capture against a native module, Blueprint dependency or archetype.
class DefaultSnapshot {
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit DefaultSnapshot(std::shared_ptr<const Impl>);

  public:
    static DefaultSnapshot parse(const std::vector<std::uint8_t> &, const DefaultSnapshotReadOptions & = {});
    static DefaultSnapshot read(const fs::path &, const DefaultSnapshotReadOptions & = {});
    const UnversionedSchemas &schemas() const;
    const Json &fields() const;
    const Json &structures() const;
    const Json &provenance() const;
    const std::string &sha256() const;
    Json to_json() const;
};
} // namespace cyuediff
