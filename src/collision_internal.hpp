#pragma once
#include <cyuediff/collision.hpp>
namespace cyuediff::detail {
struct CollisionInput {
    std::string path, class_name;
    const Json *properties = nullptr;
    Json native = nullptr, outer = nullptr, outer_index = nullptr;
    bool properties_complete = false, outer_resolved = true;
};
Json project_collision_inputs(const std::vector<CollisionInput> &, bool complete, const CollisionOptions &);
} // namespace cyuediff::detail
