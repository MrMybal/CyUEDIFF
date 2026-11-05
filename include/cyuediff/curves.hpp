#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
struct CurveOptions {
    std::size_t max_owners = 1024, max_lanes = 100000, max_keys = 1000000, max_nodes = 16000000,
                max_text_bytes = 16 * 1024 * 1024;
    unsigned max_depth = 48;
    bool source_indices = true;
    OperationControl control;
};
// Exact CurveFloat/CurveVector/CurveLinearColor and AnimDataModel float source curves.
Json curve_snapshot(const Package &, const CurveOptions & = {});
// Stored points/segments, explicit defaults and extrapolation settings. No inferred class defaults.
Json evaluate_curve(const Package &, double time, const std::string &object_path = "",
                    const std::string &lane = "", const CurveOptions & = {});
struct CurveComparison {
    Json changes = Json::array();
    std::string before_object, after_object;
    bool complete = false;
    std::string status() const;
    Json to_json() const;
};
CurveComparison compare_curves(const Package &, const Package &, const std::string &before_object = "",
                               const std::string &after_object = "", const CurveOptions & = {});
} // namespace cyuediff
