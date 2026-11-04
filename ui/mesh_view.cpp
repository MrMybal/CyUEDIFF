#include <cyuediff/mesh_view.hpp>
#include "diff_style.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace cyuediff {
void MeshView::fit(float aspect, bool changes) {
    state_.camera.target = changes ? changed_center_ : center_;
    state_.camera.distance =
        (changes ? changed_radius_ : radius_) * 3 / std::max(0.1f, std::min(1.f, aspect));
    fit_pending_ = fit_changes_pending_ = false;
}
void MeshView::set_mesh(std::shared_ptr<const MeshGeometry> mesh, std::shared_ptr<const MeshGeometry> peer,
                        bool after, bool comparing) {
    std::vector<Edge> edges;
    comparing_ = comparing || bool(peer);
    counts_ = {};
    Json current = mesh && peer ? mesh->to_json() : Json{};
    Json remote = mesh && peer ? peer->to_json() : Json{};
    std::set<std::pair<std::uint32_t, std::uint32_t>> peer_edges;
    if (mesh && peer) {
        std::map<std::uint32_t, std::uint32_t> instances;
        for (const auto &i : peer->instances)
            instances[i.id] = i.vertex;
        for (const auto &t : peer->triangles)
            for (unsigned c = 0; c < 3; ++c) {
                auto a = instances.find(t.instances[c]), b = instances.find(t.instances[(c + 1) % 3]);
                if (a == instances.end() || b == instances.end())
                    throw Error("mesh-view", "Invalid peer triangle");
                peer_edges.insert(std::minmax(a->second, b->second));
            }
    }
    auto different = [&](const char *table, std::uint32_t id) {
        auto key = std::to_string(id);
        return !peer || !remote.at(table).contains(key) ||
               current.at(table).at(key) != remote.at(table).at(key);
    };
    bool truncated = false;
    if (mesh) {
        std::map<std::uint32_t, cyui::Vec3> vertices;
        cyui::Vec3 low{}, high{};
        bool first = true;
        for (const auto &v : mesh->vertices) {
            cyui::Vec3 point{v.position[0], v.position[1], v.position[2]};
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) ||
                !vertices.emplace(v.id, point).second)
                throw Error("mesh-view", "Invalid preview vertex");
            if (first) {
                low = high = point;
                first = false;
            }
            low = {std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z)};
            high = {std::max(high.x, point.x), std::max(high.y, point.y), std::max(high.z, point.z)};
        }
        std::map<std::uint32_t, std::uint32_t> instances;
        for (const auto &i : mesh->instances)
            if (!vertices.count(i.vertex) || !instances.emplace(i.id, i.vertex).second)
                throw Error("mesh-view", "Invalid preview instance");
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> unique;
        for (const auto &t : mesh->triangles)
            for (unsigned c = 0; c < 3; ++c) {
                auto a = instances.find(t.instances[c]), b = instances.find(t.instances[(c + 1) % 3]);
                if (a == instances.end() || b == instances.end())
                    throw Error("mesh-view", "Preview triangle references a missing instance");
                auto key = std::minmax(a->second, b->second);
                auto found = unique.find(key);
                if (found == unique.end() && edges.size() >= 100000) {
                    truncated = true;
                    continue;
                }
                ChangeKind change = ChangeKind::unchanged;
                if (comparing_) {
                    if (!peer_edges.count(key))
                        change = after ? ChangeKind::added : ChangeKind::removed;
                    else {
                        bool changed = different("vertices", key.first) ||
                                       different("vertices", key.second) || different("triangles", t.id);
                        for (auto instance : t.instances)
                            changed = changed || different("instances", instance);
                        auto group = std::to_string(t.group);
                        const auto &slots = current.at("materialSlots"), &other = remote.at("materialSlots");
                        changed = changed || slots.contains(group) != other.contains(group) ||
                                  (slots.contains(group) && slots.at(group) != other.at(group));
                        if (changed)
                            change = ChangeKind::modified;
                    }
                }
                if (found == unique.end()) {
                    unique.emplace(key, edges.size());
                    edges.push_back({{vertices.at(key.first), vertices.at(key.second)}, change});
                } else if (change != ChangeKind::unchanged)
                    edges[found->second].change = change;
            }
        center_ = {(low.x + high.x) * .5, (low.y + high.y) * .5, (low.z + high.z) * .5};
        radius_ = std::max(1e-5, std::sqrt((high.x - low.x) * (high.x - low.x) +
                                           (high.y - low.y) * (high.y - low.y) +
                                           (high.z - low.z) * (high.z - low.z)) *
                                     .5);
    }
    for (const auto &edge : edges)
        counts_.add(edge.change);
    cyui::Vec3 low{}, high{};
    bool first_change = true;
    for (const auto &edge : edges)
        if (edge.change != ChangeKind::unchanged)
            for (const auto &p : edge.points) {
                if (first_change) {
                    low = high = p;
                    first_change = false;
                }
                low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
                high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
            }
    changed_center_ = {(low.x + high.x) * .5, (low.y + high.y) * .5, (low.z + high.z) * .5};
    changed_radius_ =
        std::max(1e-5, std::hypot(std::hypot(high.x - low.x, high.y - low.y), high.z - low.z) * .5);
    changed_only_ = fit_changes_pending_ = false;
    rendered_edges_ = 0;
    mesh_ = std::move(mesh);
    edges_ = std::move(edges);
    truncated_ = truncated;
    state_ = {};
    state_.camera.up = {0, 0, 1};
    state_.camera.yaw = .7;
    state_.camera.pitch = .35;
    fit_pending_ = true;
}
void MeshView::set_changed_only(bool value) {
    changed_only_ = comparing_ && value;
}
std::size_t MeshView::visible_edge_count() const {
    return comparing_ && changed_only_ ? counts_.changed() : edges_.size();
}
bool MeshView::focus_changes() {
    if (!mesh_ || !comparing_ || !counts_.changed())
        return false;
    fit_changes_pending_ = true;
    return true;
}
bool MeshView::set_camera_view(MeshCameraView view) {
    if (!mesh_)
        return false;
    if (fit_pending_)
        fit(1, fit_changes_pending_);
    switch (view) {
    case MeshCameraView::perspective:
        state_.camera.yaw = .7;
        state_.camera.pitch = .35;
        break;
    case MeshCameraView::front:
        state_.camera.yaw = 0;
        state_.camera.pitch = 0;
        break;
    case MeshCameraView::right:
        state_.camera.yaw = 1.5707963267948966;
        state_.camera.pitch = 0;
        break;
    case MeshCameraView::top:
        state_.camera.yaw = 0;
        state_.camera.pitch = 1.55;
        break;
    default:
        return false;
    }
    state_.camera.up = {0, 0, 1};
    return true;
}
Json MeshView::view_state() const {
    Json result = changed_only_ ? Json{{"changedOnly", true}} : Json::object();
    if (fit_pending_ || !mesh_)
        return result;
    const auto &c = state_.camera;
    result.update({{"target", {c.target.x, c.target.y, c.target.z}},
                   {"up", {c.up.x, c.up.y, c.up.z}},
                   {"yaw", c.yaw},
                   {"pitch", c.pitch},
                   {"distance", c.distance}});
    return result;
}
void MeshView::restore_view_state(const Json &state) {
    cyuediff::view_state::validate(state, "mesh");
    if (!mesh_ || state.empty())
        return;
    if (state.contains("changedOnly"))
        changed_only_ = comparing_ && state["changedOnly"].get<bool>();
    if (state.size() == 1 && state.contains("changedOnly"))
        return;
    if (fit_pending_)
        fit(1);
    fit_changes_pending_ = false;
    auto &c = state_.camera;
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
    state_.orbiting = state_.panning = false;
}
void MeshView::draw(const char *id) {
    cyui::ScopedID scope(id);
    rendered_edges_ = 0;
    if (!mesh_) {
        cyui::emptyState(cyui::tr("Mesh preview unavailable").c_str(),
                         cyui::tr("Load a supported source mesh in the application.").c_str(),
                         cyui::icon::document);
        return;
    }
    cyui::textDim(cyui::tr("%zu vertices / %zu triangles / %zu material groups").c_str(),
                  mesh_->vertices.size(), mesh_->triangles.size(), mesh_->material_slots.size());
    if (comparing_) {
        detail::change_legend(counts_);
        ImGui::Checkbox(cyui::trLabel("Changed edges only").c_str(), &changed_only_);
        ImGui::SameLine();
        cyui::ScopedDisabled disabled(!counts_.changed());
        if (cyui::button(cyui::trLabel("Fit changed geometry").c_str()))
            focus_changes();
    }
    if (ImGui::Button(cyui::trLabel("Fit mesh").c_str()))
        fit_pending_ = true;
    ImGui::SameLine();
    if (ImGui::BeginCombo(cyui::trLabel("Camera view").c_str(), cyui::tr("Choose view").c_str())) {
        unsigned index = 0;
        for (const auto *label : {"Perspective", "Front", "Right", "Top"}) {
            if (ImGui::Selectable(cyui::trLabel(label).c_str()))
                set_camera_view(static_cast<MeshCameraView>(index));
            ++index;
        }
        ImGui::EndCombo();
    }
    cyui::textDim("%s", cyui::tr("Alt + drag: orbit | Middle drag: pan | Wheel: zoom").c_str());
    if (truncated_)
        cyui::callout(cyui::Tone::warning, cyui::tr("Preview is limited to 100,000 edges.").c_str());
    if (changed_only_ && !counts_.changed()) {
        cyui::emptyState(cyui::tr("No changed edges").c_str(),
                         cyui::tr("Show unchanged edges to inspect the complete mesh.").c_str(),
                         cyui::icon::filter);
        return;
    }
    auto size = ImGui::GetContentRegionAvail();
    if (size.x <= 0 || size.y <= 0)
        return;
    auto origin = ImGui::GetCursorScreenPos();
    if (fit_pending_ || fit_changes_pending_)
        fit(size.x / size.y, fit_changes_pending_);
    cyui::viewport("##mesh", state_, {}, size);
    auto view = cyui::cameraViewMatrix(state_.camera);
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    auto project = [&](cyui::Vec3 p, ImVec2 &screen) {
        double x = view[0] * p.x + view[4] * p.y + view[8] * p.z + view[12],
               y = view[1] * p.x + view[5] * p.y + view[9] * p.z + view[13],
               z = view[2] * p.x + view[6] * p.y + view[10] * p.z + view[14];
        if (z >= -std::max(1e-12, state_.camera.distance * 1e-6))
            return false;
        auto sx = x / (-z * .41421356237) * (size.y / size.x), sy = y / (-z * .41421356237);
        if (!std::isfinite(sx) || !std::isfinite(sy) || std::abs(sx) > 1e5 || std::abs(sy) > 1e5)
            return false;
        screen = {origin.x + float((sx + 1) * .5 * size.x), origin.y + float((1 - sy) * .5 * size.y)};
        return true;
    };
    for (const auto &edge : edges_) {
        if (changed_only_ && edge.change == ChangeKind::unchanged)
            continue;
        ImVec2 a, b;
        if (project(edge.points[0], a) && project(edge.points[1], b)) {
            draw->AddLine(a, b, comparing_ ? detail::change_color(edge.change) : cyui::palette().accent,
                          cyui::dp(edge.change == ChangeKind::unchanged ? 1.f : 2.f));
            ++rendered_edges_;
        }
    }
    draw->PopClipRect();
}
} // namespace cyuediff
