#include "curves_internal.hpp"
#include <cmath>
#include <set>
namespace cyuediff {
Json evaluate_curve_components(const Package &p, double time, const std::string &object_path,
                               const std::string &curve_id, const CurveOptions &o) {
    if (!std::isfinite(time))
        throw Error("curve-time", "Curve time must be finite");
    const auto root = curve_snapshot(p, o);
    const auto &owners = root.at("owners");
    const auto object = object_path.empty() && owners.size() == 1 ? owners.begin().key() : object_path;
    if (object.empty() || !owners.contains(object))
        throw Error("curve-object", "Select exactly one stored curve owner");
    const auto &owner = owners.at(object), &lanes = owner.at("lanes");
    const bool animation = owner.at("class") == "/Script/Engine.AnimDataModel";
    const bool transform = animation && o.animation_family == AnimationCurveFamily::Transform;
    std::string group = curve_id;
    if (animation) {
        std::set<std::string> groups;
        if (transform)
            for (auto it = owner.at("curveGroups").begin(); it != owner.at("curveGroups").end(); ++it)
                groups.insert(it.key());
        else
            for (auto it = lanes.begin(); it != lanes.end(); ++it)
                groups.insert(it.key());
        if (group.empty() && groups.size() == 1)
            group = *groups.begin();
        if (group.empty() || !groups.count(group))
            throw Error("curve-group", "Select exactly one stored animation curve group");
    } else {
        if (!group.empty())
            throw Error("curve-group", "Curve group selectors require an animation owner");
    }
    Json out = {
        {"schema", "cyuediff.curve-components/1"},
        {"scope", "independent stored component values; no bone application, blend or runtime defaults"},
        {"object", object},
        {"curveId", group.empty() ? Json(nullptr) : Json(group)},
        {"time", time},
        {"ready", p.complete_input},
        {"complete", p.complete_input},
        {"components", Json::object()},
        {"values", Json::object()}};
    if (transform)
        out["rotationAxes"] = {{"x", "roll-degrees"}, {"y", "pitch-degrees"}, {"z", "yaw-degrees"}};
    for (auto it = lanes.begin(); it != lanes.end(); ++it) {
        if (animation && (transform ? it->at("curveId").get<std::string>() != group : it.key() != group))
            continue;
        o.control.checkpoint("Evaluating curve components", out["components"].size(),
                             transform ? 9 : lanes.size());
        const auto id = transform ? it->at("component").get<std::string>() : animation ? "value" : it.key();
        auto v = detail::evaluate_curve_lane(*it, time, p.complete_input, o.control);
        if (!v.at("ready").get<bool>())
            out["ready"] = out["complete"] = false;
        out["values"][id] = v.at("value");
        out["components"][id] = std::move(v);
    }
    if (out["components"].empty())
        out["ready"] = out["complete"] = false;
    return out;
}
} // namespace cyuediff
