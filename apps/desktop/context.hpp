#pragma once
#include "browser.hpp"

namespace cyuediff::desktop {
struct Settings {
    std::string before, after, project, engine, codec, repository, lfs_objects, after_project, after_engine;
    std::string layouts, after_layouts;
    bool inherit_after_layouts = true;
    int max_properties = 100'000, max_property_bytes = 32 * 1024 * 1024;
    int after_max_properties = 100'000, after_max_property_bytes = 32 * 1024 * 1024;
    bool inherit_after_source_limits = true;
    std::string payload_root, after_payload_root;
    bool inherit_after_payload_root = true;
    std::string pose_before_skeleton, pose_after_skeleton, pose_before_object, pose_after_object,
        pose_before_skeleton_object, pose_after_skeleton_object;
    int pose_before_frame = 0, pose_after_frame = 0;
    bool pose_enabled = false, pose_independent_frames = false;
    std::string before_revision = "HEAD~1", after_revision = "HEAD";
    int source = 0, cooked_profile = 0, after_cooked_profile = 0;
    int skeletal_profile = 0, after_skeletal_profile = 0;
    int skeletal_mobile_lod = 0, after_skeletal_mobile_lod = 0, skeletal_colors = 0,
        after_skeletal_colors = 0;
    int physical_cloth_profile = 0, after_physical_cloth_profile = 0;
    int physics_profile = 0, after_physics_profile = 0;
    int mesh_profile = 0, after_mesh_profile = 0, mesh_mobile_lod = 0, after_mesh_mobile_lod = 0;
    int nanite_profile = 0, after_nanite_profile = 0;
    bool nanite_attributes = true, after_nanite_attributes = true;
    std::string container_path, container_entry, script_objects, after_container_path, after_script_objects;
    std::string container_schema, after_container_schema;
    int animation_profile = 0, after_animation_profile = 0, animation_interpolation = 0,
        after_animation_interpolation = 0;
    std::string curve_settings_container, after_curve_settings_container, curve_settings_entry,
        after_curve_settings_entry;
    std::string after_container_entry, bone_settings_container, skeleton_container,
        after_bone_settings_container, after_skeleton_container, bone_settings_entry, skeleton_entry,
        after_bone_settings_entry, after_skeleton_entry;
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
            throw Error("desktop-animation-pose", "External Skeleton bindings require loose or Git packages");
        if (source == 2) {
            if (!layouts.empty() || !after_layouts.empty() || !inherit_after_layouts)
                throw Error("desktop-layouts", "Cooked structures use inline schema context layouts");
            LoadRequest result;
            ContainerSource input;
            input.path = fs::u8path(container_path);
            input.script_objects = fs::u8path(script_objects);
            input.schema = fs::u8path(container_schema);
            input.entry = container_entry;
            input.physical_cloth_profile = physical_cloth_profile == 1
                                               ? PhysicalClothProfile::unreal_5_3_windows
                                               : PhysicalClothProfile::unspecified;
            input.animation_profile = animation_profile == 1 ? CookedAnimationProfile::unreal_5_3_windows
                                                             : CookedAnimationProfile::unspecified;
            if (animation_interpolation)
                input.animation_interpolation = animation_interpolation == 1
                                                    ? CompressedAnimationInterpolation::linear
                                                    : CompressedAnimationInterpolation::step;
            input.bone_settings_container = fs::u8path(bone_settings_container);
            input.skeleton_container = fs::u8path(skeleton_container);
            input.bone_settings_entry = bone_settings_entry;
            input.skeleton_entry = skeleton_entry;
            input.curve_settings_container = fs::u8path(curve_settings_container);
            input.curve_settings_entry = curve_settings_entry;
            input.physics_profile = physics_profile == 1 ? CookedPhysicsProfile::unreal_5_3_windows
                                                         : CookedPhysicsProfile::unspecified;
            input.mesh_profile =
                mesh_profile == 1 ? CookedMeshProfile::unreal_5_3_windows : CookedMeshProfile::unspecified;
            if (mesh_mobile_lod)
                input.keep_mobile_min_lod_setting = mesh_mobile_lod == 2;
            input.nanite_profile =
                nanite_profile == 1 ? NaniteProfile::unreal_5_3_windows : NaniteProfile::unspecified;
            input.nanite_attributes = nanite_attributes;
            input.skeletal_profile = skeletal_profile == 1 ? CookedSkeletalProfile::unreal_5_3_windows
                                                           : CookedSkeletalProfile::unspecified;
            if (skeletal_mobile_lod)
                input.skeletal_mobile_lod = skeletal_mobile_lod == 2;
            if (skeletal_colors)
                input.skeletal_colors = skeletal_colors == 2;
            input.profile = cooked_profile == 1
                                ? ZenProfile::unreal_5_3
                                : (cooked_profile == 2 ? ZenProfile::unreal_5_5 : ZenProfile::unspecified);
            if (!after_container_path.empty()) {
                input.after_path = fs::u8path(after_container_path);
                input.after_entry = after_container_entry;
                input.after_physical_cloth_profile = after_physical_cloth_profile == 1
                                                         ? PhysicalClothProfile::unreal_5_3_windows
                                                         : PhysicalClothProfile::unspecified;
                input.after_animation_profile = after_animation_profile == 1
                                                    ? CookedAnimationProfile::unreal_5_3_windows
                                                    : CookedAnimationProfile::unspecified;
                if (after_animation_interpolation)
                    input.after_animation_interpolation = after_animation_interpolation == 1
                                                              ? CompressedAnimationInterpolation::linear
                                                              : CompressedAnimationInterpolation::step;
                input.after_bone_settings_container = fs::u8path(after_bone_settings_container);
                input.after_skeleton_container = fs::u8path(after_skeleton_container);
                input.after_bone_settings_entry = after_bone_settings_entry;
                input.after_skeleton_entry = after_skeleton_entry;
                input.after_curve_settings_container = fs::u8path(after_curve_settings_container);
                input.after_curve_settings_entry = after_curve_settings_entry;
                input.after_physics_profile = after_physics_profile == 1
                                                  ? CookedPhysicsProfile::unreal_5_3_windows
                                                  : CookedPhysicsProfile::unspecified;
                input.after_mesh_profile = after_mesh_profile == 1 ? CookedMeshProfile::unreal_5_3_windows
                                                                   : CookedMeshProfile::unspecified;
                if (after_mesh_mobile_lod)
                    input.after_keep_mobile_min_lod_setting = after_mesh_mobile_lod == 2;
                input.after_nanite_profile = after_nanite_profile == 1 ? NaniteProfile::unreal_5_3_windows
                                                                       : NaniteProfile::unspecified;
                input.after_nanite_attributes = after_nanite_attributes;
                input.after_skeletal_profile = after_skeletal_profile == 1
                                                   ? CookedSkeletalProfile::unreal_5_3_windows
                                                   : CookedSkeletalProfile::unspecified;
                if (after_skeletal_mobile_lod)
                    input.after_skeletal_mobile_lod = after_skeletal_mobile_lod == 2;
                if (after_skeletal_colors)
                    input.after_skeletal_colors = after_skeletal_colors == 2;
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
        result.source_limits = {static_cast<std::uint32_t>(max_properties),
                                static_cast<std::uint64_t>(max_property_bytes)};
        result.after_source_limits = {static_cast<std::uint32_t>(after_max_properties),
                                      static_cast<std::uint64_t>(after_max_property_bytes)};
        result.source_limits.validate();
        result.after_source_limits.validate();
        result.inherit_after_source_limits = inherit_after_source_limits;
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
