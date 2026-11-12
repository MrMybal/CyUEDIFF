#pragma once
#include <cyuediff/core.hpp>
#include <set>
namespace cyuediff::detail {
struct SourceCoverage {
    Json unavailable = Json::array(), name_changes = Json::array();
    std::set<std::string> suppressed;
    std::set<std::string> affected_objects;
    // Stored typed-name indexes of both packages (borrowed; valid while the packages live).
    // Name correspondence is by identical path, so arrays containing names must keep index
    // correspondence when their lengths differ.
    const Json *before_names = nullptr, *after_names = nullptr;
    bool excludes(const std::string &path) const;
    bool intersects(const std::string &path) const;
    // True when `path` or a descendant holds a stored typed name.
    bool names_within(const std::string &path) const;
};
struct SourceCoverageOptions {
    std::size_t max_nodes = 4'000'000, max_scopes = 100'000, max_path_bytes = 16 * 1024 * 1024;
    OperationControl control;
};
bool source_property_tag(const Json &);
SourceCoverage projection_coverage(const SourceCoverage &, const Package &, const Package &, const Json &,
                                   const Json &, const std::string &domain, const OperationControl & = {});
SourceCoverage source_coverage(const Package &, const Package &, const SourceCoverageOptions & = {});
} // namespace cyuediff::detail
