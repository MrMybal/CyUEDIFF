#include "animation_internal.hpp"
#include "animation_transform.hpp"
#include <set>
namespace cyuediff {
Json detail::compare_animation_frame_samples(const Json &a, const Json &b,
                                             const AnimationFrameDiffOptions &o) {
    validate_animation_tolerances(o.position_tolerance, o.rotation_tolerance_degrees, o.scale_tolerance);
    const auto &control = o.animation.control;
    control.check();
    auto covered = [](const Json &v) {
        return v.value("completeInput", false) && v.value("tracksCoverage", std::string()) == "stored";
    };
    auto identified = [&](const Json &v) { return covered(v) && v.value("identitiesComplete", false); };
    std::set<std::string> names;
    for (const auto *side : {&a, &b})
        if (side->contains("tracks"))
            for (auto it = side->at("tracks").begin(); it != side->at("tracks").end(); ++it) {
                control.check();
                if (!names.count(it.key()) && names.size() >= o.max_track_pairs)
                    throw Error("limit", "Animation frame track-pair budget exceeded");
                names.insert(it.key());
            }
    Json out = {
        {"schema", "cyuediff.animation-frame-diff/1"},
        {"scope", "selected bone-local source frames; no hierarchy, interpolation, retargeting or skinning"},
        {"beforeObject", a.value("object", std::string())},
        {"afterObject", b.value("object", std::string())},
        {"beforeFrame", a.value("frame", 0u)},
        {"afterFrame", b.value("frame", 0u)},
        {"positionTolerance", o.position_tolerance},
        {"rotationToleranceDegrees", o.rotation_tolerance_degrees},
        {"scaleTolerance", o.scale_tolerance},
        {"rotationMetric", "normalized shortest quaternion orientation; sign invariant"},
        {"complete", covered(a) && covered(b) && identified(a) && identified(b)},
        {"changedTracks", 0},
        {"unavailableTracks", 0},
        {"comparedTracks", 0},
        {"tracks", Json::object()},
        {"diagnostics", Json::array()}};
    if (a.contains("timeSeconds"))
        out["beforeTimeSeconds"] = a.at("timeSeconds");
    if (b.contains("timeSeconds"))
        out["afterTimeSeconds"] = b.at("timeSeconds");
    std::size_t changed = 0, unknown = 0, compared = 0;
    auto find = [](const Json &v, const std::string &key) -> const Json * {
        if (!v.contains("tracks"))
            return nullptr;
        const auto it = v.at("tracks").find(key);
        return it == v.at("tracks").end() ? nullptr : &*it;
    };
    for (const auto &name : names) {
        control.checkpoint("Comparing animation source frames", out["tracks"].size(), names.size());
        const auto *x = find(a, name), *y = find(b, name);
        Json pair = {{"before", x ? *x : Json(nullptr)},
                     {"after", y ? *y : Json(nullptr)},
                     {"status", "unavailable"},
                     {"ready", false},
                     {"changed", nullptr},
                     {"diagnostics", Json::array()}};
        if (x && y && covered(a) && covered(b)) {
            pair["metrics"] = compare_local_animation_values(
                *x, *y, o.position_tolerance, o.rotation_tolerance_degrees, o.scale_tolerance, control);
            if (pair["metrics"]["ready"].get<bool>()) {
                const bool different = pair["metrics"]["changed"].get<bool>();
                pair["ready"] = true;
                pair["changed"] = different;
                pair["status"] = different ? "changed" : "identical";
                changed += different;
                ++compared;
            }
        } else if ((x || y) && covered(a) && covered(b) && identified(x ? b : a) &&
                   name.compare(0, 5, "name:") == 0 && (x ? x : y)->value("ready", false)) {
            pair["status"] = x ? "removed" : "added";
            pair["ready"] = true;
            pair["changed"] = true;
            ++changed;
        }
        if (!pair["ready"].get<bool>()) {
            ++unknown;
            out["complete"] = false;
            pair["diagnostics"].push_back("A source value or confirmed track identity is unavailable");
        }
        out["tracks"][name] = std::move(pair);
    }
    if (!covered(a) || !covered(b))
        out["diagnostics"].push_back("A source frame or input track table is unavailable");
    if (!identified(a) || !identified(b))
        out["diagnostics"].push_back(
            "A track identity table is incomplete; missing names are not confirmed absence");
    out["changedTracks"] = changed;
    out["unavailableTracks"] = unknown;
    out["comparedTracks"] = compared;
    out["status"] = !out["complete"].get<bool>() ? "incomplete" : changed ? "changed" : "identical";
    return out;
}
Json compare_animation_frames(const Package &before, const Package &after, std::uint32_t af, std::uint32_t bf,
                              const std::string &ao, const std::string &bo,
                              const AnimationFrameDiffOptions &o) {
    detail::validate_animation_tolerances(o.position_tolerance, o.rotation_tolerance_degrees,
                                          o.scale_tolerance);
    auto options = o.animation;
    options.source_indices = false;
    const auto a = animation_frame(before, af, ao, options), b = animation_frame(after, bf, bo, options);
    return detail::compare_animation_frame_samples(a, b, o);
}
} // namespace cyuediff
