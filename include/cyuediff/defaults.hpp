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
struct DefaultContextReadOptions {
    SchemaReadOptions document;
    DefaultSnapshotReadOptions snapshot;
    std::string expected_sha256;
    std::uint32_t max_dependencies = 4096;
    std::uint64_t max_dependency_bytes = 8ull * 1024 * 1024 * 1024;
};
// A pinned prepared CDO plus an explicit source archive profile. Recorded input files
// are fingerprinted at read time; no Engine code is loaded and no automatic discovery runs.
class DefaultContext {
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit DefaultContext(std::shared_ptr<const Impl>);

  public:
    static DefaultContext read(const fs::path &, const DefaultContextReadOptions & = {});
    const DefaultSnapshot &snapshot() const;
    const Json &source_profile() const;
    const Json &provenance() const;
    const std::string &sha256() const;
};
struct EffectiveDefaultsOptions {
    std::uint32_t max_nodes = 1'000'000, max_depth = 64;
    std::uint64_t max_text_bytes = 64ull * 1024 * 1024;
    OperationControl control;
    std::function<void(std::uint32_t, std::uint64_t)> charge;
};
struct EffectiveDefaults {
    Json fields = Json::object(), unavailable = Json::array(), provenance = Json::object();
    bool complete = false;
    Json to_json() const;
};
// Resolves selected reflected properties of one root export, retaining the input Package
// unchanged. Unsupported fields stay unavailable. Names require complete actual components.
EffectiveDefaults resolve_source_defaults(const Package &, const std::string &export_path,
                                          const DefaultContext &, const EffectiveDefaultsOptions & = {});
struct EffectiveDefaultsComparison {
    Json changes = Json::array(), unavailable = Json::array(), provenance = Json::object();
    std::string status() const;
    Json to_json() const;
};
// Compares qualified effective fields; origins and capture fingerprints are provenance.
// Unavailable values and incompatible field/key contracts never become fabricated removals.
EffectiveDefaultsComparison compare_effective_defaults(const EffectiveDefaults &, const EffectiveDefaults &,
                                                       const EffectiveDefaultsOptions & = {});
} // namespace cyuediff
