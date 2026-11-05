#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
struct AnimationOptions {
    std::size_t max_models = 1024, max_tracks = 100000, max_keys = 1000000, max_nodes = 16000000,
                max_text_bytes = 16 * 1024 * 1024;
    unsigned max_depth = 48;
    bool source_indices = true;
    OperationControl control;
};
// Exact Engine.AnimDataModel source streams. No external loading, interpolation or defaults.
Json animation_snapshot(const Package &, const AnimationOptions & = {});
struct AnimationComparison {
    Json changes = Json::array();
    std::string before_object, after_object;
    bool complete = false;
    std::string status() const;
    Json to_json() const;
};
// Empty selectors require exactly one model on the respective side.
AnimationComparison compare_animation_models(const Package &, const Package &,
                                             const std::string &before_object = "",
                                             const std::string &after_object = "",
                                             const AnimationOptions & = {});
} // namespace cyuediff
