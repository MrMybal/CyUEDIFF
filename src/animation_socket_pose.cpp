#include "animation_socket_pose_internal.hpp"
#include <cmath>
#include <set>
namespace cyuediff {
AnimationPose detail::animation_socket_points(const AnimationSocketPose &socket) {
    AnimationPose out;
    out.animation_object = socket.animation_object;
    out.skeleton_object = socket.skeleton_object;
    out.frame = socket.frame;
    out.time_seconds = socket.time_seconds;
    out.display_pose = socket.display_pose;
    out.hierarchy_complete = socket.membership_complete;
    out.bindings = socket.bindings;
    return out;
}
Json detail::animation_socket_comparison(Json out) {
    out["schema"] = "cyuediff.animation-socket-pose-diff/1";
    out["scope"] = "derived socket origins and stored parent names in explicit source frames; no socket "
                   "orientation or skinning";
    out["sockets"] = std::move(out["joints"]);
    out.erase("joints");
    out["changedSockets"] = out["changedJoints"];
    out.erase("changedJoints");
    out["unavailableSockets"] = out["unavailableJoints"];
    out.erase("unavailableJoints");
    return out;
}
Json AnimationSocketPose::to_json() const {
    auto out = detail::animation_socket_points(*this).to_json();
    out["schema"] = "cyuediff.animation-socket-pose/1";
    out["scope"] = "explicit stored socket locations transformed by prepared source affines; no rest "
                   "fallback or socket orientation";
    out["membershipComplete"] = membership_complete;
    out.erase("hierarchyComplete");
    out.erase("affines");
    out.erase("unboundTracks");
    out["sockets"] = std::move(out["joints"]);
    out.erase("joints");
    return out;
}
AnimationSocketPose animation_socket_pose(const AnimationPose &source, const Package &package,
                                          const AnimationSocketOptions &options) {
    const auto &control = options.control;
    control.check();
    if (!options.max_sockets || !options.max_parent_joints)
        throw Error("animation-socket-options", "Socket and parent budgets must be nonzero");
    if (source.display_pose.joints.size() > options.max_parent_joints ||
        source.source_affines.size() > options.max_parent_joints)
        throw Error("limit", "Source socket parent budget exceeded");
    std::size_t text = 0;
    auto charge = [&](std::size_t size) {
        if (size > options.attachments.max_text_bytes - text)
            throw Error("limit", "Source socket parent text budget exceeded");
        text += size;
    };
    charge(source.animation_object.size());
    charge(source.skeleton_object.size());
    for (const auto &joint : source.display_pose.joints) {
        control.check();
        charge(joint.name.size());
        charge(joint.parent.size());
    }
    for (const auto &affine : source.source_affines) {
        control.check();
        charge(affine.first.size());
    }
    auto projection_options = options.attachments;
    projection_options.control = control;
    const auto projection = attachment_snapshot(package, projection_options);
    const auto owner = projection.at("owners").find(source.skeleton_object);
    if (source.skeleton_object.empty() || owner == projection.at("owners").end())
        throw Error("animation-socket-selection",
                    "Prepared source Skeleton owner is absent from socket definitions");
    AnimationSocketPose out;
    out.animation_object = source.animation_object;
    out.skeleton_object = source.skeleton_object;
    out.frame = source.frame;
    out.time_seconds = source.time_seconds;
    out.display_pose.object_path = source.skeleton_object;
    bool owner_complete = false;
    for (const auto &entry : package.exports)
        if (entry.path == source.skeleton_object)
            owner_complete = entry.coverage == "properties";
    out.membership_complete =
        package.complete_input && owner_complete && owner->at("socketListCoverage") == "stored";
    out.display_pose.complete = out.membership_complete;
    if (owner->at("socketListCoverage") != "stored") {
        out.display_pose.diagnostics.push_back(
            "Stored socket membership is unavailable; contained sockets are not substituted");
        return out;
    }
    std::map<std::string, const ReferenceJoint *> parents;
    for (const auto &joint : source.display_pose.joints) {
        control.check();
        if (joint.name.empty() || !parents.emplace(joint.name, &joint).second)
            throw Error("animation-socket-identity", "Empty or duplicate prepared parent name");
    }
    std::set<std::string> seen;
    for (const auto &reference : owner->at("socketReferences")) {
        control.checkpoint("Composing source animation sockets", out.display_pose.joints.size());
        if (!reference.at("path").is_string()) {
            out.membership_complete = out.display_pose.complete = false;
            out.display_pose.diagnostics.push_back("A stored socket reference has no resolved identity");
            continue;
        }
        const auto name = reference.at("path").get<std::string>();
        if (name.empty()) {
            out.membership_complete = out.display_pose.complete = false;
            out.display_pose.diagnostics.push_back("A stored socket reference has an empty identity");
            continue;
        }
        if (name == "None" || !seen.insert(name).second)
            continue;
        if (out.display_pose.joints.size() >= options.max_sockets)
            throw Error("limit", "Source socket origin budget exceeded");
        ReferenceJoint point;
        point.name = name;
        Json binding = {{"bone", nullptr}, {"location", nullptr}, {"affineReady", false}};
        const auto definition = projection.at("sockets").find(name);
        if (definition == projection.at("sockets").end())
            point.diagnostics.push_back("Referenced socket is external, missing or unresolved");
        else {
            if (definition->at("bone").is_string())
                point.parent = definition->at("bone").get<std::string>();
            binding["bone"] = point.parent.empty() ? Json(nullptr) : Json(point.parent);
            const auto parent = parents.find(point.parent);
            const auto affine = source.source_affines.find(point.parent);
            bool ready =
                parent != parents.end() && parent->second->ready && affine != source.source_affines.end();
            if (ready) {
                for (double v : affine->second)
                    ready = ready && std::isfinite(v);
                for (unsigned r = 0; r < 3; ++r)
                    ready = ready && std::isfinite(parent->second->position[r]) &&
                            parent->second->position[r] == affine->second[r * 4 + 3];
            }
            binding["affineReady"] = ready;
            if (!ready)
                point.diagnostics.push_back("Ready source parent affine is unavailable or inconsistent; no "
                                            "origin-only or reference fallback");
            if (!definition->contains("location"))
                point.diagnostics.push_back("Local socket location is unavailable; no implicit zero");
            else
                binding["location"] = definition->at("location");
            if (point.diagnostics.empty()) {
                const auto &m = affine->second;
                for (unsigned r = 0; r < 3; ++r) {
                    double value = m[r * 4 + 3];
                    for (unsigned k = 0; k < 3; ++k)
                        value += m[r * 4 + k] * definition->at("location").at(k).get<double>();
                    if (!std::isfinite(value))
                        throw Error("animation-socket-value", "Source socket affine origin overflow");
                    point.position[r] = value;
                }
                point.ready = true;
            }
        }
        if (!point.ready)
            out.display_pose.complete = false;
        out.bindings[name] = std::move(binding);
        out.display_pose.joints.push_back(std::move(point));
    }
    return out;
}
AnimationSocketPose animation_socket_pose(const Package &animation, const Package &skeleton,
                                          const AnimationPoseSelection &selection,
                                          const AnimationPoseOptions &pose_options,
                                          const AnimationSocketOptions &options) {
    OperationControl shared;
    shared.cancelled = [&] {
        return (pose_options.control.cancelled && pose_options.control.cancelled()) ||
               (options.control.cancelled && options.control.cancelled());
    };
    shared.progress = [&](const OperationProgress &p) {
        if (pose_options.control.progress)
            pose_options.control.progress(p);
        if (options.control.progress)
            options.control.progress(p);
    };
    auto selected_pose = pose_options;
    selected_pose.control = shared;
    auto selected_sockets = options;
    selected_sockets.control = shared;
    return animation_socket_pose(animation_pose(animation, skeleton, selection.frame,
                                                selection.animation_object, selection.skeleton_object,
                                                selected_pose),
                                 skeleton, selected_sockets);
}
Json compare_animation_socket_poses(const AnimationSocketPose &a, const AnimationSocketPose &b,
                                    const AnimationPoseDiffOptions &options) {
    return detail::animation_socket_comparison(compare_animation_poses(
        detail::animation_socket_points(a), detail::animation_socket_points(b), options));
}
} // namespace cyuediff
