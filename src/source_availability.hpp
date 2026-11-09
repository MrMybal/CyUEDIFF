#pragma once
#include <cyuediff/core.hpp>
#include <set>
namespace cyuediff::detail {
struct SourceCoverage {
    Json unavailable = Json::array();
    std::set<std::string> suppressed;
    std::set<std::string> affected_objects;
    bool excludes(const std::string &path) const;
    bool intersects(const std::string &path) const;
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
