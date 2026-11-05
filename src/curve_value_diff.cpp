#include "curves_internal.hpp"
#include <cmath>
namespace cyuediff {
namespace {
void validate(const CurveValueDiffOptions &o) {
    if (!std::isfinite(o.time_begin) || !std::isfinite(o.time_end) || o.time_begin > o.time_end ||
        !std::isfinite(o.value_tolerance) || o.value_tolerance < 0 || !o.samples ||
        (o.samples == 1 && o.time_begin != o.time_end))
        throw Error("curve-grid", "Invalid finite sample range, count or tolerance");
    if (o.samples > o.max_samples)
        throw Error("limit", "Curve sample grid exceeds configured limit");
}
} // namespace
Json detail::compare_curve_lanes(const Json *a, const Json *b, bool before_complete, bool after_complete,
                                 const CurveValueDiffOptions &o) {
    validate(o);
    const auto &control = o.curves.control;
    control.check();
    Json out = {{"schema", "cyuediff.curve-values-diff/1"},
                {"scope", "explicit discrete evaluation grid; no continuous-equality guarantee"},
                {"timeBegin", o.time_begin},
                {"timeEnd", o.time_end},
                {"sampleCount", o.samples},
                {"valueTolerance", o.value_tolerance},
                {"complete", true},
                {"changedSamples", 0},
                {"unavailableSamples", 0},
                {"comparedSamples", 0},
                {"maximumAbsoluteDelta", nullptr},
                {"samples", Json::array()}};
    std::size_t changed = 0, unavailable = 0, compared = 0;
    double maximum = 0;
    for (std::size_t i = 0; i < o.samples; ++i) {
        control.checkpoint("Comparing sampled curve values", i, o.samples);
        const auto ratio = o.samples == 1 ? 0. : double(i) / double(o.samples - 1);
        const double time = i == 0               ? o.time_begin
                            : i + 1 == o.samples ? o.time_end
                                                 : (1 - ratio) * o.time_begin + ratio * o.time_end;
        auto unknown = [&] {
            return Json{{"time", time},
                        {"ready", false},
                        {"value", nullptr},
                        {"diagnostics", Json::array({"Selected curve lane is unavailable"})}};
        };
        auto x = a ? detail::evaluate_curve_lane(*a, time, before_complete, control) : unknown();
        auto y = b ? detail::evaluate_curve_lane(*b, time, after_complete, control) : unknown();
        Json sample = {{"time", time},   {"before", std::move(x)}, {"after", std::move(y)},
                       {"ready", false}, {"delta", nullptr},       {"changed", nullptr}};
        if (sample["before"]["ready"].get<bool>() && sample["after"]["ready"].get<bool>()) {
            const double delta =
                sample["after"]["value"].get<double>() - sample["before"]["value"].get<double>();
            if (std::isfinite(delta)) {
                const auto distance = std::abs(delta);
                const bool different = distance > o.value_tolerance;
                sample["ready"] = true;
                sample["delta"] = delta;
                sample["changed"] = different;
                maximum = std::max(maximum, distance);
                changed += different;
                ++compared;
            } else
                sample["diagnostics"] = Json::array({"Value delta arithmetic overflow"});
        }
        if (!sample["ready"].get<bool>())
            ++unavailable;
        out["samples"].push_back(std::move(sample));
    }
    out["complete"] = unavailable == 0;
    out["changedSamples"] = changed;
    out["unavailableSamples"] = unavailable;
    out["comparedSamples"] = compared;
    if (compared)
        out["maximumAbsoluteDelta"] = maximum;
    out["status"] = unavailable ? "incomplete" : changed ? "changed" : "identical";
    return out;
}
Json compare_curve_values(const Package &before, const Package &after, const CurveValueDiffOptions &o) {
    validate(o);
    auto options = o.curves;
    options.source_indices = false;
    const auto a = curve_snapshot(before, options), b = curve_snapshot(after, options);
    auto select = [](const Json &root, const std::string &object, const std::string &lane) {
        const auto &owners = root.at("owners");
        const auto owner = object.empty() && owners.size() == 1 ? owners.begin().key() : object;
        if (owner.empty() || !owners.contains(owner))
            throw Error("curve-object", "Select exactly one curve owner");
        const auto &lanes = owners.at(owner).at("lanes");
        const auto id = lane.empty() && lanes.size() == 1 ? lanes.begin().key() : lane;
        if (id.empty() || !lanes.contains(id))
            throw Error("curve-lane", "Select exactly one curve lane");
        return std::make_pair(owner, id);
    };
    const auto x = select(a, o.before_object, o.before_lane), y = select(b, o.after_object, o.after_lane);
    auto out = detail::compare_curve_lanes(&a.at("owners").at(x.first).at("lanes").at(x.second),
                                           &b.at("owners").at(y.first).at("lanes").at(y.second),
                                           before.complete_input, after.complete_input, o);
    out["beforeObject"] = x.first;
    out["afterObject"] = y.first;
    out["beforeLane"] = x.second;
    out["afterLane"] = y.second;
    return out;
}
} // namespace cyuediff
