#include <cyuediff/animation_pose.hpp>
#include "affine_transform.hpp"
namespace cyuediff {
Json AnimationPose::to_json() const {
    Json joints = Json::object();
    for (const auto &joint : display_pose.joints)
        joints[joint.name] = {{"parent", joint.parent.empty() ? Json(nullptr) : Json(joint.parent)},
                              {"position", joint.ready ? Json(joint.position) : Json(nullptr)},
                              {"ready", joint.ready},
                              {"diagnostics", joint.diagnostics}};
    Json out = {
        {"schema", "cyuediff.animation-pose/1"},
        {"space", "source-animation-affine"},
        {"scope",
         "explicit named source tracks and raw bone hierarchy; no rest fallback, retargeting or skinning"},
        {"animationObject", animation_object},
        {"skeletonObject", skeleton_object},
        {"frame", frame},
        {"complete", display_pose.complete},
        {"hierarchyComplete", hierarchy_complete},
        {"joints", std::move(joints)},
        {"bindings", bindings},
        {"affines", source_affines},
        {"unboundTracks", unbound_tracks},
        {"diagnostics", display_pose.diagnostics}};
    if (time_seconds)
        out["timeSeconds"] = *time_seconds;
    return out;
}
AnimationPose animation_pose(const Package &animation, const Package &skeleton, std::uint32_t frame,
                             const std::string &animation_object, const std::string &skeleton_object,
                             const AnimationPoseOptions &options) {
    const auto &control = options.control;
    control.check();
    auto ao = options.animation;
    ao.control = control;
    ao.source_indices = false;
    auto so = options.skeleton;
    so.control = control;
    so.source_indices = true;
    const auto sample = animation_frame(animation, frame, animation_object, ao);
    const auto projection = skeleton_snapshot(skeleton, so);
    const auto &skeletons = projection.at("skeletons");
    if (skeleton_object.empty() && skeletons.size() != 1)
        throw Error("skeleton-selection", "Select exactly one Skeleton or SkeletalMesh object path");
    auto selected = skeleton_object.empty() ? skeletons.begin() : skeletons.find(skeleton_object);
    if (selected == skeletons.end())
        throw Error("skeleton-selection", "Skeleton object path not found");
    const auto &bones = selected->at("bones");
    if (bones.size() > options.max_joints)
        throw Error("limit", "Animation pose joint budget exceeded");
    AnimationPose out;
    out.animation_object = sample.at("object").get<std::string>();
    out.skeleton_object = selected.key();
    out.frame = frame;
    if (sample.contains("timeSeconds"))
        out.time_seconds = sample.at("timeSeconds").get<double>();
    out.display_pose.object_path = out.skeleton_object;
    out.hierarchy_complete = skeleton.complete_input && selected->at("referenceComplete").get<bool>();
    const bool covered = sample.value("completeInput", false) && sample.at("tracksCoverage") == "stored";
    out.display_pose.complete =
        out.hierarchy_complete && covered && sample.at("identitiesComplete").get<bool>();
    if (!out.hierarchy_complete)
        out.display_pose.diagnostics.push_back("Stored raw bone hierarchy or Skeleton input is unavailable");
    if (!covered)
        out.display_pose.diagnostics.push_back("Source animation track table or input is unavailable");
    if (!sample.at("identitiesComplete").get<bool>())
        out.display_pose.diagnostics.push_back("Source track names are ambiguous or unavailable");
    const auto &tracks = sample.at("tracks");
    std::vector<std::string> order(bones.size());
    for (auto it = bones.begin(); it != bones.end(); ++it)
        order[it->at("sourceIndex").get<std::size_t>()] = it.key();
    std::vector<detail::affine::Matrix> matrices(order.size());
    std::vector<unsigned> depths(order.size());
    std::map<std::string, std::size_t> indices;
    for (std::size_t i = 0; i < order.size(); ++i) {
        control.checkpoint("Composing source animation hierarchy", i, order.size());
        const auto &name = order[i];
        const auto &bone = bones.at(name);
        ReferenceJoint joint;
        joint.name = name;
        if (!bone.at("parent").is_null())
            joint.parent = bone.at("parent").get<std::string>();
        std::size_t parent = 0;
        if (!joint.parent.empty()) {
            parent = indices.at(joint.parent);
            if (depths[parent] >= options.max_hierarchy_depth)
                throw Error("limit", "Animation hierarchy depth exceeded");
            depths[i] = depths[parent] + 1;
            if (!out.display_pose.joints[parent].ready)
                joint.diagnostics.push_back("Parent affine transform is unavailable");
        }
        const auto track = tracks.find("name:" + name);
        const bool local_ready =
            covered && out.hierarchy_complete && track != tracks.end() && track->value("ready", false);
        Json binding = {{"track", track == tracks.end() ? Json(nullptr) : Json("name:" + name)},
                        {"localReady", local_ready},
                        {"diagnostics", Json::array()}};
        detail::affine::Matrix local{};
        if (!local_ready) {
            joint.diagnostics.push_back(
                "A unique ready source track is unavailable; reference pose is not substituted");
            if (track != tracks.end())
                binding["diagnostics"] = track->at("diagnostics");
        } else {
            Json local_pose = {{"translation", track->at("positions")},
                               {"rotation", track->at("rotations")},
                               {"scale", track->at("scales")}};
            if (!detail::affine::local_matrix(local_pose, local))
                joint.diagnostics.push_back("Source quaternion is not unit length");
        }
        if (joint.diagnostics.empty()) {
            matrices[i] = joint.parent.empty() ? local : detail::affine::compose(matrices[parent], local);
            for (auto x : matrices[i])
                if (!std::isfinite(x))
                    throw Error("animation-pose-value", "Source affine transform overflow");
            joint.position = {matrices[i][3], matrices[i][7], matrices[i][11]};
            joint.ready = true;
            out.source_affines.emplace(name, matrices[i]);
        } else
            out.display_pose.complete = false;
        indices.emplace(name, i);
        out.bindings[name] = std::move(binding);
        out.display_pose.joints.push_back(std::move(joint));
    }
    for (auto track = tracks.begin(); track != tracks.end(); ++track) {
        control.check();
        if (track.key().compare(0, 5, "name:") != 0 || !bones.contains(track.key().substr(5))) {
            out.unbound_tracks.push_back(track.key());
            out.display_pose.complete = false;
        }
    }
    if (!out.unbound_tracks.empty())
        out.display_pose.diagnostics.push_back("Some source tracks have no exact raw-bone binding");
    return out;
}
} // namespace cyuediff
