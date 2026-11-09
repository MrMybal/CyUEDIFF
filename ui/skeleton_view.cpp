#include <cyuediff/skeleton_view.hpp>
#include "diff_style.hpp"
#include "../src/source_availability.hpp"
#include "../src/skeleton_pose_internal.hpp"
#include <algorithm>
#include <set>
namespace cyuediff {
const Json *SkeletonView::body(const Json &model) const {
    if (model.is_null() || object_.empty())
        return nullptr;
    auto it = model.at("skeletons").find(object_);
    return it == model.at("skeletons").end() ? nullptr : &*it;
}
void SkeletonView::bind() {
    nodes_.clear();
    links_.clear();
    changes_.clear();
    state_ = {};
    fit_ = true;
    truncated_ = false;
    comparison_unavailable_ = comparing_ && unavailable_objects_.count(object_);
    const bool comparing = comparing_ && !comparison_unavailable_;
    const auto *a = body(before_), *b = body(after_);
    complete_ = (!a || a->at("complete").get<bool>()) && (!b || b->at("complete").get<bool>()) &&
                (comparing_ ? a && b : a || b);
    ValueCoverage coverage;
    if (comparison_unavailable_)
        coverage.comparison_unavailable.push_back("");
    metadata_.set_values(a, b, comparing_, coverage);
    attachments_.select_owner(object_);
    if (comparison_unavailable_) {
        const auto *local = body(animation_before_);
        if (!local)
            local = body(animation_after_);
        animation_.set_models(local, nullptr, false);
    } else
        animation_.set_models(body(animation_before_), body(animation_after_), comparing);
    std::set<std::string> sources;
    for (const auto *object : {a, b})
        if (object)
            for (auto it = object->at("retargeting").at("sources").begin();
                 it != object->at("retargeting").at("sources").end(); ++it)
                sources.insert(it.key());
    retarget_sources_.assign(sources.begin(), sources.end());
    bind_pose();
    const auto *bones_a = a ? &a->at("bones") : nullptr, *bones_b = b ? &b->at("bones") : nullptr;
    auto value = [](const Json *model, const std::string &key) -> const Json * {
        if (!model)
            return nullptr;
        auto it = model->find(key);
        return it == model->end() ? nullptr : &*it;
    };
    std::set<std::string> keys;
    for (const auto *model : {bones_a, bones_b})
        if (model)
            for (auto it = model->begin(); it != model->end(); ++it)
                keys.insert(it.key());
    // Bound canvas work independently of the full Core snapshot and generic property view.
    constexpr std::size_t max_nodes = 4096;
    std::map<std::string, cyui::ItemId> ids;
    std::vector<GraphInspectionItem> inspection;
    cyui::ItemId next = 1;
    std::map<unsigned, unsigned> rows;
    low_ = high_ = {};
    for (const auto &key : keys) {
        if (nodes_.size() >= max_nodes) {
            truncated_ = true;
            break;
        }
        const auto *x = value(bones_a, key), *y = value(bones_b, key);
        unsigned depth = 0;
        std::string ancestor = key;
        std::set<std::string> visited;
        while (depth < 32 && visited.insert(ancestor).second) {
            const auto *parent = value(bones_b, ancestor);
            if (!parent)
                parent = value(bones_a, ancestor);
            if (!parent || parent->at("parent").is_null())
                break;
            ancestor = parent->at("parent").get<std::string>();
            ++depth;
        }
        cyui::Node node;
        node.id = next;
        next += 3;
        node.label = key;
        node.position = {double(depth) * 250., double(rows[depth]++) * 115.};
        node.inputs.push_back({node.id + 1, cyui::tr("Parent"), 6});
        node.outputs.push_back({node.id + 2, cyui::tr("Children"), 6});
        ids[key] = node.id;
        const auto change = comparing                 ? classify_change(x, y)
                            : comparison_unavailable_ ? ChangeKind::unknown
                                                      : ChangeKind::unchanged;
        changes_[node.id] = change;
        inspection.push_back({node.id, key, key, change});
        high_.x = std::max(high_.x, node.position.x + 210.);
        high_.y = std::max(high_.y, node.position.y + 90.);
        nodes_.push_back(std::move(node));
    }
    rendered_locale_ = cyui::locale();
    std::set<std::pair<std::string, std::string>> relations;
    for (const auto *model : {bones_a, bones_b})
        if (model)
            for (auto it = model->begin(); it != model->end(); ++it)
                if (!it->at("parent").is_null() && ids.count(it.key()) &&
                    ids.count(it->at("parent").get<std::string>()))
                    relations.insert({it->at("parent").get<std::string>(), it.key()});
    for (const auto &relation : relations) {
        auto parent = ids.find(relation.first), child = ids.find(relation.second);
        if (parent != ids.end() && child != ids.end())
            links_.push_back({next++, parent->second + 2, child->second + 1, 6});
    }
    Json visible_a = Json::object(), visible_b = Json::object();
    for (const auto &id : ids) {
        if (const auto *entry = value(bones_a, id.first))
            visible_a[id.first] = *entry;
        if (const auto *entry = value(bones_b, id.first))
            visible_b[id.first] = *entry;
    }
    inspector_.set_nodes(bones_a ? &visible_a : nullptr, bones_b ? &visible_b : nullptr,
                         std::move(inspection), comparing);
}
void SkeletonView::set_packages(const Package *before, const Package *after, bool comparing) {
    SkeletonOptions options;
    options.source_indices = true;
    before_ = before ? skeleton_snapshot(*before, options) : Json(nullptr);
    after_ = after ? skeleton_snapshot(*after, options) : Json(nullptr);
    unavailable_objects_.clear();
    if (before && after) {
        const auto raw = detail::source_coverage(*before, *after);
        const auto coverage = detail::projection_coverage(raw, *before, *after, before_, after_, "skeletons");
        for (const auto &path : coverage.suppressed) {
            Json::json_pointer pointer(path);
            if (!pointer.empty())
                unavailable_objects_.insert(pointer.back());
        }
    }
    AttachmentOptions attachment_options;
    attachment_options.source_indices = false;
    attachments_.set_models(before ? attachment_snapshot(*before, attachment_options) : Json(nullptr),
                            after ? attachment_snapshot(*after, attachment_options) : Json(nullptr), before_,
                            after_, comparing || (before && after), before && before->complete_input,
                            after && after->complete_input);
    attachments_preview_ = false;
    animation_before_ = before ? skeleton_animation_snapshot(*before) : Json(nullptr);
    animation_after_ = after ? skeleton_animation_snapshot(*after) : Json(nullptr);
    animation_preview_ = false;
    poses_before_.clear();
    poses_after_.clear();
    pose_preview_ = false;
    source_before_.clear();
    source_after_.clear();
    auto prepare = [&](Json &model, auto &poses, bool complete_input) {
        if (model.is_null())
            return;
        for (auto it = model["skeletons"].begin(); it != model["skeletons"].end(); ++it) {
            auto compose = [&](const std::string &source) {
                ReferencePoseOptions pose_options;
                pose_options.retarget_source = source;
                try {
                    poses[{it.key(), source}] =
                        detail::compose_reference_pose(*it, it.key(), complete_input, pose_options);
                } catch (const Error &error) {
                    ReferencePose unavailable;
                    unavailable.object_path = it.key();
                    unavailable.retarget_source = source;
                    unavailable.diagnostics.push_back(error.what());
                    poses[{it.key(), source}] = std::move(unavailable);
                }
            };
            compose("");
            for (auto source = (*it)["retargeting"]["sources"].begin();
                 source != (*it)["retargeting"]["sources"].end(); ++source)
                compose(source.key());
            for (auto &source : (*it)["retargeting"]["sources"])
                for (auto &bone : source["bones"])
                    bone.erase("sourceIndex");
            // Stored-node outlines stay independent of source array order and derived display positions.
            for (auto &bone : (*it)["bones"])
                bone.erase("sourceIndex");
        }
    };
    prepare(before_, poses_before_, before && before->complete_input);
    prepare(after_, poses_after_, after && after->complete_input);
    comparing_ = comparing || (before && after);
    std::set<std::string> paths;
    for (const auto *model : {&before_, &after_})
        if (!model->is_null())
            for (auto it = model->at("skeletons").begin(); it != model->at("skeletons").end(); ++it)
                paths.insert(it.key());
    objects_.assign(paths.begin(), paths.end());
    object_ = objects_.empty() ? "" : objects_.front();
    bind();
}
bool SkeletonView::select_object(const std::string &path) {
    if (!path.empty() && std::find(objects_.begin(), objects_.end(), path) == objects_.end())
        return false;
    object_ = path;
    source_before_.clear();
    source_after_.clear();
    bind();
    return true;
}
void SkeletonView::bind_pose() {
    auto find = [&](const auto &poses, const std::string &source) {
        const auto pose = poses.find({object_, source});
        if (pose != poses.end())
            return pose->second;
        ReferencePose unavailable;
        unavailable.object_path = object_;
        unavailable.retarget_source = source;
        unavailable.diagnostics.push_back(
            "Selected stored source is unavailable; raw pose is not substituted");
        return unavailable;
    };
    selected_before_ = find(poses_before_, source_before_);
    selected_after_ = find(poses_after_, source_after_);
    pose_view_.set_models(body(before_) ? &selected_before_ : nullptr,
                          body(after_) ? &selected_after_ : nullptr, comparing_ && !comparison_unavailable_);
    pose_selection_.clear();
    pose_details_.set_values(nullptr, nullptr, comparing_);
    update_pose_selection();
}
bool SkeletonView::select_retarget_sources(std::string before, std::string after) {
    const auto properties = pose_details_.view_state();
    source_before_ = std::move(before);
    source_after_ = std::move(after);
    bind_pose();
    pose_details_.restore_view_state(properties);
    return (source_before_.empty() || poses_before_.count({object_, source_before_})) &&
           (source_after_.empty() || poses_after_.count({object_, source_after_}));
}
void SkeletonView::update_pose_selection() {
    const auto &selected = inspector_.selected_node();
    if (pose_selection_ == selected)
        return;
    pose_selection_ = selected;
    pose_view_.select(selected);
    auto model = [&](const Json &snapshot, const ReferencePose &pose, const std::string &source) -> Json {
        const auto *object = body(snapshot);
        if (!object || !object->at("bones").contains(selected))
            return nullptr;
        auto stored = object->at("bones").at(selected);
        if (!source.empty()) {
            stored.erase("localPose");
            const auto found = object->at("retargeting").at("sources").find(source);
            if (found != object->at("retargeting").at("sources").end() &&
                found->at("bones").contains(selected))
                stored["localPose"] = found->at("bones").at(selected).at("localPose");
        }
        Json value = {{"stored", std::move(stored)}};
        if (!source.empty())
            value["retargetSource"] = source;
        for (const auto &joint : pose.joints)
            if (joint.name == selected) {
                value["reference"] = {{"position", joint.ready ? Json(joint.position) : Json(nullptr)},
                                      {"ready", joint.ready},
                                      {"diagnostics", joint.diagnostics}};
                break;
            }
        if (pose.joints.empty())
            value["reference"] = {{"position", nullptr}, {"ready", false}, {"diagnostics", pose.diagnostics}};
        return value;
    };
    const auto a = model(before_, selected_before_, source_before_),
               b = model(after_, selected_after_, source_after_);
    pose_details_.set_values(a.is_null() ? nullptr : &a, b.is_null() ? nullptr : &b,
                             comparing_ && !comparison_unavailable_);
}
bool SkeletonView::select_bone(const std::string &name) {
    const bool selected = inspector_.select_node(name);
    update_pose_selection();
    return selected;
}
bool SkeletonView::next_changed_bone(bool previous) {
    const bool selected = comparison_available() && inspector_.next_change(previous);
    update_pose_selection();
    return selected;
}
void SkeletonView::set_filter(std::string query) { inspector_.set_filter(std::move(query)); }
void SkeletonView::set_changed_only(bool enabled) { inspector_.set_changed_only(enabled); }
Json SkeletonView::view_state() const {
    if (empty())
        return Json::object();
    Json out = {{"object", object_.empty() ? Json(nullptr) : Json(object_)},
                {"inspection", inspector_.view_state()}};
    if (!fit_) {
        const auto &camera = state_.canvas.view;
        out["canvas"] = {{"pan", {camera.origin.x, camera.origin.y}}, {"zoom", camera.zoom}};
    }
    if (attachments_preview_)
        out["attachmentsPreview"] = true;
    const auto attachment_state = attachments_.view_state();
    if (!attachment_state.empty())
        out["attachments"] = attachment_state;
    if (animation_preview_)
        out["animationPreview"] = true;
    const auto animation_state = animation_.view_state();
    if (!animation_state.empty())
        out["animation"] = animation_state;
    if (pose_preview_)
        out["posePreview"] = true;
    if (!source_before_.empty() || !source_after_.empty()) {
        out["beforeRetargetSource"] = source_before_;
        out["afterRetargetSource"] = source_after_;
    }
    const auto pose_camera = pose_view_.view_state();
    if (!pose_camera.empty())
        out["poseCamera"] = pose_camera;
    const auto pose_properties = pose_details_.view_state();
    if (!pose_properties.empty())
        out["poseProperties"] = pose_properties;
    const auto metadata = metadata_.view_state();
    if (!metadata.empty())
        out["metadata"] = metadata;
    return out;
}
void SkeletonView::restore_view_state(const Json &state) {
    view_state::validate(state, "skeleton");
    if (state.contains("object")) {
        const auto path = state["object"].is_null() ? "" : state["object"].get<std::string>();
        if (!select_object(path))
            select_object("");
    }
    if (state.contains("inspection"))
        inspector_.restore_view_state(state["inspection"]);
    if (state.contains("metadata"))
        metadata_.restore_view_state(state["metadata"]);
    if (state.contains("attachmentsPreview"))
        set_attachments_preview(state.at("attachmentsPreview").get<bool>());
    if (state.contains("attachments"))
        attachments_.restore_view_state(state.at("attachments"));
    if (state.contains("animationPreview"))
        set_animation_preview(state.at("animationPreview").get<bool>());
    if (state.contains("animation"))
        animation_.restore_view_state(state.at("animation"));
    if (state.contains("posePreview"))
        set_pose_preview(state["posePreview"].get<bool>());
    if (state.contains("beforeRetargetSource") || state.contains("afterRetargetSource"))
        select_retarget_sources(state.value("beforeRetargetSource", source_before_),
                                state.value("afterRetargetSource", source_after_));
    if (state.contains("poseCamera"))
        pose_view_.restore_view_state(state["poseCamera"]);
    update_pose_selection();
    if (state.contains("poseProperties"))
        pose_details_.restore_view_state(state["poseProperties"]);
    const bool explicit_camera = state.contains("canvas") && !state["canvas"].empty();
    if (!explicit_camera && state.contains("inspection") && state["inspection"].contains("selected") &&
        state["inspection"]["selected"].is_string())
        inspector_.select_node(state["inspection"]["selected"].get<std::string>());
    if (explicit_camera && !nodes_.empty()) {
        const auto &canvas = state["canvas"];
        if (canvas.contains("pan"))
            state_.canvas.view.origin = {canvas["pan"][0].get<double>(), canvas["pan"][1].get<double>()};
        if (canvas.contains("zoom"))
            state_.canvas.view.zoom = canvas["zoom"].get<double>();
        fit_ = false;
    }
}
void SkeletonView::draw(const char *id) {
    if (comparison_unavailable_)
        cyui::callout(cyui::Tone::info,
                      cyui::tr("Comparison is unavailable for this view because source values are unread or "
                               "use incompatible decoders. Inspect each revision or use scoped properties.")
                          .c_str());
    cyui::ScopedID scope(id);
    if (rendered_locale_ != cyui::locale()) {
        rendered_locale_ = cyui::locale();
        for (auto &node : nodes_) {
            node.inputs[0].label = cyui::tr("Parent");
            node.outputs[0].label = cyui::tr("Children");
        }
    }
    if (empty()) {
        cyui::emptyState(cyui::tr("No stored reference skeleton").c_str(),
                         cyui::tr("This package has no supported Skeleton or SkeletalMesh object.").c_str(),
                         cyui::icon::document);
        return;
    }
    ImGui::TextWrapped(
        "%s", cyui::tr("Raw reference hierarchy and local pose; animations and skinning are not evaluated.")
                  .c_str());
    if (ImGui::BeginCombo(cyui::trLabel("Skeleton object").c_str(), object_.c_str())) {
        for (const auto &path : objects_)
            if (ImGui::Selectable(path.c_str(), path == object_))
                select_object(path);
        ImGui::EndCombo();
    }
    if (!complete_)
        cyui::callout(cyui::Tone::warning,
                      cyui::tr("Skeleton data is incomplete. Unknown native data remains opaque.").c_str());
    if (truncated_)
        cyui::callout(
            cyui::Tone::warning,
            cyui::tr(
                "Hierarchy diagram is limited to 4,096 bones. Complete stored data remains in Properties.")
                .c_str());

    if (ImGui::CollapsingHeader(cyui::trLabel("Skeleton properties").c_str())) {
        ImGui::BeginChild("Skeleton metadata panel", {0, cyui::dp(200)}, ImGuiChildFlags_Borders);
        metadata_.draw("Skeleton metadata");
        ImGui::EndChild();
    }
    if (ImGui::RadioButton(cyui::trLabel("Hierarchy").c_str(),
                           !pose_preview_ && !animation_preview_ && !attachments_preview_)) {
        pose_preview_ = false;
        animation_preview_ = false;
        attachments_preview_ = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(cyui::trLabel("Reference pose").c_str(), pose_preview_))
        set_pose_preview(true);
    if (ImGui::GetContentRegionAvail().x > cyui::dp(230))
        ImGui::SameLine();
    if (ImGui::RadioButton(cyui::trLabel("Animation metadata").c_str(), animation_preview_))
        set_animation_preview(true);
    if (ImGui::GetContentRegionAvail().x > cyui::dp(200))
        ImGui::SameLine();
    if (ImGui::RadioButton(cyui::trLabel("Attachments").c_str(), attachments_preview_))
        set_attachments_preview(true);
    if (attachments_preview_) {
        attachments_.draw();
        return;
    }
    if (animation_preview_) {
        animation_.draw();
        return;
    }
    if (nodes_.empty()) {
        metadata_.draw("Skeleton metadata");
        return;
    }
    if (pose_preview_) {
        if (!retarget_sources_.empty() || !source_before_.empty() || !source_after_.empty()) {
            auto choice = [&](const char *label, bool after) {
                const auto &current = after ? source_after_ : source_before_;
                ImGui::SetNextItemWidth(
                    std::min(cyui::dp(360), std::max(cyui::dp(80), ImGui::GetContentRegionAvail().x * .65f)));
                if (ImGui::BeginCombo(cyui::trLabel(label).c_str(),
                                      current.empty() ? cyui::tr("Stored raw reference").c_str()
                                                      : current.c_str())) {
                    auto choose = [&](const std::string &key) {
                        select_retarget_sources(after ? source_before_ : key, after ? key : source_after_);
                    };
                    if (ImGui::Selectable(cyui::trLabel("Stored raw reference").c_str(), current.empty()))
                        choose("");
                    for (const auto &source : retarget_sources_)
                        if (ImGui::Selectable(source.c_str(), source == current))
                            choose(source);
                    ImGui::EndCombo();
                }
            };
            choice(comparing_ ? "Retarget source before" : "Retarget source", !comparing_ && !body(before_));
            if (comparing_)
                choice("Retarget source after", true);
        }
        update_pose_selection();
        const auto &selected = inspector_.selected_node();
        ImGui::SetNextItemWidth(std::min(cyui::dp(300), ImGui::GetContentRegionAvail().x));
        if (ImGui::BeginCombo(cyui::trLabel("Bone").c_str(), selected.c_str())) {
            for (const auto &node : nodes_)
                if (ImGui::Selectable(node.label.c_str(), node.label == selected))
                    select_bone(node.label);
            ImGui::EndCombo();
        }
        if (comparing_ && !selected.empty()) {
            const auto *difference = pose_view_.joint_difference(selected);
            if (difference && difference->at("distance").is_number()) {
                const auto &delta = difference->at("delta");
                ImGui::TextWrapped(
                    cyui::tr("Reference displacement: %.6g | delta (%.6g, %.6g, %.6g)").c_str(),
                    difference->at("distance").get<double>(), delta[0].get<double>(), delta[1].get<double>(),
                    delta[2].get<double>());
            }
        }
        const auto available = ImGui::GetContentRegionAvail();
        const bool details = !selected.empty();
        const float height = details ? std::max(cyui::dp(180), available.y * .62f) : available.y;
        ImGui::BeginChild("##reference-pose-panel", {0, height});
        pose_view_.draw();
        const auto picked = pose_view_.take_picked_joint();
        ImGui::EndChild();
        if (!picked.empty())
            select_bone(picked);
        if (details) {
            ImGui::BeginChild("##reference-joint-details", {0, 0}, ImGuiChildFlags_Borders);
            pose_details_.draw("Reference joint details");
            ImGui::EndChild();
        }
        return;
    }
    if (comparing_)
        cyui::textDim("%s", cyui::tr("Connections include both stored parent relationships.").c_str());
    inspector_.draw(state_, nodes_, links_, changes_, fit_, low_, high_);
}
} // namespace cyuediff
