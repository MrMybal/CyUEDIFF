#include <cyuediff/animation_pose_view.hpp>
#include <algorithm>
#include <set>
namespace cyuediff {
void AnimationPoseView::register_translations() {
    cyui::registerTranslations(
        "fr",
        {{"Animation hierarchy pose", "Pose hiérarchique de l’animation"},
         {"Animation pose", "Pose d’animation"},
         {"Some source animation joints are unavailable.",
          "Certaines articulations de l’animation source sont indisponibles."},
         {"No source animation positions available", "Aucune position d’animation source disponible"},
         {"Fit animation pose", "Cadrer la pose d’animation"},
         {"Affine source positions; colors show position or parent changes.",
          "Positions sources affines ; les couleurs indiquent les changements de position ou de parent."},
         {"Prepared source frames with an explicit Skeleton; missing tracks remain unavailable.",
          "Frames sources préparées avec un squelette explicite ; les pistes absentes restent "
          "indisponibles."},
         {"No prepared animation pose", "Aucune pose d’animation préparée"},
         {"Pose position tolerance", "Tolérance de position de la pose"},
         {"Find animation joint", "Rechercher une articulation de l’animation"},
         {"Joint details", "Détails de l’articulation"},
         {"Source position difference: %.6g", "Écart de position source : %.6g"},
         {"Source frame: %u", "Frame source : %u"},
         {"Source binding unavailable", "Association source indisponible"},
         {"Joint absent from selected hierarchy", "Articulation absente de la hiérarchie choisie"},
         {"Pose comparison incomplete", "Comparaison de poses incomplète"},
         {"Before", "Avant"},
         {"After", "Après"},
         {"Parent", "Parent"},
         {"Position", "Position"},
         {"Moved joints only", "Articulations déplacées uniquement"},
         {"Pose preview is limited to 4,096 joints.", "L’aperçu est limité à 4 096 articulations."},
         {"Click a joint: inspect | Alt + drag: orbit | Middle drag: pan | Wheel: zoom",
          "Clic sur une articulation : inspecter | Alt + glisser : orbite | Glisser central : déplacer | "
          "Molette : zoom"}});
}
void AnimationPoseView::set_poses(const AnimationPose *a, const AnimationPose *b, bool comparing) {
    comparing_ = comparing;
    truncated_ = false;
    position_tolerance_ = 0;
    selected_.clear();
    filter_ = {};
    std::set<std::string> keys;
    for (const auto *pose : {a, b})
        if (pose)
            for (const auto &joint : pose->display_pose.joints) {
                if (keys.size() < 4096 || keys.count(joint.name))
                    keys.insert(joint.name);
                else
                    truncated_ = true;
            }
    names_.assign(keys.begin(), keys.end());
    auto retain = [&](const AnimationPose *input) -> std::optional<AnimationPose> {
        if (!input)
            return {};
        AnimationPose out;
        out.animation_object = input->animation_object;
        out.skeleton_object = input->skeleton_object;
        out.frame = input->frame;
        out.time_seconds = input->time_seconds;
        out.hierarchy_complete = input->hierarchy_complete && !truncated_;
        out.display_pose.complete = input->display_pose.complete && !truncated_;
        out.display_pose.object_path = input->display_pose.object_path;
        for (const auto &joint : input->display_pose.joints)
            if (keys.count(joint.name)) {
                auto display_joint = joint;
                if (display_joint.ready)
                    for (double value : display_joint.position)
                        if (!std::isfinite(value) || std::abs(value) > 1e12) {
                            display_joint.ready = false;
                            out.display_pose.complete = false;
                        }
                out.display_pose.joints.push_back(std::move(display_joint));
                if (input->bindings.contains(joint.name))
                    out.bindings[joint.name] = input->bindings.at(joint.name);
            }
        if (!input->unbound_tracks.empty())
            out.display_pose.diagnostics.push_back("Unbound source tracks are present");
        return out;
    };
    before_ = retain(a);
    after_ = retain(b);
    if (!names_.empty())
        selected_ = names_.front();
    rebuild(false);
}
void AnimationPoseView::rebuild(bool preserve_camera) {
    const auto camera = preserve_camera ? viewport_.view_state() : Json::object();
    viewport_.set_models(before_ ? &before_->display_pose : nullptr, after_ ? &after_->display_pose : nullptr,
                         comparing_, position_tolerance_, PosePreviewSpace::source_animation);
    if (preserve_camera)
        viewport_.restore_view_state(camera);
    viewport_.select(selected_);
    comparison_ = Json::object();
    if (comparing_) {
        AnimationPose absent;
        absent.hierarchy_complete = true;
        absent.display_pose.complete = true;
        AnimationPoseDiffOptions o;
        o.position_tolerance = position_tolerance_;
        o.max_joint_pairs = 4096;
        comparison_ = compare_animation_poses(before_ ? *before_ : absent, after_ ? *after_ : absent, o);
    }
}
bool AnimationPoseView::select_joint(const std::string &name) {
    if (std::find(names_.begin(), names_.end(), name) == names_.end())
        return false;
    selected_ = name;
    viewport_.select(name);
    return true;
}
void AnimationPoseView::set_position_tolerance(double value) {
    if (!std::isfinite(value) || value < 0)
        throw Error("animation-pose-tolerance", "Position tolerance must be finite and nonnegative");
    if (value == position_tolerance_)
        return;
    position_tolerance_ = value;
    rebuild(true);
}
Json AnimationPoseView::view_state() const {
    return {{"selected", selected_},
            {"filter", filter_.query},
            {"positionTolerance", position_tolerance_},
            {"camera", viewport_.view_state()}};
}
void AnimationPoseView::restore_view_state(const Json &state) {
    view_state::validate(state, "animation-pose");
    set_position_tolerance(state.value("positionTolerance", 0.));
    filter_.query = state.value("filter", std::string{});
    if (state.contains("selected"))
        select_joint(state.at("selected").get<std::string>());
    if (state.contains("camera"))
        viewport_.restore_view_state(state.at("camera"));
}
void AnimationPoseView::draw(const char *id) {
    cyui::ScopedID scope(id);
    ImGui::TextWrapped(
        "%s", cyui::tr("Prepared source frames with an explicit Skeleton; missing tracks remain unavailable.")
                  .c_str());
    if (!before_ && !after_) {
        cyui::textDim("%s", cyui::tr("No prepared animation pose").c_str());
        return;
    }
    auto provenance = [](const std::optional<AnimationPose> &pose, const char *label) {
        if (!pose)
            return;
        if (pose->animation_object.empty() && pose->skeleton_object.empty())
            ImGui::TextWrapped("%s: %s", cyui::tr(label).c_str(),
                               cyui::tr("Source binding unavailable").c_str());
        else
            ImGui::TextWrapped("%s: %s | %s", cyui::tr(label).c_str(), pose->animation_object.c_str(),
                               pose->skeleton_object.c_str());
        cyui::textDim(cyui::tr("Source frame: %u").c_str(), pose->frame);
    };
    provenance(before_, "Before");
    provenance(after_, "After");
    if (truncated_)
        cyui::textDim("%s", cyui::tr("Pose preview is limited to 4,096 joints.").c_str());
    if (comparing_) {
        auto tolerance = position_tolerance_;
        if (ImGui::InputDouble(cyui::trLabel("Pose position tolerance").c_str(), &tolerance, 0, 0, "%.6g") &&
            std::isfinite(tolerance) && tolerance >= 0)
            set_position_tolerance(tolerance);
        if (comparison_.value("status", std::string{}) == "incomplete")
            cyui::textDim("%s", cyui::tr("Pose comparison incomplete").c_str());
    }
    auto available = ImGui::GetContentRegionAvail();
    const auto details_height = std::min(cyui::dp(270), std::max(cyui::dp(150), available.y * .4f));
    ImGui::BeginChild("pose-details", {0, details_height}, ImGuiChildFlags_Borders);
    cyui::FilterOptions filter_options;
    filter_options.hint = "Find animation joint";
    filter_options.width = std::min(cyui::dp(300), ImGui::GetContentRegionAvail().x);
    cyui::filterBox("##joint-filter", filter_, filter_options);
    if (ImGui::BeginCombo("##joint", selected_.c_str())) {
        for (const auto &name : names_)
            if (filter_.matches(name) && ImGui::Selectable(name.c_str(), name == selected_))
                select_joint(name);
        ImGui::EndCombo();
    }
    cyui::textDim("%s", cyui::tr("Joint details").c_str());
    auto detail = [&](const std::optional<AnimationPose> &pose, const char *side) {
        if (!pose)
            return;
        const auto it = std::find_if(pose->display_pose.joints.begin(), pose->display_pose.joints.end(),
                                     [&](const ReferenceJoint &j) { return j.name == selected_; });
        if (it == pose->display_pose.joints.end()) {
            cyui::textDim("%s: %s", cyui::tr(side).c_str(),
                          cyui::tr(pose->hierarchy_complete ? "Joint absent from selected hierarchy"
                                                            : "Source binding unavailable")
                              .c_str());
            return;
        }
        cyui::textDim("%s | %s: %s", cyui::tr(side).c_str(), cyui::tr("Parent").c_str(), it->parent.c_str());
        if (it->ready)
            ImGui::Text("%s: (%.6g, %.6g, %.6g)", cyui::tr("Position").c_str(), it->position[0],
                        it->position[1], it->position[2]);
        else
            cyui::textDim("%s", cyui::tr("Source binding unavailable").c_str());
    };
    detail(before_, "Before");
    detail(after_, "After");
    if (comparing_ && comparison_.contains("joints") && comparison_.at("joints").contains(selected_)) {
        const auto &joint = comparison_.at("joints").at(selected_);
        if (!joint.at("distance").is_null())
            ImGui::Text(cyui::tr("Source position difference: %.6g").c_str(),
                        joint.at("distance").get<double>());
    }
    ImGui::EndChild();
    viewport_.draw("animation-pose-viewport");
    const auto picked = viewport_.take_picked_joint();
    if (!picked.empty())
        select_joint(picked);
}
} // namespace cyuediff
