#include <cyuediff/reference_pose_viewport.hpp>
#include "diff_style.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace cyuediff {
void ReferencePoseViewport::set_models(const ReferencePose *before, const ReferencePose *after,
                                       bool comparing) {
    before_.clear();
    after_.clear();
    changes_.clear();
    selected_.clear();
    picked_.clear();
    comparing_ = comparing;
    changed_only_ = false;
    fit_pending_ = true;
    truncated_ = false;
    navigation_pane_ = 0;
    incomplete_ = (!before && !after) || (before && !before->complete) || (after && !after->complete);
    camera_ = {};
    camera_.camera.up = {0, 0, 1};
    camera_.camera.yaw = .7;
    camera_.camera.pitch = .35;
    std::set<std::string> keys;
    for (const auto *model : {before, after})
        if (model)
            for (const auto &joint : model->joints)
                keys.insert(joint.name);
    constexpr std::size_t limit = 4096;
    truncated_ = keys.size() > limit;
    std::set<std::string> displayed;
    for (const auto &key : keys) {
        if (displayed.size() == limit)
            break;
        displayed.insert(key);
    }
    auto copy = [&](const ReferencePose *model, auto &out) {
        if (model)
            for (const auto &joint : model->joints)
                if (displayed.count(joint.name)) {
                    auto limited_joint = joint;
                    if (limited_joint.ready)
                        for (double x : limited_joint.position)
                            if (std::abs(x) > 1e12) {
                                limited_joint.ready = false;
                                incomplete_ = true;
                            }
                    if (!out.emplace(joint.name, std::move(limited_joint)).second)
                        throw Error("skeleton-pose-identity", "Duplicate preview joint identity");
                }
    };
    copy(before, before_);
    copy(after, after_);
    // Classification is derived from the Core position comparison, separate from stored graph outlines.
    ReferencePose a, b;
    a.complete = !before;
    b.complete = !after; // Absent asset sides are known empty; unread selected sources are not.
    if (before) {
        a.object_path = before->object_path;
        a.retarget_source = before->retarget_source;
        a.complete = before->complete;
    }
    if (after) {
        b.object_path = after->object_path;
        b.retarget_source = after->retarget_source;
        b.complete = after->complete;
    }
    for (const auto &joint : before_)
        a.joints.push_back(joint.second);
    for (const auto &joint : after_)
        b.joints.push_back(joint.second);
    const auto comparison = compare_reference_poses(a, b);
    differences_ = comparison.joints;
    for (auto it = comparison.joints.begin(); it != comparison.joints.end(); ++it) {
        const auto kind = it->at("kind").get<std::string>();
        changes_[it.key()] = !comparing           ? ChangeKind::unchanged
                             : kind == "added"    ? ChangeKind::added
                             : kind == "removed"  ? ChangeKind::removed
                             : kind == "modified" ? ChangeKind::modified
                                                  : ChangeKind::unchanged;
    }
}
bool ReferencePoseViewport::visible(const std::string &name) const {
    const auto found = changes_.find(name);
    return !changed_only_ || !comparing_ ||
           (found != changes_.end() && found->second != ChangeKind::unchanged);
}
std::size_t ReferencePoseViewport::visible_joint_count(bool after) const {
    std::size_t count = 0;
    for (const auto &entry : after ? after_ : before_)
        if (entry.second.ready && visible(entry.first))
            ++count;
    return count;
}
void ReferencePoseViewport::fit(float aspect) {
    std::array<double, 3> low{}, high{};
    bool valid = false;
    for (const auto *model : {&before_, &after_})
        for (const auto &entry : *model)
            if (entry.second.ready && visible(entry.first)) {
                const auto &p = entry.second.position;
                if (!valid) {
                    low = high = p;
                    valid = true;
                } else
                    for (unsigned i = 0; i < 3; ++i) {
                        low[i] = std::min(low[i], p[i]);
                        high[i] = std::max(high[i], p[i]);
                    }
            }
    if (valid) {
        camera_.camera.target = {std::clamp(low[0] * .5 + high[0] * .5, -1e12, 1e12),
                                 std::clamp(low[1] * .5 + high[1] * .5, -1e12, 1e12),
                                 std::clamp(low[2] * .5 + high[2] * .5, -1e12, 1e12)};
        const double radius =
            std::max(1e-5, std::hypot(std::hypot(high[0] - low[0], high[1] - low[1]), high[2] - low[2]) * .5);
        camera_.camera.distance = std::clamp(radius * 3 / std::max(.1f, std::min(1.f, aspect)), 1e-5, 1e12);
    }
    fit_pending_ = false;
}
std::string ReferencePoseViewport::take_picked_joint() {
    auto name = std::move(picked_);
    picked_.clear();
    return name;
}
Json ReferencePoseViewport::view_state() const {
    Json out = changed_only_ ? Json{{"changedOnly", true}} : Json::object();
    if (fit_pending_ || (!visible_joint_count() && !visible_joint_count(true)))
        return out;
    const auto &c = camera_.camera;
    out.update({{"target", {c.target.x, c.target.y, c.target.z}},
                {"up", {c.up.x, c.up.y, c.up.z}},
                {"yaw", c.yaw},
                {"pitch", c.pitch},
                {"distance", c.distance}});
    return out;
}
void ReferencePoseViewport::restore_view_state(const Json &state) {
    view_state::validate(state, "mesh");
    if (state.contains("changedOnly"))
        changed_only_ = comparing_ && state["changedOnly"].get<bool>();
    if (state.empty() || (state.size() == 1 && state.contains("changedOnly")))
        return;
    if (!visible_joint_count() && !visible_joint_count(true))
        return;
    fit(1);
    auto &c = camera_.camera;
    auto vector = [&](const char *key, cyui::Vec3 &out) {
        if (state.contains(key))
            out = {state[key][0].get<double>(), state[key][1].get<double>(), state[key][2].get<double>()};
    };
    vector("target", c.target);
    vector("up", c.up);
    if (state.contains("yaw"))
        c.yaw = state["yaw"].get<double>();
    if (state.contains("pitch"))
        c.pitch = state["pitch"].get<double>();
    if (state.contains("distance"))
        c.distance = state["distance"].get<double>();
    camera_.orbiting = camera_.panning = false;
    navigation_pane_ = 0;
}
void ReferencePoseViewport::pane(const char *label, const std::map<std::string, ReferenceJoint> &model,
                                 ImVec2 size, int id, const cyui::OrbitCamera &shown) {
    cyui::ScopedID scope(label);
    ImGui::BeginGroup();
    cyui::textDim("%s", cyui::tr(label).c_str());
    size.y = std::max(cyui::dp(40), size.y - ImGui::GetTextLineHeightWithSpacing());
    const auto origin = ImGui::GetCursorScreenPos();
    const auto &io = ImGui::GetIO();
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                         ImGui::IsMouseHoveringRect(origin, {origin.x + size.x, origin.y + size.y});
    if (hovered && ((io.KeyAlt && ImGui::IsMouseClicked(0)) || ImGui::IsMouseClicked(2)))
        navigation_pane_ = id;
    const auto interaction = cyui::viewport("##pose-camera", camera_, {}, size,
                                            navigation_pane_ == id || (!navigation_pane_ && hovered));
    const auto matrix = cyui::cameraViewMatrix(shown);
    auto project = [&](const std::array<double, 3> &p, ImVec2 &screen) {
        const double x = matrix[0] * p[0] + matrix[4] * p[1] + matrix[8] * p[2] + matrix[12],
                     y = matrix[1] * p[0] + matrix[5] * p[1] + matrix[9] * p[2] + matrix[13],
                     z = matrix[2] * p[0] + matrix[6] * p[1] + matrix[10] * p[2] + matrix[14];
        if (z >= -std::max(1e-12, shown.distance * 1e-6))
            return false;
        const double sx = x / (-z * .41421356237) * (size.y / size.x), sy = y / (-z * .41421356237);
        if (!std::isfinite(sx) || !std::isfinite(sy) || std::abs(sx) > 1e5 || std::abs(sy) > 1e5)
            return false;
        screen = {origin.x + float((sx + 1) * .5 * size.x), origin.y + float((1 - sy) * .5 * size.y)};
        return true;
    };
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    double nearest = double(cyui::dp(8)) * cyui::dp(8);
    std::string hit;
    for (const auto &entry : model) {
        const auto &joint = entry.second;
        if (!joint.ready || !visible(entry.first))
            continue;
        ImVec2 point;
        if (!project(joint.position, point))
            continue;
        const auto color =
            comparing_ ? detail::change_color(changes_.at(entry.first)) : cyui::palette().accent;
        auto parent = model.find(joint.parent);
        if (parent != model.end() && parent->second.ready) {
            ImVec2 end;
            if (project(parent->second.position, end))
                draw->AddLine(end, point, color, cyui::dp(2));
        }
        draw->AddCircleFilled(point, cyui::dp(entry.first == selected_ ? 5.f : 3.f), color);
        if (entry.first == selected_) {
            draw->AddCircle(point, cyui::dp(8), cyui::palette().accent, 0, cyui::dp(2));
            draw->AddText({point.x + cyui::dp(10), point.y - cyui::dp(10)}, cyui::palette().text,
                          entry.first.c_str());
        }
        ++rendered_joints_;
        if (interaction.clicked &&
            ImGui::IsMouseHoveringRect(origin, {origin.x + size.x, origin.y + size.y})) {
            const double dx = point.x - io.MousePos.x, dy = point.y - io.MousePos.y, d = dx * dx + dy * dy;
            if (d < nearest) {
                nearest = d;
                hit = entry.first;
            }
        }
    }
    if (!hit.empty())
        picked_ = std::move(hit);
    if (model.empty())
        draw->AddText({origin.x + cyui::dp(10), origin.y + cyui::dp(10)}, cyui::palette().text,
                      cyui::tr("No reference positions available").c_str());
    draw->PopClipRect();
    ImGui::EndGroup();
}
void ReferencePoseViewport::draw(const char *id) {
    cyui::ScopedID scope(id);
    rendered_joints_ = 0;
    if (incomplete_)
        cyui::callout(cyui::Tone::warning, cyui::tr("Some reference joints are unavailable.").c_str());
    if (truncated_)
        cyui::textDim("%s", cyui::tr("Pose preview is limited to 4,096 joints.").c_str());
    if (comparing_) {
        if (ImGui::Checkbox(cyui::trLabel("Moved joints only").c_str(), &changed_only_))
            fit_pending_ = true;
    }
    if (cyui::button(cyui::trLabel("Fit reference pose").c_str()))
        fit_pending_ = true;
    ImGui::TextWrapped(
        "%s", cyui::tr("Affine reference positions; colors show position or parent changes.").c_str());
    ImGui::TextWrapped(
        "%s",
        cyui::tr("Click a joint: inspect | Alt + drag: orbit | Middle drag: pan | Wheel: zoom").c_str());
    auto size = ImGui::GetContentRegionAvail();
    if (size.x < cyui::dp(20) || size.y < cyui::dp(50))
        return;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const bool stacked = comparing_ && size.x < cyui::dp(700);
    const float width = comparing_ && !stacked ? (size.x - gap) * .5f : size.x;
    const float height = stacked ? (size.y - gap) * .5f : size.y;
    if (fit_pending_)
        fit(width / std::max(cyui::dp(1), height - ImGui::GetTextLineHeightWithSpacing()));
    const auto shown = camera_.camera;
    pane(comparing_ ? "Before" : "Reference pose", !comparing_ && before_.empty() ? after_ : before_,
         {width, height}, 1, shown);
    if (comparing_) {
        if (!stacked)
            ImGui::SameLine();
        pane("After", after_, {width, height}, 2, shown);
    }
    if (!ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(2))
        navigation_pane_ = 0;
}
} // namespace cyuediff
