#include <cyuediff/collision_viewport.hpp>
#include "diff_style.hpp"
#include "../src/source_availability.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace cyuediff {
namespace {
const Json *shape_value(const Json *snapshot, const std::string &body, const std::string &key) {
    if (!snapshot || snapshot->is_null())
        return nullptr;
    auto b = snapshot->at("bodies").find(body);
    if (b == snapshot->at("bodies").end())
        return nullptr;
    auto s = b->at("shapes").find(key);
    return s == b->at("shapes").end() ? nullptr : &*s;
}
bool collection_known(const Json *snapshot, const CollisionWireShape &shape) {
    if (!snapshot || snapshot->is_null())
        return true; // An absent document is an actual missing side.
    auto body = snapshot->at("bodies").find(shape.body);
    if (body == snapshot->at("bodies").end())
        return snapshot->value("bodyTableKnown", false);
    if (!shape.collection.empty()) {
        if (body->at("collections").contains(shape.collection))
            return true;
        if (body->value("nativeFormatTableKnown", false)) {
            for (const auto &prefix : body->at("unavailableFormats"))
                if (shape.collection.rfind(prefix.get<std::string>(), 0) == 0)
                    return false;
            return true; // The complete native format table proves this collection absent.
        }
        return false;
    }
    const char *collection = shape.kind == "sphere"            ? "SphereElems"
                             : shape.kind == "box"             ? "BoxElems"
                             : shape.kind == "capsule"         ? "SphylElems"
                             : shape.kind == "tapered-capsule" ? "TaperedCapsuleElems"
                             : shape.kind == "convex"          ? "ConvexElems"
                                                               : "";
    return body->at("collections").contains(collection);
}
void include(CollisionBounds &out, const CollisionBounds &other) {
    if (!other.valid)
        return;
    if (!out.valid) {
        out = other;
        return;
    }
    for (unsigned i = 0; i < 3; ++i) {
        out.low[i] = std::min(out.low[i], other.low[i]);
        out.high[i] = std::max(out.high[i], other.high[i]);
    }
}
} // namespace
void CollisionViewport::set_models(CollisionWireframe before, CollisionWireframe after, const Json *a,
                                   const Json *b, bool comparing, const ValueCoverage &coverage) {
    changes_.clear();
    stored_changes_.clear();
    comparing_ = comparing;
    detail::SourceCoverage unavailable;
    for (const auto &path : coverage.comparison_unavailable)
        unavailable.suppressed.insert(path);
    auto escaped = [](const std::string &s) {
        std::string out;
        for (char c : s)
            out += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
        return out;
    };
    std::set<std::pair<std::string, std::string>> ready_before, ready_after;
    for (const auto &s : before.shapes)
        if (s.ready())
            ready_before.emplace(s.body, s.identity);
    for (const auto &s : after.shapes)
        if (s.ready())
            ready_after.emplace(s.body, s.identity);
    for (const auto *model : {&before, &after})
        for (const auto &s : model->shapes) {
            const auto key = std::make_pair(s.body, s.identity);
            if (changes_.count(key))
                continue;
            const auto *x = shape_value(a, s.body, s.identity), *y = shape_value(b, s.body, s.identity);
            const bool known = (x || collection_known(a, s)) && (y || collection_known(b, s));
            const auto stored = comparing && unavailable.intersects("/bodies/" + escaped(s.body))
                                    ? ChangeKind::unknown
                                : comparing && known ? classify_change(x, y)
                                                     : ChangeKind::unchanged;
            stored_changes_[key] = stored;
            const bool ready = (!x || ready_before.count(key)) && (!y || ready_after.count(key));
            changes_[key] = ready ? stored : ChangeKind::unchanged;
        }
    before_ = std::move(before);
    after_ = std::move(after);
    picked_.clear();
    body_.clear();
    shape_.clear();
    changed_only_ = false;
    fit_pending_ = true;
    fit_changes_ = false;
    navigation_pane_ = 0;
    camera_ = {};
    camera_.camera.up = {0, 0, 1};
    camera_.camera.yaw = .7;
    camera_.camera.pitch = .35;
}
void CollisionViewport::select(const std::string &body, const std::string &shape) {
    if (body_ == body && shape_ == shape)
        return;
    picked_.clear();
    body_ = body;
    shape_ = shape;
    fit_pending_ = true;
    fit_changes_ = false;
}
bool CollisionViewport::visible(const CollisionWireShape &shape, bool changes) const {
    if (body_.empty() || shape.body != body_ || (!shape_.empty() && shape.identity != shape_))
        return false;
    auto found = changes_.find({shape.body, shape.identity});
    return !((changed_only_ || changes) && comparing_) ||
           (found != changes_.end() && found->second != ChangeKind::unchanged);
}
CollisionBounds CollisionViewport::bounds(bool changes) const {
    CollisionBounds result;
    for (const auto *model : {&before_, &after_})
        for (const auto &s : model->shapes)
            if (visible(s, changes))
                include(result, s.bounds);
    return result;
}
void CollisionViewport::fit(float aspect) {
    auto box = bounds(fit_changes_);
    if (box.valid) {
        CollisionPoint diagonal{};
        for (unsigned i = 0; i < 3; ++i)
            diagonal[i] = box.high[i] - box.low[i];
        camera_.camera.target = {std::clamp(box.low[0] * .5 + box.high[0] * .5, -1e12, 1e12),
                                 std::clamp(box.low[1] * .5 + box.high[1] * .5, -1e12, 1e12),
                                 std::clamp(box.low[2] * .5 + box.high[2] * .5, -1e12, 1e12)};
        const double radius =
            std::max(1e-5, std::hypot(std::hypot(diagonal[0], diagonal[1]), diagonal[2]) * .5);
        camera_.camera.distance = std::clamp(radius * 3 / std::max(.1f, std::min(1.f, aspect)), 1e-5, 1e12);
    }
    fit_pending_ = fit_changes_ = false;
}
void CollisionViewport::set_changed_only(bool value) {
    changed_only_ = comparing_ && value;
}
bool CollisionViewport::focus_changes() {
    if (!comparing_ || !bounds(true).valid)
        return false;
    fit_pending_ = fit_changes_ = true;
    return true;
}
bool CollisionViewport::set_camera_view(CollisionCameraView view) {
    if (!bounds().valid)
        return false;
    if (fit_pending_)
        fit(1);
    auto &c = camera_.camera;
    switch (view) {
    case CollisionCameraView::perspective:
        c.yaw = .7;
        c.pitch = .35;
        break;
    case CollisionCameraView::front:
        c.yaw = 0;
        c.pitch = 0;
        break;
    case CollisionCameraView::right:
        c.yaw = 1.5707963267948966;
        c.pitch = 0;
        break;
    case CollisionCameraView::top:
        c.yaw = 0;
        c.pitch = 1.55;
        break;
    default:
        return false;
    }
    c.up = {0, 0, 1};
    return true;
}
Json CollisionViewport::view_state() const {
    Json result = changed_only_ ? Json{{"changedOnly", true}} : Json::object();
    if (fit_pending_ || !bounds().valid)
        return result;
    const auto &c = camera_.camera;
    result.update({{"target", {c.target.x, c.target.y, c.target.z}},
                   {"up", {c.up.x, c.up.y, c.up.z}},
                   {"yaw", c.yaw},
                   {"pitch", c.pitch},
                   {"distance", c.distance}});
    return result;
}
void CollisionViewport::restore_view_state(const Json &state) {
    view_state::validate(state, "mesh");
    if (state.empty())
        return;
    if (state.contains("changedOnly"))
        set_changed_only(state["changedOnly"].get<bool>());
    if (state.size() == 1 && state.contains("changedOnly"))
        return;
    if (!bounds().valid)
        return;
    if (fit_pending_)
        fit(1);
    fit_pending_ = fit_changes_ = false;
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
std::string CollisionViewport::take_picked_shape() {
    auto key = std::move(picked_);
    picked_.clear();
    return key;
}
std::size_t CollisionViewport::visible_edge_count(bool after) const {
    std::size_t count = 0;
    for (const auto &s : (after ? after_ : before_).shapes)
        if (visible(s))
            count += s.lines.size();
    return count;
}
ChangeKind CollisionViewport::stored_change(const std::string &body, const std::string &shape) const {
    auto i = stored_changes_.find({body, shape});
    return i == stored_changes_.end() ? ChangeKind::unchanged : i->second;
}
ChangeCounts CollisionViewport::changes() const {
    ChangeCounts result;
    for (const auto &entry : changes_)
        if (entry.first.first == body_ && (shape_.empty() || entry.first.second == shape_))
            result.add(entry.second);
    return result;
}
void CollisionViewport::pane(const char *label, const CollisionWireframe &model, ImVec2 size, int pane_id,
                             const cyui::OrbitCamera &shown) {
    cyui::ScopedID scope(label);
    ImGui::BeginGroup();
    cyui::textDim("%s", cyui::tr(label).c_str());
    size.y = std::max(cyui::dp(40), size.y - ImGui::GetTextLineHeightWithSpacing());
    const auto origin = ImGui::GetCursorScreenPos();
    const auto &io = ImGui::GetIO();
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                         ImGui::IsMouseHoveringRect(origin, {origin.x + size.x, origin.y + size.y});
    if (hovered && ((io.KeyAlt && ImGui::IsMouseClicked(0)) || ImGui::IsMouseClicked(2)))
        navigation_pane_ = pane_id;
    const auto interaction = cyui::viewport("##collision-camera", camera_, {}, size,
                                            navigation_pane_ == pane_id || (!navigation_pane_ && hovered));
    const auto matrix = cyui::cameraViewMatrix(shown);
    auto project = [&](const CollisionPoint &p, ImVec2 &screen) {
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
    std::size_t unavailable = 0, ready = 0;
    double nearest = double(cyui::dp(6)) * cyui::dp(6);
    std::string hit;
    auto distance_squared = [&](ImVec2 a, ImVec2 b) {
        const double dx = b.x - a.x, dy = b.y - a.y, len = dx * dx + dy * dy;
        const double t =
            len > 0 ? std::clamp(((io.MousePos.x - a.x) * dx + (io.MousePos.y - a.y) * dy) / len, 0., 1.) : 0;
        const double x = io.MousePos.x - a.x - t * dx, y = io.MousePos.y - a.y - t * dy;
        return x * x + y * y;
    };
    for (const auto &shape : model.shapes)
        if (visible(shape)) {
            if (!shape.ready()) {
                ++unavailable;
                continue;
            }
            ++ready;
            const auto change = changes_.at({shape.body, shape.identity});
            for (const auto &edge : shape.lines) {
                ImVec2 a, b;
                if (project(edge.a, a) && project(edge.b, b)) {
                    draw->AddLine(a, b, comparing_ ? detail::change_color(change) : cyui::palette().accent,
                                  cyui::dp(change == ChangeKind::unchanged ? 1.f : 2.f));
                    ++rendered_edges_;
                    if (interaction.clicked) {
                        const double distance = distance_squared(a, b);
                        if (distance < nearest) {
                            nearest = distance;
                            hit = shape.identity;
                        }
                    }
                }
            }
        }
    if (!hit.empty())
        picked_ = std::move(hit);
    const char *notice = unavailable ? "Some shapes have no supported stored geometry."
                         : !ready    ? "No visible collision geometry"
                                     : nullptr;
    if (notice)
        draw->AddText({origin.x + cyui::dp(10), origin.y + cyui::dp(10)}, cyui::palette().text,
                      cyui::tr(notice).c_str());
    draw->PopClipRect();
    ImGui::EndGroup();
}
void CollisionViewport::draw(const char *id) {
    cyui::ScopedID scope(id);
    rendered_edges_ = 0;
    if (body_.empty()) {
        cyui::textDim("%s", cyui::tr("Select a body to preview its local geometry.").c_str());
        return;
    }
    ImGui::TextWrapped(
        "%s", cyui::tr(before_.space == "cooker" || after_.space == "cooker"
                           ? "Cooker-coordinate preview; bone and component placement is unavailable."
                           : "Body-local preview; bone and component placement is unavailable.")
                  .c_str());
    auto inline_next = [&](float width) {
        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        if (right - ImGui::GetItemRectMax().x > width + ImGui::GetStyle().ItemSpacing.x)
            ImGui::SameLine();
    };
    auto button_width = [&](const char *label) {
        return ImGui::CalcTextSize(cyui::tr(label).c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
    };
    if (comparing_) {
        detail::change_legend(changes());
        ImGui::Checkbox(cyui::trLabel("Changed shapes only").c_str(), &changed_only_);
        inline_next(button_width("Fit changed geometry"));
        if (cyui::button(cyui::trLabel("Fit changed geometry").c_str()))
            focus_changes();
        inline_next(button_width("Fit collision geometry"));
    }
    if (cyui::button(cyui::trLabel("Fit collision geometry").c_str())) {
        fit_pending_ = true;
        fit_changes_ = false;
    }
    inline_next(cyui::dp(150) + button_width("Camera view"));
    ImGui::SetNextItemWidth(std::min(cyui::dp(140), std::max(cyui::dp(60), ImGui::GetContentRegionAvail().x -
                                                                               button_width("Camera view"))));
    if (ImGui::BeginCombo(cyui::trLabel("Camera view").c_str(), cyui::tr("Choose view").c_str())) {
        unsigned i = 0;
        for (const auto *label : {"Perspective", "Front", "Right", "Top"}) {
            if (ImGui::Selectable(cyui::trLabel(label).c_str()))
                set_camera_view(static_cast<CollisionCameraView>(i));
            ++i;
        }
        ImGui::EndCombo();
    }
    cyui::textDim(
        "%s",
        cyui::tr("Click a wire: inspect shape | Alt + drag: orbit | Middle drag: pan | Wheel: zoom").c_str());
    auto size = ImGui::GetContentRegionAvail();
    if (size.x < cyui::dp(20) || size.y < cyui::dp(50))
        return;
    const bool split = comparing_;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float width = split ? (size.x - gap) * .5f : size.x;
    if (fit_pending_)
        fit(width / std::max(cyui::dp(1), size.y - ImGui::GetTextLineHeightWithSpacing()));
    const auto shown = camera_.camera;
    pane(comparing_ ? "Before" : "Collision geometry",
         !comparing_ && before_.shapes.empty() ? after_ : before_, {width, size.y}, 1, shown);
    if (split) {
        ImGui::SameLine();
        pane("After", after_, {width, size.y}, 2, shown);
    }
    if (!ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(2))
        navigation_pane_ = 0;
}
} // namespace cyuediff
