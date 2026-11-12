#include <cyuediff/nanite_view.hpp>
#include "diff_style.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
namespace cyuediff {
namespace {
using Ref = std::array<std::uint32_t, 3>;
Ref key(const NaniteTriangleReference &r) {
    return {r.page, r.cluster, r.triangle};
}
struct Selection {
    std::uint32_t page = 0, cluster = 0, vertex = 0, triangle = 0, node = 0;
    int inspector = 1;
    bool comparing = false, after = false, cluster_preview = false, attributes = true;
};
struct Row {
    std::uint32_t page = 0, cluster = 0, vertices = 0, triangles = 0, changes = 0;
    bool leaf = false, root = false;
    std::string label;
};
struct Edge {
    std::array<cyui::Vec3, 2> points;
    Ref triangle;
    int state = 0;
};
struct Scene {
    std::vector<Edge> edges;
    std::vector<Row> rows;
    cyui::Vec3 center{}, changed_center{};
    double radius = 1, changed_radius = 1;
    std::uint64_t triangles = 0, clusters = 0;
    bool capped = false, rows_capped = false, known = false;
};
struct Comparison {
    NaniteSurfaceComparison difference;
    std::map<Ref, std::pair<int, std::size_t>> marks[2];
};
const char *state_label(int s) {
    return s == 1 ? "Added" : s == 2 ? "Removed" : s == 3 ? "Modified" : s == 4 ? "Unavailable" : "Unchanged";
}
ChangeKind kind(int s) {
    return s == 1   ? ChangeKind::added
           : s == 2 ? ChangeKind::removed
           : s == 3 ? ChangeKind::modified
                    : ChangeKind::unchanged;
}
Json unavailable(const char *reason) {
    return {{"ready", false}, {"unavailable", reason}};
}
struct Bounds {
    bool first = true;
    cyui::Vec3 low{}, high{};
    void add(const std::array<float, 3> &p) {
        cyui::Vec3 v{p[0], p[1], p[2]};
        if (first) {
            low = high = v;
            first = false;
        }
        low = {std::min(low.x, v.x), std::min(low.y, v.y), std::min(low.z, v.z)};
        high = {std::max(high.x, v.x), std::max(high.y, v.y), std::max(high.z, v.z)};
    }
    void finish(cyui::Vec3 &center, double &radius) const {
        if (first)
            return;
        center = {(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2};
        radius = std::max(1e-12, std::sqrt((high.x - low.x) * (high.x - low.x) +
                                           (high.y - low.y) * (high.y - low.y) +
                                           (high.z - low.z) * (high.z - low.z)) /
                                     2);
    }
};
} // namespace
struct NaniteView::Review {
    Selection selection;
    std::shared_ptr<const Scene> scene;
    std::shared_ptr<const Comparison> comparison;
    Json coverage[2], primary = nullptr, peer = nullptr;
    bool paired = false;
    std::string notice;
};
struct NaniteView::Worker {
    struct Work {
        std::uint64_t id;
        std::shared_ptr<const CookedNaniteResource> models[2];
        Selection selection;
        std::shared_ptr<std::atomic<bool>> cancel;
    };
    struct Result {
        std::uint64_t id = 0;
        std::shared_ptr<const Review> value;
        std::string error;
    };
    std::mutex mutex;
    std::condition_variable wake;
    std::optional<Work> pending;
    std::optional<Result> result;
    std::shared_ptr<std::atomic<bool>> cancel;
    bool stopping = false;
    std::thread thread;
    std::shared_ptr<const CookedNaniteResource> cached_models[2];
    bool cached_attributes = false, cached_comparing = false;
    std::shared_ptr<const Comparison> comparison;
    std::shared_ptr<const Scene> scene;
    Selection scene_selection;
    // Started once every member exists: run() reads the cache members declared after `thread`.
    Worker() { thread = std::thread([this] { run(); }); }
    ~Worker() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            if (cancel)
                cancel->store(true);
            pending.reset();
        }
        wake.notify_one();
        thread.join();
    }
    void submit(Work w) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (cancel)
                cancel->store(true);
            cancel = w.cancel;
            pending = std::move(w);
            result.reset();
        }
        wake.notify_one();
    }
    std::optional<Result> take() {
        std::lock_guard<std::mutex> lock(mutex);
        auto r = std::move(result);
        result.reset();
        return r;
    }
    std::shared_ptr<const Comparison> compare(const Work &w, const OperationControl &control) {
        auto out = std::make_shared<Comparison>();
        if (w.selection.comparing && w.models[0] && w.models[1]) {
            try {
                NaniteSurfaceDiffOptions o;
                o.attributes = w.selection.attributes;
                o.max_output_bytes = 64ull * 1024 * 1024;
                o.control = control;
                out->difference = compare_nanite_surfaces(*w.models[0], *w.models[1], o);
                if (out->difference.removed.size() + out->difference.added.size() +
                        2 * out->difference.modified.size() >
                    500000)
                    throw Error("ui-nanite-limit", "Nanite change references exceed review limit");
                for (const auto &r : out->difference.removed) {
                    control.check();
                    out->marks[0][key(r)] = {2, SIZE_MAX};
                }
                for (const auto &r : out->difference.added) {
                    control.check();
                    out->marks[1][key(r)] = {1, SIZE_MAX};
                }
                for (std::size_t i = 0; i < out->difference.modified.size(); ++i) {
                    control.check();
                    const auto &r = out->difference.modified[i];
                    out->marks[0][key(r.before)] = {3, i};
                    out->marks[1][key(r.after)] = {3, i};
                }
            } catch (const Error &e) {
                if (e.code == "cancelled")
                    throw;
                out = std::make_shared<Comparison>();
                out->difference.summary = {{"complete", false},
                                           {"geometryComplete", false},
                                           {"status", "incomplete"},
                                           {"unavailable", e.code + ": " + e.what()}};
            }
        } else
            out->difference.summary = {{"complete", false},
                                       {"geometryComplete", false},
                                       {"status", w.selection.comparing ? "incomplete" : "inspection"}};
        return out;
    }
    std::shared_ptr<const Scene> make_scene(const Work &w, const OperationControl &control) {
        auto out = std::make_shared<Scene>();
        const unsigned side = w.selection.after ? 1 : 0;
        const auto resource = w.models[side];
        if (!resource)
            return out;
        auto model = resource->geometry();
        out->known = resource->geometry_known();
        if (!model)
            return out;
        const auto roots = resource->information(control).value("rootPages", 0u);
        const auto &summary = comparison->difference.summary;
        const bool classified = summary.value("geometryComplete", false) &&
                                (!w.selection.attributes || summary.value("attributeComplete", false));
        Bounds bounds, changed;
        for (std::size_t p = 0; p < model->page_count(); ++p)
            for (std::size_t c = 0; c < model->cluster_count(p); ++c) {
                control.check();
                auto m = model->cluster_metadata(p, c, control);
                const bool leaf = m.at("fullLeaf");
                const auto triangles = m.at("triangles").get<std::uint32_t>();
                std::uint32_t changes = 0;
                for (std::uint32_t t = 0; t < triangles; ++t)
                    if (comparison->marks[side].count(
                            {static_cast<std::uint32_t>(p), static_cast<std::uint32_t>(c), t}))
                        ++changes;
                ++out->clusters;
                if (out->rows.size() < 100000)
                    out->rows.push_back({static_cast<std::uint32_t>(p), static_cast<std::uint32_t>(c),
                                         m.at("vertices").get<std::uint32_t>(), triangles, changes, leaf,
                                         p < roots, std::to_string(p) + " / " + std::to_string(c)});
                else
                    out->rows_capped = true;
                if (w.selection.cluster_preview ? (p != w.selection.page || c != w.selection.cluster) : !leaf)
                    continue;
                for (std::uint32_t t = 0; t < triangles; ++t) {
                    control.check();
                    ++out->triangles;
                    Ref ref{static_cast<std::uint32_t>(p), static_cast<std::uint32_t>(c), t};
                    int state = w.selection.comparing && (!classified || !leaf) ? 4 : 0;
                    auto found = comparison->marks[side].find(ref);
                    if (found != comparison->marks[side].end())
                        state = found->second.first;
                    auto tri = model->triangle(p, c, t, control);
                    std::array<std::array<float, 3>, 3> points;
                    for (unsigned v = 0; v < 3; ++v) {
                        points[v] = model->position(p, c, tri.vertices[v], control);
                        bounds.add(points[v]);
                        if (state >= 1 && state <= 3)
                            changed.add(points[v]);
                    }
                    for (unsigned v = 0; v < 3; ++v)
                        if (out->edges.size() < 200000) {
                            const auto &a = points[v], &b = points[(v + 1) % 3];
                            Edge edge;
                            edge.points[0] = {a[0], a[1], a[2]};
                            edge.points[1] = {b[0], b[1], b[2]};
                            edge.triangle = ref;
                            edge.state = state;
                            out->edges.push_back(edge);
                        } else
                            out->capped = true;
                }
            }
        bounds.finish(out->center, out->radius);
        changed.finish(out->changed_center, out->changed_radius);
        return out;
    }
    Json triangle(const std::shared_ptr<const CookedNaniteResource> &r, const NaniteTriangleReference &ref,
                  unsigned rotation, const OperationControl &control) {
        auto m = r ? r->geometry() : nullptr;
        if (!m || ref.page >= m->page_count() || ref.cluster >= m->cluster_count(ref.page))
            return unavailable("triangle-selection");
        auto meta = m->cluster_metadata(ref.page, ref.cluster, control);
        if (ref.triangle >= meta.at("triangles").get<std::size_t>())
            return unavailable("triangle-selection");
        auto tri = m->triangle(ref.page, ref.cluster, ref.triangle, control);
        Json out = {{"material", tri.material}, {"corners", Json::array()}};
        for (unsigned i = 0; i < 3; ++i) {
            const auto vertex = tri.vertices[(i + rotation) % 3];
            Json corner = {{"position", m->position(ref.page, ref.cluster, vertex, control)}};
            corner["attributes"] = m->attributes_decoded()
                                       ? m->attributes(ref.page, ref.cluster, vertex, control).to_json()
                                       : unavailable("attributes-not-requested");
            out["corners"].push_back(std::move(corner));
        }
        return out;
    }
    std::shared_ptr<const Review> prepare(const Work &w) {
        OperationControl control;
        control.cancelled = [flag = w.cancel] { return flag->load(); };
        control.check();
        if (cached_models[0] != w.models[0] || cached_models[1] != w.models[1] ||
            cached_attributes != w.selection.attributes || cached_comparing != w.selection.comparing) {
            auto fresh = compare(w, control);
            control.check();
            comparison = std::move(fresh);
            cached_models[0] = w.models[0];
            cached_models[1] = w.models[1];
            cached_attributes = w.selection.attributes;
            cached_comparing = w.selection.comparing;
            scene.reset();
        }
        const auto &s = w.selection;
        if (!scene || scene_selection.after != s.after ||
            scene_selection.cluster_preview != s.cluster_preview ||
            (s.cluster_preview && (scene_selection.page != s.page || scene_selection.cluster != s.cluster))) {
            auto fresh = make_scene(w, control);
            control.check();
            scene = std::move(fresh);
            scene_selection = s;
        }
        auto out = std::make_shared<Review>();
        out->scene = scene;
        out->comparison = comparison;
        out->selection = s;
        for (unsigned side = 0; side < 2; ++side)
            out->coverage[side] =
                w.models[side] ? w.models[side]->information(control) : unavailable("resource-unavailable");
        const unsigned side = s.after ? 1 : 0;
        const auto r = w.models[side];
        if (!r) {
            out->primary = unavailable("resource-unavailable");
            return out;
        }
        auto model = r->geometry();
        const bool valid_cluster =
            model && s.page < model->page_count() && s.cluster < model->cluster_count(s.page);
        if (s.inspector == 0) {
            out->primary = {{"resource", out->coverage[side]},
                            {"page", s.page < r->page_count() ? r->page_metadata(s.page, control)
                                                              : unavailable("page-selection")},
                            {"cluster", valid_cluster ? model->cluster_metadata(s.page, s.cluster, control)
                                                      : unavailable("cluster-selection")}};
        } else if (s.inspector == 3)
            out->primary = s.node < r->hierarchy_node_count() ? r->hierarchy_node(s.node, control)
                                                              : unavailable("hierarchy-selection");
        else if (s.inspector == 1) {
            if (valid_cluster &&
                s.vertex <
                    model->cluster_metadata(s.page, s.cluster, control).at("vertices").get<std::uint32_t>())
                out->primary = {
                    {"position", model->position(s.page, s.cluster, s.vertex, control)},
                    {"quantizedPosition", model->quantized_position(s.page, s.cluster, s.vertex, control)},
                    {"attributes", model->attributes_decoded()
                                       ? model->attributes(s.page, s.cluster, s.vertex, control).to_json()
                                       : unavailable("attributes-not-requested")}};
            else
                out->primary = unavailable("vertex-selection");
        } else {
            NaniteTriangleReference ref{s.page, s.cluster, s.triangle};
            out->primary = triangle(r, ref, 0, control);
            const auto mark = comparison->marks[side].find(key(ref));
            if (mark != comparison->marks[side].end()) {
                if (mark->second.first == 3) {
                    const auto &change = comparison->difference.modified[mark->second.second];
                    out->primary =
                        triangle(r, ref, side ? change.after_rotation : change.before_rotation, control);
                    out->peer = triangle(w.models[1 - side], side ? change.before : change.after,
                                         side ? change.before_rotation : change.after_rotation, control);
                    out->paired = true;
                    if (change.ambiguous)
                        out->notice =
                            "Overlapping triangle identity is ambiguous; corner comparison is deterministic.";
                } else
                    out->paired = true;
            } else if (w.models[1 - side] && r->encoding_sha256() != "" &&
                       r->encoding_sha256() == w.models[1 - side]->encoding_sha256()) {
                out->peer = triangle(w.models[1 - side], ref, 0, control);
                out->paired = true;
            }
            if (w.selection.comparing && !out->paired)
                out->notice = "Select a reported change to inspect aligned before/after corners.";
        }
        if (out->primary.is_object() && !out->primary.value("ready", true) && out->notice.empty())
            out->notice = "Selection unavailable in this resource; choose a valid page, cluster or index.";
        control.check();
        return out;
    }
    void run() {
        for (;;) {
            std::optional<Work> w;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [this] { return stopping || pending.has_value(); });
                if (stopping)
                    return;
                w = std::move(pending);
                pending.reset();
            }
            Result r;
            r.id = w->id;
            try {
                r.value = prepare(*w);
            } catch (const Error &e) {
                if (e.code == "cancelled")
                    continue;
                r.error = e.code + ": " + e.what();
            } catch (const std::exception &e) {
                r.error = e.what();
            } catch (...) {
                r.error = "Unexpected error";
            }
            std::lock_guard<std::mutex> lock(mutex);
            if (!stopping && !w->cancel->load())
                result = std::move(r);
        }
    }
};

