#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
struct SkeletonOptions {
    std::size_t max_skeletons = 1024, max_bones = 100000, max_nodes = 1000000,
                max_text_bytes = 16 * 1024 * 1024;
    std::size_t max_materials = 100000;
    unsigned max_depth = 48;
    bool source_indices = true;
    OperationControl control;
};
// Skeleton and SkeletalMesh objects. Exact stored bone names and named parents; no virtual-bone rebuilding or
// default values.
Json skeleton_snapshot(const Package &, const SkeletonOptions & = {});
struct SkeletonComparison {
    Json changes = Json::array();
    bool complete = false;
    std::string status() const;
    Json to_json() const;
};
SkeletonComparison compare_skeletons(const Package &, const Package &, const SkeletonOptions & = {});
} // namespace cyuediff
