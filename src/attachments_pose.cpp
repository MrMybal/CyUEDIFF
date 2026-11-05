#include "attachments_pose_internal.hpp"
#include "skeleton_pose_internal.hpp"
#include <cmath>
#include <set>
namespace cyuediff {
Json SocketPose::to_json() const {
    auto out = ReferencePose::to_json();
    out["schema"] = "cyuediff.socket-pose/1";
    out["scope"] = "origins from explicitly stored socket locations and raw-bone affine poses";
    out["sockets"] = std::move(out["joints"]);
    out.erase("joints");
    return out;
}
Json SocketPoseComparison::to_json() const {
    auto out = ReferencePoseComparison::to_json();
    out["schema"] = "cyuediff.socket-pose-diff/1";
    out["scope"] =
        "derived socket origin positions and stored bone names; no rotation, animation or skin comparison";
    out["sockets"] = std::move(out["joints"]);
    out.erase("joints");
    out["changedSockets"] = out["changedJoints"];
    out.erase("changedJoints");
    out["unavailableSockets"] = out["unavailableJoints"];
    out.erase("unavailableJoints");
    return out;
}
SocketPose detail::compose_socket_pose(const Json &attachments, const Json &body, const std::string &path,
                                       bool complete_input, const ReferencePoseOptions &options,
                                       ReferencePose *parent_origins) {
    const auto &owner = attachments.at("owners").at(path);
    std::map<std::string, std::array<double, 12>> matrices;
    auto bones = compose_reference_pose(body, path, complete_input, options, &matrices);
    SocketPose out;
    out.object_path = path;
    out.retarget_source = options.retarget_source;
    out.complete = complete_input && owner.at("socketsComplete").get<bool>();
    if (owner.at("socketListCoverage") != "stored") {
        out.complete = false;
        out.diagnostics.push_back("Stored socket membership is unavailable");
        if (parent_origins)
            *parent_origins = std::move(bones);
        return out;
    }
    std::set<std::string> seen;
    for (const auto &ref : owner.at("socketReferences")) {
        options.skeleton.control.checkpoint("Composing socket origins", out.joints.size());
        const auto &value = ref.at("path");
        if (!value.is_string()) {
            out.complete = false;
            out.diagnostics.push_back("A stored socket reference has no resolved identity");
            continue;
        }
        const auto socket = value.get<std::string>();
        if (socket == "None" || !seen.insert(socket).second)
            continue;
        if (out.joints.size() >= options.max_joints)
            throw Error("limit", "Socket origin point budget exceeded");
        ReferenceJoint point;
        point.name = socket;
        const auto it = attachments.at("sockets").find(socket);
        if (it == attachments.at("sockets").end()) {
            point.diagnostics.push_back("Referenced socket is external, missing or unresolved");
        } else {
            if (it->at("bone").is_string())
                point.parent = it->at("bone").get<std::string>();
            if (point.parent.empty() || !matrices.count(point.parent))
                point.diagnostics.push_back("Selected raw-bone affine transform is unavailable");
            if (!it->contains("location"))
                point.diagnostics.push_back(
                    "Local socket location was not stored or decoded; no default is inferred");
            if (point.diagnostics.empty()) {
                const auto &m = matrices.at(point.parent);
                const auto &location = it->at("location");
                for (unsigned row = 0; row < 3; ++row) {
                    double result = m[row * 4 + 3];
                    for (unsigned k = 0; k < 3; ++k)
                        result += m[row * 4 + k] * location.at(k).get<double>();
                    if (!std::isfinite(result))
                        throw Error("socket-pose-value", "Socket affine origin overflow");
                    point.position[row] = result;
                }
                point.ready = true;
            }
        }
        if (!point.ready)
            out.complete = false;
        out.joints.push_back(std::move(point));
    }
    if (!bones.complete) {
        // Ready bones remain usable even when another branch or unrelated property is unavailable.
        for (const auto &diagnostic : bones.diagnostics)
            out.diagnostics.push_back(diagnostic);
    }
    if (parent_origins)
        *parent_origins = std::move(bones);
    return out;
}
SocketPose socket_pose(const Package &package, const std::string &path, const ReferencePoseOptions &options,
                       const AttachmentOptions &attachment_options) {
    auto attachments_options = attachment_options;
    auto reference_options = options;
    OperationControl control;
    control.cancelled = [&] {
        return (options.skeleton.control.cancelled && options.skeleton.control.cancelled()) ||
               (attachment_options.control.cancelled && attachment_options.control.cancelled());
    };
    control.progress = [&](const OperationProgress &progress) {
        if (options.skeleton.control.progress)
            options.skeleton.control.progress(progress);
        if (attachment_options.control.progress)
            attachment_options.control.progress(progress);
    };
    attachments_options.control = control;
    reference_options.skeleton.control = control;
    const auto attachments = attachment_snapshot(package, attachments_options);
    const auto &owners = attachments.at("owners");
    if (path.empty() && owners.size() != 1)
        throw Error("socket-selection", "Select exactly one Skeleton or SkeletalMesh owner");
    const auto owner = path.empty() ? owners.begin() : owners.find(path);
    if (owner == owners.end())
        throw Error("socket-selection", "Socket owner path not found");
    auto skeleton_options = options.skeleton;
    skeleton_options.control = control;
    skeleton_options.source_indices = true;
    const auto model = skeleton_snapshot(package, skeleton_options);
    const auto &body = model.at("skeletons").at(owner.key());
    return detail::compose_socket_pose(attachments, body, owner.key(), package.complete_input,
                                       reference_options);
}
SocketPoseComparison compare_socket_poses(const Package &before, const Package &after,
                                          const std::string &before_object, const std::string &after_object,
                                          const ReferencePoseDiffOptions &options,
                                          const AttachmentOptions &attachments) {
    const auto a = socket_pose(before, before_object, options.pose, attachments);
    auto after_options = options.pose;
    if (options.after_retarget_source)
        after_options.retarget_source = *options.after_retarget_source;
    const auto b = socket_pose(after, after_object, after_options, attachments);
    SocketPoseComparison out;
    auto comparison_options=options;
    comparison_options.pose.skeleton.control.cancelled=[&]{
        return (options.pose.skeleton.control.cancelled && options.pose.skeleton.control.cancelled()) ||
               (attachments.control.cancelled && attachments.control.cancelled());
    };
    comparison_options.pose.skeleton.control.progress=[&](const OperationProgress &progress){
        if(options.pose.skeleton.control.progress)options.pose.skeleton.control.progress(progress);
        if(attachments.control.progress)attachments.control.progress(progress);
    };
    static_cast<ReferencePoseComparison &>(out) = compare_reference_poses(a, b, comparison_options);
    return out;
}
} // namespace cyuediff