NaniteView::NaniteView() {
    camera_.camera.up = {0, 0, 1};
}
NaniteView::~NaniteView() = default;
NaniteView::NaniteView(NaniteView &&) noexcept = default;
NaniteView &NaniteView::operator=(NaniteView &&) noexcept = default;
void NaniteView::request() {
    ++generation_;
    error_.clear();
    review_.reset();
    visible_.clear();
    if (!models_[0] && !models_[1]) {
        worker_.reset();
        busy_ = false;
        return;
    }
    if (!worker_)
        worker_ = std::make_unique<Worker>();
    busy_ = true;
    Selection s{page_,      cluster_,   vertex_, triangle_,        node_,
                inspector_, comparing_, after_,  cluster_preview_, attributes_};
    worker_->submit({generation_, {models_[0], models_[1]}, s, std::make_shared<std::atomic<bool>>(false)});
}
void NaniteView::set_resources(std::shared_ptr<const CookedNaniteResource> a,
                               std::shared_ptr<const CookedNaniteResource> b, bool compare) {
    models_[0] = std::move(a);
    models_[1] = std::move(b);
    comparing_ = compare || bool(models_[1]);
    after_ = !models_[0] && bool(models_[1]);
    page_ = cluster_ = vertex_ = triangle_ = node_ = 0;
    fit_ = true;
    request();
}
bool NaniteView::poll() {
    if (!worker_)
        return false;
    auto r = worker_->take();
    if (!r || r->id != generation_)
        return false;
    busy_ = false;
    error_ = r->error;
    if (r->value) {
        review_ = std::move(r->value);
        rebuild();
        bind_details();
    }
    return true;
}
void NaniteView::rebuild() {
    visible_.clear();
    if (!review_)
        return;
    for (std::size_t i = 0; i < review_->scene->rows.size(); ++i) {
        const auto &r = review_->scene->rows[i];
        if ((!comparing_ || !changed_only_ || r.changes) &&
            filter_.matches(r.label, std::string(r.leaf ? "full leaf" : "coarse") + " " +
                                         (r.root ? "root" : "stream")))
            visible_.push_back(i);
    }
}
void NaniteView::bind_details() {
    if (!review_)
        return;
    const auto saved = details_state_.is_object() ? details_state_ : details_.view_state();
    const auto &a = review_->primary, &b = review_->peer;
    if (review_->paired) {
        auto before = after_ ? b : a;
        auto after = after_ ? a : b;
        // Unknown decoded channels cannot establish a property addition/removal. Keep raw
        // query results in report(), and mask only the paired presentation copies.
        if (before.contains("corners") && after.contains("corners"))
            for (std::size_t i = 0; i < std::min(before["corners"].size(), after["corners"].size()); ++i) {
                auto &left = before["corners"][i]["attributes"];
                auto &right = after["corners"][i]["attributes"];
                if (!left.value("ready", true) || !right.value("ready", true))
                    left = right = unavailable("paired-attributes-unavailable");
                else {
                    if (left["normal"].is_null() || right["normal"].is_null()) {
                        left["normal"] = right["normal"] = unavailable("paired-normal-unavailable");
                        left["normalStatus"] = right["normalStatus"] = "unavailable";
                    }
                    if (left.value("tangentStatus", "") == "normal-unavailable" ||
                        right.value("tangentStatus", "") == "normal-unavailable") {
                        left["tangent"] = right["tangent"] = unavailable("paired-tangent-unavailable");
                        left["tangentStatus"] = right["tangentStatus"] = "unavailable";
                    }
                }
            }
        details_.set_values(before.is_null() ? nullptr : &before, after.is_null() ? nullptr : &after, true);
    } else
        details_.set_values(&a, nullptr, false);
    details_.restore_view_state(saved);
    details_state_ = nullptr;
}
void NaniteView::set_after(bool value) {
    if (after_ == value)
        return;
    after_ = value;
    fit_ = true;
    request();
}
void NaniteView::set_cluster_preview(bool value) {
    if (cluster_preview_ == value)
        return;
    cluster_preview_ = value;
    fit_ = true;
    request();
}
void NaniteView::set_compare_attributes(bool value) {
    if (attributes_ == value)
        return;
    attributes_ = value;
    request();
}
void NaniteView::select_cluster(std::uint32_t p, std::uint32_t c) {
    if (p > 16383 || c > 255)
        throw Error("view-state", "Invalid Nanite cluster selection");
    std::uint32_t first = 0;
    if (review_) {
        auto it = review_->comparison->marks[after_ ? 1 : 0].lower_bound({p, c, 0});
        if (it != review_->comparison->marks[after_ ? 1 : 0].end() && it->first[0] == p && it->first[1] == c)
            first = it->first[2];
    }
    page_ = p;
    cluster_ = c;
    vertex_ = 0;
    triangle_ = first;
    scroll_ = true;
    if (cluster_preview_)
        fit_ = true;
    request();
}
void NaniteView::select_vertex(std::uint32_t v) {
    if (v > 255)
        throw Error("view-state", "Invalid Nanite vertex selection");
    vertex_ = v;
    request();
}
void NaniteView::select_triangle(std::uint32_t t) {
    if (t > 127)
        throw Error("view-state", "Invalid Nanite triangle selection");
    triangle_ = t;
    request();
}
void NaniteView::select_node(std::uint32_t n) {
    if (n > 65535)
        throw Error("view-state", "Invalid Nanite hierarchy selection");
    node_ = n;
    request();
}
void NaniteView::set_inspector(int mode) {
    if (mode < 0 || mode > 3)
        throw Error("view-state", "Invalid Nanite inspector");
    inspector_ = mode;
    request();
}
void NaniteView::set_filter(std::string value) {
    view_state::text(value);
    filter_.query = std::move(value);
    rebuild();
}
void NaniteView::set_changed_only(bool value) {
    changed_only_ = value;
    rebuild();
}
bool NaniteView::next_change(bool previous) {
    if (!review_ || visible_.empty())
        return false;
    auto it = std::find_if(visible_.begin(), visible_.end(), [&](auto i) {
        const auto &r = review_->scene->rows[i];
        return r.page == page_ && r.cluster == cluster_;
    });
    const auto start =
        it == visible_.end() ? (previous ? 0 : visible_.size() - 1) : std::size_t(it - visible_.begin());
    for (std::size_t n = 1; n <= visible_.size(); ++n) {
        auto at = previous ? (start + visible_.size() - n) % visible_.size() : (start + n) % visible_.size();
        auto r = review_->scene->rows[visible_[at]];
        if (r.changes) {
            inspector_ = 2;
            select_cluster(r.page, r.cluster);
            return true;
        }
    }
    return false;
}
Json NaniteView::view_state() const {
    Json camera = Json::object();
    if (!fit_) {
        const auto &c = camera_.camera;
        camera = {{"target", {c.target.x, c.target.y, c.target.z}},
                  {"up", {c.up.x, c.up.y, c.up.z}},
                  {"yaw", c.yaw},
                  {"pitch", c.pitch},
                  {"distance", c.distance}};
    }
    return {{"page", page_},
            {"cluster", cluster_},
            {"vertex", vertex_},
            {"triangle", triangle_},
            {"node", node_},
            {"inspector", inspector_},
            {"after", after_},
            {"clusterPreview", cluster_preview_},
            {"attributes", attributes_},
            {"changedOnly", changed_only_},
            {"filter", filter_.query},
            {"camera", camera},
            {"layout", layout_.snapshot()},
            {"compactSurface", compact_surface_},
            {"properties", details_state_.is_object() ? details_state_ : details_.view_state()}};
}
void NaniteView::restore_view_state(const Json &j) {
    view_state::validate(j, "nanite");
    page_ = j.value("page", page_);
    cluster_ = j.value("cluster", cluster_);
    vertex_ = j.value("vertex", vertex_);
    triangle_ = j.value("triangle", triangle_);
    node_ = j.value("node", node_);
    inspector_ = j.value("inspector", inspector_);
    after_ = j.value("after", after_);
    cluster_preview_ = j.value("clusterPreview", cluster_preview_);
    attributes_ = j.value("attributes", attributes_);
    changed_only_ = j.value("changedOnly", changed_only_);
    filter_.query = j.value("filter", filter_.query);
    if (j.contains("properties"))
        details_state_ = j.at("properties");
    if (j.contains("camera") && !j.at("camera").empty()) {
        const auto &x = j.at("camera");
        auto &c = camera_.camera;
        auto vector = [&](const char *key, cyui::Vec3 &out) {
            if (x.contains(key))
                out = {x[key][0].get<double>(), x[key][1].get<double>(), x[key][2].get<double>()};
        };
        vector("target", c.target);
        vector("up", c.up);
        c.yaw = x.value("yaw", c.yaw);
        c.pitch = x.value("pitch", c.pitch);
        c.distance = x.value("distance", c.distance);
        camera_.orbiting = camera_.panning = false;
        fit_ = false;
    }
    if (j.contains("layout"))
        layout_.restore(j.at("layout"), "nanite-layout");
    compact_surface_ = j.value("compactSurface", compact_surface_);
    request();
    scroll_ = true;
}
Json NaniteView::report() const {
    Json out = {{"schema", "cyuediff.nanite-view/1"},
                {"busy", busy_},
                {"error", error_},
                {"state", view_state()},
                {"visibleClusters", visible_.size()},
                {"renderedRows", rendered_rows_},
                {"renderedEdges", rendered_edges_},
                {"selectedVisible", selected_visible_},
                {"frameDisplayCapped", frame_capped_}};
    if (review_) {
        out["comparison"] = review_->comparison->difference.summary;
        out["coverage"] = Json::array({review_->coverage[0], review_->coverage[1]});
        out["details"] = review_->primary;
        out["peerDetails"] = review_->peer;
        ChangeCounts rows;
        for (const auto &row : details_.table().rows)
            rows.add(row.change);
        out["inspectorRows"] = {{"unchanged", rows.unchanged},
                                {"added", rows.added},
                                {"removed", rows.removed},
                                {"modified", rows.modified}};
        out["paired"] = review_->paired;
        out["notice"] = review_->notice;
        out["clusters"] = review_->scene->clusters;
        out["sceneTriangles"] = review_->scene->triangles;
        out["preparedEdges"] = review_->scene->edges.size();
        out["sceneCapped"] = review_->scene->capped;
        out["rowsCapped"] = review_->scene->rows_capped;
        out["geometryKnown"] = review_->scene->known;
    }
    return out;
}
void NaniteView::viewport() {
    if (!review_)
        return;
    const auto &s = *review_->scene;
    if (ImGui::Button(cyui::trLabel("Fit Nanite surface").c_str()))
        fit_ = true;
    if (!s.known) {
        cyui::callout(cyui::Tone::warning,
                      cyui::tr("Nanite geometry is unavailable; absence is not established.").c_str());
        return;
    }
    if (s.triangles == 0) {
        cyui::emptyState(cyui::tr("No stored triangles in this selection").c_str(),
                         cyui::tr("An empty resource or invalid selection has no wire preview.").c_str());
        return;
    }
    cyui::textDim(cyui::tr("%llu triangles / %zu prepared edges").c_str(),
                  static_cast<unsigned long long>(s.triangles), s.edges.size());
    const std::size_t frame_limit =
        (ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasVtxOffset) ? 200000 : 4000;
    frame_capped_ = s.capped || s.edges.size() > frame_limit;
    if (frame_capped_)
        cyui::callout(
            cyui::Tone::warning,
            cyui::tr("Viewport edge limit reached; comparison counts retain their full scope.").c_str());
    auto size = ImGui::GetContentRegionAvail();
    if (size.x <= 0 || size.y <= 0)
        return;
    const auto origin = ImGui::GetCursorScreenPos();
    if (fit_) {
        camera_.camera.target = s.center;
        camera_.camera.distance =
            std::clamp(s.radius * 3 / std::max(.1f, std::min(1.f, size.x / size.y)), 1e-9, 1e12);
        fit_ = false;
    }
    cyui::viewport("##nanite-wire", camera_, {}, size);
    auto matrix = cyui::cameraViewMatrix(camera_.camera);
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    auto project = [&](cyui::Vec3 p, ImVec2 &screen) {
        const double x = matrix[0] * p.x + matrix[4] * p.y + matrix[8] * p.z + matrix[12],
                     y = matrix[1] * p.x + matrix[5] * p.y + matrix[9] * p.z + matrix[13],
                     z = matrix[2] * p.x + matrix[6] * p.y + matrix[10] * p.z + matrix[14];
        if (z >= -std::max(1e-12, camera_.camera.distance * 1e-6))
            return false;
        const double a = x / (-z * .41421356237) * (size.y / size.x), b = y / (-z * .41421356237);
        if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a) > 1e5 || std::abs(b) > 1e5)
            return false;
        screen = {origin.x + float((a + 1) * .5 * size.x), origin.y + float((1 - b) * .5 * size.y)};
        return true;
    };
    std::size_t considered = 0;
    for (const auto &edge : s.edges) {
        if (changed_only_ && comparing_ && (edge.state == 0 || edge.state == 4))
            continue;
        if (considered++ >= frame_limit) {
            frame_capped_ = true;
            break;
        }
        ImVec2 a, b;
        if (project(edge.points[0], a) && project(edge.points[1], b)) {
            const bool selected = edge.triangle == Ref{page_, cluster_, triangle_};
            auto color = edge.state == 4 ? cyui::palette().textDim
                         : comparing_    ? detail::change_color(kind(edge.state))
                                         : cyui::palette().accent;
            if (selected)
                color = cyui::palette().warning;
            draw->AddLine(a, b, color,
                          cyui::dp(selected                             ? 2.5f
                                   : edge.state >= 1 && edge.state <= 3 ? 1.8f
                                                                        : 1.f));
            ++rendered_edges_;
        }
    }
    draw->PopClipRect();
}
void NaniteView::draw(const char *id) {
    poll();
    cyui::ScopedID scope(id);
    rendered_rows_ = rendered_edges_ = 0;
    selected_visible_ = frame_capped_ = false;
    ImGui::TextWrapped(
        "%s", cyui::tr("Stored Nanite pages and full-leaf surfaces; no runtime LOD or material evaluation.")
                  .c_str());
    if (comparing_) {
        auto before = cyui::tr("Before"), after = cyui::tr("After");
        const char *labels[] = {before.c_str(), after.c_str()};
        int side = after_ ? 1 : 0;
        if (cyui::segmented("##nanite-side", labels, 2, &side, -1))
            set_after(side != 0);
    }
    bool cluster = cluster_preview_;
    if (ImGui::Checkbox(cyui::trLabel("Selected cluster preview").c_str(), &cluster))
        set_cluster_preview(cluster);
    if (comparing_) {
        bool attributes = attributes_;
        if (ImGui::Checkbox(cyui::trLabel("Compare common-surface attributes").c_str(), &attributes))
            set_compare_attributes(attributes);
    }
    if (busy_)
        cyui::textDim("%s", cyui::tr("Preparing Nanite review…").c_str());
    if (!error_.empty())
        cyui::callout(cyui::Tone::error, error_.c_str());
    if (!review_)
        return;
    const auto &summary = review_->comparison->difference.summary;
    if (comparing_) {
        if (!summary.value("complete", false))
            cyui::callout(
                cyui::Tone::warning,
                cyui::tr("Comparison is incomplete; unavailable channels cannot prove equality.").c_str());
        bool first = true;
        for (const auto &item : {std::make_pair(ChangeKind::added, "addedTriangles"),
                                 std::make_pair(ChangeKind::removed, "removedTriangles"),
                                 std::make_pair(ChangeKind::modified, "modifiedAttributeTriangles")}) {
            const auto label = cyui::tr(change_name(item.first)) + ": " +
                               std::to_string(summary.value(item.second, std::uint64_t(0)));
            if (!first && ImGui::GetContentRegionAvail().x >
                              ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().ItemSpacing.x)
                ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, detail::change_color(item.first));
            ImGui::TextUnformatted(label.c_str());
            ImGui::PopStyleColor();
            first = false;
        }
        std::string statuses;
        for (const char *key : {"encodingStatus", "surfaceStatus", "attributeStatus"})
            if (summary.contains(key)) {
                if (!statuses.empty())
                    statuses += " / ";
                statuses += cyui::tr(key) + ": " + cyui::tr(summary[key].get<std::string>().c_str());
            }
        ImGui::TextWrapped("%s", statuses.c_str());
    }
    if (!review_->notice.empty())
        cyui::callout(cyui::Tone::info, cyui::tr(review_->notice.c_str()).c_str());
    if (ImGui::GetContentRegionAvail().x < cyui::dp(900)) {
        const auto inspector_label = cyui::tr("Inspector"), surface = cyui::tr("Surface");
        const char *labels[] = {inspector_label.c_str(), surface.c_str()};
        int mode = compact_surface_ ? 1 : 0;
        if (cyui::segmented("##nanite-compact", labels, 2, &mode, -1))
            compact_surface_ = mode != 0;
        if (compact_surface_)
            viewport();
        else {
            ImGui::BeginChild("##nanite-inspector");
            inspector();
            ImGui::EndChild();
        }
    } else if (layout_.begin_table("##nanite-layout", "columns", 2,
                                   ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        layout_.column("Inspector", 0, .45f);
        layout_.column("Surface", 1, .55f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::BeginChild("##nanite-inspector");
        inspector();
        ImGui::EndChild();
        ImGui::TableSetColumnIndex(1);
        viewport();
        layout_.end_table();
    }
}
void NaniteView::inspector() {
    cyui::FilterOptions f;
    f.hint = "Find page / cluster";
    f.width = std::min(cyui::dp(250), ImGui::GetContentRegionAvail().x);
    if (cyui::filterBox("##nanite-filter", filter_, f))
        rebuild();
    if (comparing_) {
        if (ImGui::Checkbox(cyui::trLabel("Changed clusters only").c_str(), &changed_only_))
            rebuild();
        if (cyui::button(cyui::trLabel("Next changed cluster").c_str())) {
            next_change();
            return;
        }
    }
    if (review_->scene->rows_capped)
        cyui::callout(
            cyui::Tone::warning,
            cyui::tr("Cluster list is limited to 100,000 rows; numeric selection remains available.")
                .c_str());
    const auto list_height = std::clamp(ImGui::GetContentRegionAvail().y * .24f, cyui::dp(80), cyui::dp(170));
    if (ImGui::BeginTable("##nanite-clusters", 4,
                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, list_height))) {
        for (auto label : {"Page / Cluster", "Vertices", "Triangles", "Changes"})
            ImGui::TableSetupColumn(cyui::tr(label).c_str());
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(visible_.size()));
        if (scroll_)
            for (std::size_t i = 0; i < visible_.size(); ++i) {
                const auto &r = review_->scene->rows[visible_[i]];
                if (r.page == page_ && r.cluster == cluster_) {
                    clip.IncludeItemByIndex(static_cast<int>(i));
                    break;
                }
            }
        bool select = false;
        std::uint32_t selected_p = 0, selected_c = 0;
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const auto &r = review_->scene->rows[visible_[std::size_t(i)]];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(i);
                bool current = r.page == page_ && r.cluster == cluster_;
                if (ImGui::Selectable(r.label.c_str(), current, ImGuiSelectableFlags_SpanAllColumns)) {
                    select = true;
                    selected_p = r.page;
                    selected_c = r.cluster;
                }
                if (current) {
                    selected_visible_ = ImGui::IsItemVisible();
                    if (scroll_) {
                        ImGui::SetScrollHereY(.5f);
                        scroll_ = false;
                    }
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%u", r.vertices);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u", r.triangles);
                ImGui::TableSetColumnIndex(3);
                if (r.leaf)
                    ImGui::Text("%u", r.changes);
                else
                    ImGui::TextUnformatted(cyui::tr("Coarse").c_str());
                ImGui::PopID();
                ++rendered_rows_;
            }
        ImGui::EndTable();
        if (select) {
            select_cluster(selected_p, selected_c);
            return;
        }
    }
    int p = page_, c = cluster_;
    ImGui::SetNextItemWidth(std::min(cyui::dp(110), ImGui::GetContentRegionAvail().x));
    if (ImGui::InputInt(cyui::trLabel("Page").c_str(), &p) && p >= 0 && p <= 16383) {
        select_cluster(p, cluster_);
        return;
    }
    if (ImGui::GetContentRegionAvail().x > cyui::dp(300))
        ImGui::SameLine();
    ImGui::SetNextItemWidth(std::min(cyui::dp(110), ImGui::GetContentRegionAvail().x));
    if (ImGui::InputInt(cyui::trLabel("Cluster").c_str(), &c) && c >= 0 && c <= 255) {
        select_cluster(page_, c);
        return;
    }
    const char *names[] = {"Resource / cluster", "Vertex", "Triangle corners", "Stored hierarchy"};
    if (ImGui::BeginCombo(cyui::trLabel("Nanite inspector").c_str(), cyui::tr(names[inspector_]).c_str())) {
        int chosen = -1;
        for (int i = 0; i < 4; ++i)
            if (ImGui::Selectable(cyui::trLabel(names[i]).c_str(), inspector_ == i))
                chosen = i;
        ImGui::EndCombo();
        if (chosen >= 0) {
            set_inspector(chosen);
            return;
        }
    }
    int index = inspector_ == 1 ? vertex_ : inspector_ == 2 ? triangle_ : node_;
    if (inspector_) {
        ImGui::SetNextItemWidth(std::min(cyui::dp(110), ImGui::GetContentRegionAvail().x));
        if (ImGui::InputInt(cyui::trLabel(inspector_ == 1   ? "Vertex"
                                          : inspector_ == 2 ? "Triangle"
                                                            : "Hierarchy node")
                                .c_str(),
                            &index) &&
            index >= 0 &&
            index <= (inspector_ == 1   ? 255
                      : inspector_ == 2 ? 127
                                        : 65535)) {
            if (inspector_ == 1)
                select_vertex(index);
            else if (inspector_ == 2)
                select_triangle(index);
            else
                select_node(index);
            return;
        }
    }
    details_.draw("##nanite-details");
}

