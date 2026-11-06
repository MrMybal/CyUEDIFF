#pragma once
#include "browser.hpp"

namespace cyuediff::desktop {
struct Settings {
    std::string before, after, project, engine, codec, repository, lfs_objects, after_project, after_engine;
    std::string layouts, after_layouts;
    bool inherit_after_layouts = true;
    std::string payload_root, after_payload_root;
    bool inherit_after_payload_root = true;
    std::string pose_before_skeleton, pose_after_skeleton, pose_before_object, pose_after_object,
        pose_before_skeleton_object, pose_after_skeleton_object;
    int pose_before_frame = 0, pose_after_frame = 0;
    bool pose_enabled = false, pose_independent_frames = false;
    std::string before_revision = "HEAD~1", after_revision = "HEAD";
    int source = 0, cooked_profile = 0, after_cooked_profile = 0;
    std::string container_path, container_entry, script_objects, after_container_path, after_script_objects;
    std::string container_schema, after_container_schema;
    bool compare_container_payloads = false;
    bool previews = true, discover_engine = true, include_engine = false;
    bool ready() const {
        if (source == 2)
            return !container_path.empty();
        return !before.empty() &&
               (source == 0 || (!repository.empty() && !before_revision.empty() && !after_revision.empty()));
    }
    bool browse_ready() const {
        if (source == 2)
            return false;
        return source == 0 ? !project.empty()
                           : (!repository.empty() && !before_revision.empty() && !after_revision.empty());
    }
    BrowseRequest browse_request() const {
        return {fs::u8path(project), fs::u8path(engine), include_engine, request().git};
    }
    LoadRequest request() const {
        if (pose_enabled && source == 2)
            throw Error("desktop-animation-pose",
                        "External Skeleton bindings require loose or Git packages");
        if (source == 2) {
            if (!layouts.empty() || !after_layouts.empty() || !inherit_after_layouts)
                throw Error("desktop-layouts", "Cooked structures use inline schema context layouts");
            LoadRequest result;
            ContainerSource input;
            input.path = fs::u8path(container_path);
            input.script_objects = fs::u8path(script_objects);
            input.schema = fs::u8path(container_schema);
            input.entry = container_entry;
            input.profile = cooked_profile == 1
                                ? ZenProfile::unreal_5_3
                                : (cooked_profile == 2 ? ZenProfile::unreal_5_5 : ZenProfile::unspecified);
            if (!after_container_path.empty()) {
                input.after_path = fs::u8path(after_container_path);
                input.after_script_objects = fs::u8path(after_script_objects);
                input.after_schema = fs::u8path(after_container_schema);
                input.after_profile =
                    after_cooked_profile == 1
                        ? ZenProfile::unreal_5_3
                        : (after_cooked_profile == 2 ? ZenProfile::unreal_5_5 : ZenProfile::unspecified);
                input.compare_payloads = compare_container_payloads;
            }
            result.container = std::move(input);
            result.codec = fs::u8path(codec);
            result.previews = false;
            return result;
        }
        LoadRequest result{before,  after, fs::u8path(project), fs::u8path(engine), fs::u8path(codec),
                           previews};
        result.payload_root = fs::u8path(payload_root);
        result.after_payload_root = inherit_after_payload_root ? fs::path{} : fs::u8path(after_payload_root);
        result.inherit_after_payload_root = inherit_after_payload_root;
        result.layouts = fs::u8path(layouts);
        result.after_layouts = inherit_after_layouts ? fs::path{} : fs::u8path(after_layouts);
        result.inherit_after_layouts = inherit_after_layouts;
        result.after_project = fs::u8path(after_project);
        result.after_engine_root = fs::u8path(after_engine);
        if (pose_enabled) {
            if (pose_before_frame < 0 || pose_after_frame < 0)
                throw Error("desktop-animation-pose", "Source frames must be nonnegative integers");
            AnimationPoseRequest pose;
            pose.before_skeleton = pose_before_skeleton;
            pose.after_skeleton = pose_after_skeleton;
            pose.before = {static_cast<std::uint32_t>(pose_before_frame), pose_before_object,
                           pose_before_skeleton_object};
            pose.after = {
                static_cast<std::uint32_t>(pose_independent_frames ? pose_after_frame : pose_before_frame),
                pose_after_object, pose_after_skeleton_object};
            result.animation_pose = std::move(pose);
        }
        if (source == 1)
            result.git = GitSource{fs::u8path(repository), fs::u8path(lfs_objects), before_revision,
                                   after_revision, discover_engine};
        return result;
    }
};
} // namespace cyuediff::desktop
