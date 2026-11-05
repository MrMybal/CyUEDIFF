#include "skeleton_pose_internal.hpp"
#include "affine_transform.hpp"
#include <algorithm>
#include <cmath>
namespace cyuediff {
using namespace detail::affine;
Json ReferencePose::to_json() const {
    Json values = Json::object();
    for (const auto &joint : joints)
        values[joint.name] = {{"parent", joint.parent.empty() ? Json(nullptr) : Json(joint.parent)},
                              {"position", joint.ready ? Json(joint.position) : Json(nullptr)},
                              {"ready", joint.ready},
                              {"diagnostics", joint.diagnostics}};
    return {{"schema", "cyuediff.reference-pose/1"},
            {"space", "raw-reference-affine"},
            {"object", object_path},
            {"retargetSource", retarget_source.empty() ? Json(nullptr) : Json(retarget_source)},
            {"complete", complete},
            {"joints", std::move(values)},
            {"diagnostics", diagnostics}};
}
ReferencePose detail::compose_reference_pose(const Json &body, const std::string &path, bool complete_input,
                                             const ReferencePoseOptions &options,
                                             std::map<std::string, std::array<double, 12>> *affines) {
    ReferencePose result;
    result.object_path = path;
    result.retarget_source = options.retarget_source;
    result.complete = complete_input && body.at("referenceComplete").get<bool>();
    const auto &bones = body.at("bones");
    const Json *selected = nullptr;
    if (!options.retarget_source.empty()) {
        const auto &retarget = body.at("retargeting");
        if (retarget.at("coverage") != "stored") {
            result.complete = false;
            result.diagnostics.push_back(
                "Stored retarget sources are unavailable; raw reference pose is not substituted");
            return result;
        }
        const auto found = retarget.at("sources").find(options.retarget_source);
        if (found == retarget.at("sources").end())
            throw Error("skeleton-retarget-selection", "Stored retarget source key not found");
        if (!found->at("complete").get<bool>()) {
            result.complete = false;
            result.diagnostics.push_back(
                "Stored retarget transforms have no verified raw-bone correspondence");
            return result;
        }
        selected = &found->at("bones");
    }
    if (bones.size() > options.max_joints)
        throw Error("limit", "Reference pose joint budget exceeded");
    if (!result.complete)
        result.diagnostics.push_back("Stored skeleton projection is incomplete");
    std::vector<std::string> order(bones.size());
    for (auto bone = bones.begin(); bone != bones.end(); ++bone)
        order[bone->at("sourceIndex").get<std::size_t>()] = bone.key();
    std::vector<Matrix> matrices(order.size());
    std::vector<unsigned> depths(order.size());
    std::map<std::string, std::size_t> indices;
    for (std::size_t i = 0; i < order.size(); ++i) {
        options.skeleton.control.checkpoint("Composing reference pose", i, order.size());
        const auto &name = order[i];
        const auto &bone = bones.at(name);
        ReferenceJoint joint;
        joint.name = name;
        if (!bone.at("parent").is_null())
            joint.parent = bone.at("parent").get<std::string>();
        Matrix local{};
        if (!joint.parent.empty()) {
            const auto parent = indices.at(joint.parent);
            if (depths[parent] >= options.max_hierarchy_depth)
                throw Error("limit", "Reference pose hierarchy depth exceeded");
            depths[i] = depths[parent] + 1;
            if (!result.joints[parent].ready)
                joint.diagnostics.push_back("Parent affine transform is unavailable");
        }
        const bool valid =
            local_matrix(selected ? selected->at(name).at("localPose") : bone.at("localPose"), local);
        if (!valid)
            joint.diagnostics.push_back("Stored quaternion is not unit length");
        if (valid && joint.diagnostics.empty()) {
            matrices[i] = joint.parent.empty() ? local : compose(matrices[indices.at(joint.parent)], local);
            for (const auto x : matrices[i])
                if (!std::isfinite(x))
                    throw Error("skeleton-pose-value", "Reference affine transform overflow");
            joint.position = {matrices[i][3], matrices[i][7], matrices[i][11]};
            joint.ready = true;
            if (affines)
                affines->emplace(name, matrices[i]);
        } else
            result.complete = false;
        indices.emplace(name, i);
        result.joints.push_back(std::move(joint));
    }
    return result;
}
ReferencePose reference_pose(const Package &package, const std::string &path,
                             const ReferencePoseOptions &options) {
    auto projection_options = options.skeleton;
    projection_options.source_indices = true;
    const auto model = skeleton_snapshot(package, projection_options);
    const auto &skeletons = model.at("skeletons");
    if (path.empty() && skeletons.size() != 1)
        throw Error("skeleton-selection", "Select exactly one Skeleton or SkeletalMesh object path");
    auto it = path.empty() ? skeletons.begin() : skeletons.find(path);
    if (it == skeletons.end())
        throw Error("skeleton-selection", "Skeleton object path not found");
    return detail::compose_reference_pose(*it, it.key(), package.complete_input, options);
}
} // namespace cyuediff