void NaniteView::register_translations() {
    PropertyView::register_translations();
    cyui::registerTranslations("en", {{"encodingStatus", "Encoding"},
                                      {"surfaceStatus", "Surface"},
                                      {"attributeStatus", "Shading"},
                                      {"decoded", "Decoded"},
                                      {"empty", "Empty"},
                                      {"stripped", "Stripped"},
                                      {"identical", "Unchanged"},
                                      {"changed", "Modified"},
                                      {"unavailable", "Unavailable"},
                                      {"not-applicable", "Not applicable"},
                                      {"not-requested", "Not requested"}});
    cyui::registerTranslations(
        "fr",
        {{"Selection unavailable in this resource; choose a valid page, cluster or index.",
          "Sélection indisponible dans cette ressource ; choisissez une page, un cluster ou un indice "
          "valide."},
         {"Inspector", "Inspecteur"},
         {"Surface", "Surface"},
         {"Stored Nanite pages and full-leaf surfaces; no runtime LOD or material evaluation.",
          "Pages Nanite et surfaces de pleine résolution stockées ; aucun calcul de LOD ou de matériau à "
          "l’exécution."},
         {"Selected cluster preview", "Aperçu du cluster sélectionné"},
         {"Compare common-surface attributes", "Comparer les attributs des surfaces communes"},
         {"Preparing Nanite review…", "Préparation de la vue Nanite…"},
         {"Comparison is incomplete; unavailable channels cannot prove equality.",
          "Comparaison incomplète ; les canaux indisponibles ne permettent pas de conclure à l’égalité."},
         {"encodingStatus", "Encodage"},
         {"surfaceStatus", "Surface"},
         {"attributeStatus", "Attributs"},
         {"decoded", "Décodé"},
         {"empty", "Vide"},
         {"stripped", "Données retirées"},
         {"identical", "Inchangé"},
         {"changed", "Modifié"},
         {"unavailable", "Indisponible"},
         {"not-applicable", "Sans objet"},
         {"not-requested", "Non demandé"},
         {"Overlapping triangle identity is ambiguous; corner comparison is deterministic.",
          "L’identité des triangles superposés est ambiguë ; la comparaison des coins est déterministe."},
         {"Select a reported change to inspect aligned before/after corners.",
          "Sélectionnez un changement signalé pour inspecter les coins avant/après alignés."},
         {"Fit Nanite surface", "Cadrer la surface Nanite"},
         {"Nanite geometry is unavailable; absence is not established.",
          "Géométrie Nanite indisponible ; son absence n’est pas établie."},
         {"No stored triangles in this selection", "Aucun triangle stocké dans cette sélection"},
         {"An empty resource or invalid selection has no wire preview.",
          "Une ressource vide ou une sélection invalide n’a pas d’aperçu filaire."},
         {"%llu triangles / %zu prepared edges", "%llu triangles / %zu arêtes préparées"},
         {"Viewport edge limit reached; comparison counts retain their full scope.",
          "Limite d’arêtes de l’aperçu atteinte ; les compteurs de comparaison conservent leur portée "
          "complète."},
         {"Find page / cluster", "Rechercher une page ou un cluster"},
         {"Changed clusters only", "Clusters modifiés uniquement"},
         {"Next changed cluster", "Cluster modifié suivant"},
         {"Cluster list is limited to 100,000 rows; numeric selection remains available.",
          "La liste est limitée à 100 000 clusters ; la sélection numérique reste disponible."},
         {"Page / Cluster", "Page / Cluster"},
         {"Vertices", "Sommets"},
         {"Triangles", "Triangles"},
         {"Changes", "Changements"},
         {"Coarse", "Simplifié"},
         {"Page", "Page"},
         {"Cluster", "Cluster"},
         {"Resource / cluster", "Ressource / cluster"},
         {"Vertex", "Sommet"},
         {"Triangle corners", "Coins du triangle"},
         {"Stored hierarchy", "Hiérarchie stockée"},
         {"Nanite inspector", "Inspecteur Nanite"},
         {"Triangle", "Triangle"},
         {"Hierarchy node", "Nœud de hiérarchie"}});
}
} // namespace cyuediff
