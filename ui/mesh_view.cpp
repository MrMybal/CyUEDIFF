#include <cyuediff/mesh_view.hpp>
#include <algorithm>
#include <cmath>
#include <set>
namespace cyuediff {
void MeshView::fit(float aspect) {
    state_.camera.target = center_;
    state_.camera.distance = radius_ * 3 / std::max(0.1f, std::min(1.f, aspect));
    state_.camera.up = {0, 0, 1};
    state_.camera.yaw = .7;
    state_.camera.pitch = .35;
    fit_pending_ = false;
}
void MeshView::set_mesh(std::shared_ptr<const MeshGeometry> mesh) {
    std::vector<std::array<cyui::Vec3, 2>> edges;
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
        std::set<std::pair<std::uint32_t, std::uint32_t>> unique;
        for (const auto &t : mesh->triangles)
            for (unsigned c = 0; c < 3; ++c) {
                auto a = instances.find(t.instances[c]), b = instances.find(t.instances[(c + 1) % 3]);
                if (a == instances.end() || b == instances.end())
                    throw Error("mesh-view", "Preview triangle references a missing instance");
                auto key = std::minmax(a->second, b->second);
                if (edges.size() >= 100000) {
                    truncated = true;
                    continue;
                }
                if (unique.emplace(key.first, key.second).second)
                    edges.push_back({vertices.at(key.first), vertices.at(key.second)});
            }
        center_ = {(low.x + high.x) * .5, (low.y + high.y) * .5, (low.z + high.z) * .5};
        radius_ = std::max(1e-5, std::sqrt((high.x - low.x) * (high.x - low.x) +
                                           (high.y - low.y) * (high.y - low.y) +
                                           (high.z - low.z) * (high.z - low.z)) *
                                     .5);
    }
    rendered_edges_ = 0;
    mesh_ = std::move(mesh);
    edges_ = std::move(edges);
    truncated_ = truncated;
    state_ = {};
    fit_pending_ = true;
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
    if (ImGui::Button(cyui::trLabel("Fit mesh").c_str()))
        fit_pending_ = true;
    ImGui::SameLine();
    cyui::textDim("%s", cyui::tr("Alt + drag: orbit | Middle drag: pan | Wheel: zoom").c_str());
    if (truncated_)
        cyui::callout(cyui::Tone::warning, cyui::tr("Preview is limited to 100,000 edges.").c_str());
    auto size = ImGui::GetContentRegionAvail();
    if (size.x <= 0 || size.y <= 0)
        return;
    auto origin = ImGui::GetCursorScreenPos();
    if (fit_pending_)
        fit(size.x / size.y);
    cyui::viewport("##mesh", state_, {}, size);
    auto view = cyui::cameraViewMatrix(state_.camera);
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    auto project = [&](cyui::Vec3 p, ImVec2 &screen) {
        double x = view[0] * p.x + view[4] * p.y + view[8] * p.z + view[12],
               y = view[1] * p.x + view[5] * p.y + view[9] * p.z + view[13],
               z = view[2] * p.x + view[6] * p.y + view[10] * p.z + view[14];
        if (z >= -radius_ * 1e-4)
            return false;
        auto sx = x / (-z * .41421356237) * (size.y / size.x), sy = y / (-z * .41421356237);
        if (!std::isfinite(sx) || !std::isfinite(sy) || std::abs(sx) > 1e5 || std::abs(sy) > 1e5)
            return false;
        screen = {origin.x + float((sx + 1) * .5 * size.x), origin.y + float((1 - sy) * .5 * size.y)};
        return true;
    };
    for (const auto &edge : edges_) {
        ImVec2 a, b;
        if (project(edge[0], a) && project(edge[1], b)) {
            draw->AddLine(a, b, cyui::palette().accent, cyui::dp(1));
            ++rendered_edges_;
        }
    }
    draw->PopClipRect();
}
} // namespace cyuediff
