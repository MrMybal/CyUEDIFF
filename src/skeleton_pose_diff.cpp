#include <cyuediff/skeleton_pose.hpp>
#include <cmath>
#include <set>
namespace cyuediff {
std::string ReferencePoseComparison::status() const {
    return !complete ? "incomplete" : changed_joints ? "changed" : "identical";
}
Json ReferencePoseComparison::to_json() const {
    return {
        {"schema", "cyuediff.reference-pose-diff/1"},
        {"space", "raw-reference-affine"},
        {"scope", "derived joint positions and named parents; no rotation, animation or skin comparison"},
        {"beforeObject", before_object},
        {"beforeRetargetSource",
         before_retarget_source.empty() ? Json(nullptr) : Json(before_retarget_source)},
        {"afterRetargetSource", after_retarget_source.empty() ? Json(nullptr) : Json(after_retarget_source)},
        {"afterObject", after_object},
        {"complete", complete},
        {"status", status()},
        {"positionTolerance", position_tolerance},
        {"changedJoints", changed_joints},
        {"unavailableJoints", unavailable_joints},
        {"joints", joints}};
}
ReferencePoseComparison compare_reference_poses(const ReferencePose &before, const ReferencePose &after,
                                                const ReferencePoseDiffOptions &options) {
    const auto &control = options.pose.skeleton.control;
    control.check();
    if (!std::isfinite(options.position_tolerance) || options.position_tolerance < 0)
        throw Error("skeleton-pose-tolerance", "Position tolerance must be finite and nonnegative");
    std::map<std::string, const ReferenceJoint *> a, b;
    std::set<std::string> names;
    std::size_t text = 0;
    auto charge = [&](std::size_t size) {
        if (size > options.pose.skeleton.max_text_bytes - text)
            throw Error("limit", "Reference pose comparison text budget exceeded");
        text += size;
    };
    charge(before.retarget_source.size());
    charge(after.retarget_source.size());
    charge(before.object_path.size());
    charge(after.object_path.size());
    auto index = [&](const ReferencePose &pose, auto &map) {
        if (pose.joints.size() > options.pose.max_joints)
            throw Error("limit", "Reference pose comparison joint budget exceeded");
        for (const auto &joint : pose.joints) {
            control.check();
            if (joint.name.empty() || !map.emplace(joint.name, &joint).second)
                throw Error("skeleton-pose-identity", "Empty or duplicate reference joint name");
            charge(joint.name.size());
            charge(joint.parent.size());
            if (joint.ready)
                for (double x : joint.position)
                    if (!std::isfinite(x))
                        throw Error("skeleton-pose-value", "Non-finite prepared reference position");
            if (names.insert(joint.name).second && names.size() > options.max_joint_pairs)
                throw Error("limit", "Reference pose union budget exceeded");
        }
    };
    index(before, a);
    index(after, b);
    ReferencePoseComparison result;
    result.before_retarget_source = before.retarget_source;
    result.after_retarget_source = after.retarget_source;
    result.before_object = before.object_path;
    result.after_object = after.object_path;
    result.complete = before.complete && after.complete;
    result.position_tolerance = options.position_tolerance;
    auto value = [](const ReferenceJoint *joint) -> Json {
        return joint ? Json{{"parent", joint->parent.empty() ? Json(nullptr) : Json(joint->parent)},
                            {"position", joint->ready ? Json(joint->position) : Json(nullptr)},
                            {"ready", joint->ready}}
                     : Json(nullptr);
    };
    std::size_t processed = 0;
    for (const auto &name : names) {
        control.checkpoint("Comparing reference poses", processed++, names.size());
        const auto x = a.find(name), y = b.find(name);
        const auto *left = x == a.end() ? nullptr : x->second, *right = y == b.end() ? nullptr : y->second;
        const bool unknown_membership = (!left && !before.complete) || (!right && !after.complete);
        const bool unavailable = unknown_membership || (left && !left->ready) || (right && !right->ready);
        Json entry = {{"before", value(left)},
                      {"after", value(right)},
                      {"delta", nullptr},
                      {"distance", nullptr},
                      {"parentChanged", left && right ? Json(left->parent != right->parent) : Json(nullptr)}};
        std::string kind;
        if (unknown_membership)
            kind = "unavailable";
        else if (!left)
            kind = "added";
        else if (!right)
            kind = "removed";
        else if (unavailable)
            kind = "unavailable";
        else {
            std::array<double, 3> delta{};
            for (unsigned i = 0; i < 3; ++i) {
                delta[i] = right->position[i] - left->position[i];
                if (!std::isfinite(delta[i]))
                    throw Error("skeleton-pose-value", "Reference displacement overflow");
            }
            const double distance = std::hypot(std::hypot(delta[0], delta[1]), delta[2]);
            if (!std::isfinite(distance))
                throw Error("skeleton-pose-value", "Reference displacement magnitude overflow");
            entry["delta"] = delta;
            entry["distance"] = distance;
            kind = distance > options.position_tolerance || left->parent != right->parent ? "modified"
                                                                                          : "unchanged";
        }
        entry["kind"] = kind;
        if (kind != "unchanged" && kind != "unavailable")
            ++result.changed_joints;
        if (unavailable) {
            ++result.unavailable_joints;
            result.complete = false;
        }
        result.joints[name] = std::move(entry);
    }
    return result;
}
ReferencePoseComparison compare_reference_poses(const Package &before, const Package &after,
                                                const std::string &before_object,
                                                const std::string &after_object,
                                                const ReferencePoseDiffOptions &options) {
    const auto a = reference_pose(before, before_object, options.pose);
    auto after_options = options.pose;
    if (options.after_retarget_source)
        after_options.retarget_source = *options.after_retarget_source;
    const auto b = reference_pose(after, after_object, after_options);
    return compare_reference_poses(a, b, options);
}
} // namespace cyuediff
